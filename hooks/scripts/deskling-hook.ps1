# Windows PowerShell version of deskling-hook.sh, for machines without Git Bash.
# Use it from ~/.claude/settings.json with "shell": "powershell" (see hooks/README.md).
#
#   deskling-hook.ps1          fire-and-forget
#   deskling-hook.ps1 sync     quick round trip, prints the relay's reply
#   deskling-hook.ps1 live     fire-and-forget tool activity (what the session is doing now)
#   deskling-hook.ps1 wait     PermissionRequest / AskUserQuestion: wait for an answer on the screen

param([string]$Mode = "")

$ErrorActionPreference = "SilentlyContinue"
$payload = [Console]::In.ReadToEnd()
# Started by the Deskling runner over ACP: the runner already shows this session.
if ($env:DESKLING_BRIDGED -eq "1") { exit 0 }

# ~/.deskling/config wins over the deskling.conf a personal copy of the plugin carries.
$fileVars = @{}
$confs = @()
if ($env:CLAUDE_PLUGIN_ROOT) { $confs += (Join-Path $env:CLAUDE_PLUGIN_ROOT "deskling.conf") }
$confs += (Join-Path $HOME ".deskling\config")
foreach ($conf in $confs) {
  if (Test-Path $conf) {
    foreach ($line in Get-Content $conf) {
      if ($line -match '^\s*([A-Z_]+)\s*=\s*"?([^"]*)"?\s*$') { $fileVars[$Matches[1]] = $Matches[2] }
    }
  }
}
function Pick($pluginKey, $envKey, $default) {
  $v = [Environment]::GetEnvironmentVariable($pluginKey)
  if (-not $v) { $v = [Environment]::GetEnvironmentVariable($envKey) }
  if (-not $v) { $v = $fileVars[$envKey] }
  if (-not $v) { $v = $default }
  return $v
}

$url = Pick "CLAUDE_PLUGIN_OPTION_RELAY_URL" "DESKLING_URL" ""
$token = Pick "CLAUDE_PLUGIN_OPTION_TOKEN" "DESKLING_TOKEN" ""
$name = Pick "CLAUDE_PLUGIN_OPTION_DEVICE_NAME" "DESKLING_NAME" $env:COMPUTERNAME
$wait = [int](Pick "CLAUDE_PLUGIN_OPTION_APPROVAL_WAIT" "DESKLING_WAIT" "30")
if (-not $url -or -not $token) { exit 0 }

$url = $url.TrimEnd("/")
$headers = @{
  "Authorization" = "Bearer $token"
  "X-Deskling-Host" = $name
  "X-Deskling-OS" = "windows"
}
# The folder the session started in names it, wherever Claude has cd'd to since.
if ($env:CLAUDE_PROJECT_DIR) { $headers["X-Deskling-Project"] = Split-Path -Leaf $env:CLAUDE_PROJECT_DIR }
$body = [System.Text.Encoding]::UTF8.GetBytes($payload)
$stream = Pick "CLAUDE_PLUGIN_OPTION_STREAM" "DESKLING_STREAM" "1"

# The conversation as it happens (deskling-stream.ps1; a problem there never breaks this hook).
function Send-Transcript([switch]$Background) {
  if ($stream -eq "0") { return }
  $script = Join-Path $PSScriptRoot "deskling-stream.ps1"
  try {
    if ($Background) {
      # In its own hidden process, so this hook returns at once (hooks have short time limits).
      $tmp = [System.IO.Path]::GetTempFileName()
      [System.IO.File]::WriteAllText($tmp, $payload)
      $exe = (Get-Process -Id $PID).Path
      $quoted = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$script`"", "-Url", "`"$url`"", "-Token", "`"$token`"", "-PayloadFile", "`"$tmp`"")
      Start-Process -FilePath $exe -ArgumentList $quoted -WindowStyle Hidden | Out-Null
    } else {
      & $script -Url $url -Token $token -Payload $payload
    }
  } catch { }
}

# Keep listening: true once you've typed something in the terminal or app since $from (Claude Code
# queues it), so the hook lets go and your message runs.
function Test-Typed([string]$path, [int64]$from) {
  try {
    $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
      if ($fs.Length -le $from) { return $false }
      [void]$fs.Seek($from, [System.IO.SeekOrigin]::Begin)
      $reader = New-Object System.IO.StreamReader($fs)
      return $reader.ReadToEnd().Contains('"operation":"enqueue"')
    } finally { $fs.Dispose() }
  } catch { return $false }
}

try {
  if ($Mode -eq "wait") {
    Send-Transcript -Background
    if ($wait -le 0) { exit 0 }
    $res = Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$url/hook?wait=$wait" -Headers $headers `
      -ContentType "application/json" -Body $body -TimeoutSec ($wait + 10)
    if ($res.Content) { [Console]::Out.Write($res.Content) }
  } elseif ($Mode -eq "stop") {
    Send-Transcript
    # Stop: if "Keep listening" is on, the relay holds this until a message arrives from the screen.
    # The relay answers {"again":true} every ~85 s so no connection stays open too long. If you
    # type in the terminal or app meanwhile, we let go at once so your message runs.
    $tp = ""
    try { $tp = [string]($payload | ConvertFrom-Json).transcript_path } catch { }
    $held = $false
    if ($tp -and (Test-Path -LiteralPath $tp)) {
      try {
        Add-Type -AssemblyName System.Net.Http
        $start = (Get-Item -LiteralPath $tp).Length
        $client = New-Object System.Net.Http.HttpClient
        $client.Timeout = [TimeSpan]::FromSeconds(100)
        foreach ($k in $headers.Keys) { [void]$client.DefaultRequestHeaders.TryAddWithoutValidation($k, $headers[$k]) }
        $held = $true
        $round = 0
        while ($true) {
          $content = New-Object System.Net.Http.ByteArrayContent(, $body)
          $content.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse("application/json")
          $task = $client.PostAsync("$url/hook?hold=1&round=$round", $content)
          while (-not $task.Wait(1000)) {
            if (Test-Typed $tp $start) {
              $client.CancelPendingRequests()
              $rel = New-Object System.Net.Http.StringContent("{`"session_id`":`"$(($payload | ConvertFrom-Json).session_id)`"}", [System.Text.Encoding]::UTF8, "application/json")
              [void]$client.PostAsync("$url/listen/release", $rel).Wait(5000)
              exit 0
            }
          }
          $text = $task.Result.Content.ReadAsStringAsync().Result
          if ($text -and $text -like '*"again":true*') { $round++; if ($round -gt 80) { break }; continue }
          if ($text) { [Console]::Out.Write($text) }
          break
        }
      } catch {
        if ($held) { exit 0 } # the hold failed part-way: let Claude stop normally
      }
      if ($held) { exit 0 }
    }
    $round = 0
    while ($true) {
      $res = Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$url/hook?hold=1&round=$round" -Headers $headers `
        -ContentType "application/json" -Body $body -TimeoutSec 100
      if ($res.Content -and $res.Content -like '*"again":true*') { $round++; if ($round -gt 80) { break }; continue }
      if ($res.Content) { [Console]::Out.Write($res.Content) }
      break
    }
  } elseif ($Mode -eq "sync") {
    $res = Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$url/hook" -Headers $headers `
      -ContentType "application/json" -Body $body -TimeoutSec 4
    if ($res.Content) { [Console]::Out.Write($res.Content) }
    Send-Transcript -Background
  } else {
    $q = if ($Mode -eq "live") { "?live=1" } else { "" }
    Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$url/hook$q" -Headers $headers `
      -ContentType "application/json" -Body $body -TimeoutSec 5 | Out-Null
    Send-Transcript -Background
  }
} catch { }
exit 0
