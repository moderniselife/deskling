# Windows PowerShell version of deskling-hook.sh, for machines without Git Bash.
# Use it from ~/.claude/settings.json with "shell": "powershell" (see hooks/README.md).
#
#   deskling-hook.ps1          fire-and-forget
#   deskling-hook.ps1 sync     quick round trip, prints the relay's reply
#   deskling-hook.ps1 wait     PermissionRequest / AskUserQuestion: wait for an answer on the screen

param([string]$Mode = "")

$ErrorActionPreference = "SilentlyContinue"
$payload = [Console]::In.ReadToEnd()

$conf = Join-Path $HOME ".deskling\config"
$fileVars = @{}
if (Test-Path $conf) {
  foreach ($line in Get-Content $conf) {
    if ($line -match '^\s*([A-Z_]+)\s*=\s*"?([^"]*)"?\s*$') { $fileVars[$Matches[1]] = $Matches[2] }
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
$body = [System.Text.Encoding]::UTF8.GetBytes($payload)

try {
  if ($Mode -eq "wait") {
    if ($wait -le 0) { exit 0 }
    $res = Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$url/hook?wait=$wait" -Headers $headers `
      -ContentType "application/json" -Body $body -TimeoutSec ($wait + 10)
    if ($res.Content) { [Console]::Out.Write($res.Content) }
  } elseif ($Mode -eq "sync") {
    $res = Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$url/hook" -Headers $headers `
      -ContentType "application/json" -Body $body -TimeoutSec 4
    if ($res.Content) { [Console]::Out.Write($res.Content) }
  } else {
    Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$url/hook" -Headers $headers `
      -ContentType "application/json" -Body $body -TimeoutSec 5 | Out-Null
  }
} catch { }
exit 0
