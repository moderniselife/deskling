# Windows PowerShell version of stream_transcript in deskling-hook.sh: sends the new lines of the
# session's transcript to the relay, which keeps Claude's words, thoughts, steps and pictures for
# the screen. Tool output (file contents, command output) is filtered out here and never sent,
# except images. One upload at a time per session; the offset is remembered.
# deskling-hook.ps1 runs it inside a try block, so a problem here never breaks the hook.
#
#   deskling-stream.ps1 -Url <relay> -Token <token> -Payload <hook JSON>
#   deskling-stream.ps1 -Url <relay> -Token <token> -PayloadFile <file with the hook JSON>  (deleted after)

param([string]$Url, [string]$Token, [string]$Payload, [string]$PayloadFile)

$ErrorActionPreference = "Stop"
if ($PayloadFile) {
  $Payload = [System.IO.File]::ReadAllText($PayloadFile)
  Remove-Item -LiteralPath $PayloadFile -Force -ErrorAction SilentlyContinue
}
$info = $Payload | ConvertFrom-Json
$sid = [string]$info.session_id
$tp = [string]$info.transcript_path
if (-not $sid -or -not $tp -or -not (Test-Path -LiteralPath $tp)) { return }

$state = Join-Path $HOME ".deskling\state"
New-Item -ItemType Directory -Force -Path $state | Out-Null
$lock = Join-Path $state "$sid.lock"
# A lock older than a minute is from an upload that died.
if (Test-Path -LiteralPath $lock) {
  if ((Get-Item -LiteralPath $lock).LastWriteTime -lt (Get-Date).AddMinutes(-1)) { Remove-Item -LiteralPath $lock -Force -Recurse }
}
try { New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null } catch { return }

try {
  $offFile = Join-Path $state "$sid.off"
  $fs = [System.IO.File]::Open($tp, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
  try {
    $size = $fs.Length
    $off = -1
    if (Test-Path -LiteralPath $offFile) {
      $saved = (Get-Content -LiteralPath $offFile -Raw).Trim()
      if ($saved -match '^\d+$') { $off = [int64]$saved }
    }
    if ($off -lt 0) { $off = [Math]::Max([int64]0, $size - 65536) }
    if ($off -gt $size) { $off = 0 }
    if ($size -le $off) { return }
    $max = [int][Math]::Min([int64]2097152, $size - $off)
    $buf = New-Object byte[] $max
    [void]$fs.Seek($off, [System.IO.SeekOrigin]::Begin)
    $got = 0
    while ($got -lt $max) {
      $n = $fs.Read($buf, $got, $max - $got)
      if ($n -le 0) { break }
      $got += $n
    }
  } finally { $fs.Dispose() }

  if ($got -le 0) { return }
  # Only whole lines: leave a half-written last line for next time.
  $use = [Array]::LastIndexOf($buf, [byte]10, $got - 1) + 1
  if ($got -gt 0 -and $use -le 0 -and $got -ge 2097152) { $use = $got } # one enormous line: skip past it
  if ($use -le 0) { return }
  $text = [System.Text.Encoding]::UTF8.GetString($buf, 0, $use)

  $send = New-Object System.Collections.Generic.List[string]
  foreach ($line in ($text -split "`n")) {
    $l = $line.TrimEnd("`r")
    if (-not $l) { continue }
    if ($l.Contains('"type":"tool_result"')) {
      if ($l.Contains('"type":"image"')) { $send.Add($l) }
      continue
    }
    if ($l.Contains('"type":"attachment"')) { continue }
    if ($l -match '^\{"type":"(last-prompt|mode|atis-latch|frame-link)"') { continue }
    $send.Add($l)
  }

  # Replying to a bit of Claude's message: the app attaches the quoted words as a file that only
  # exists here. Send them so the screen can show what you replied to.
  $quotes = @{}
  foreach ($m in [regex]::Matches($text, '"([^"]*excerpt_from_previous_claude_message\.txt)"')) {
    $p = $m.Groups[1].Value.Replace('\\', '\').Replace('\/', '/')
    if ($quotes.Count -lt 5 -and -not $quotes.ContainsKey($p)) { $quotes[$p] = $true }
  }
  foreach ($p in $quotes.Keys) {
    try {
      if (-not (Test-Path -LiteralPath $p)) { continue }
      $words = [System.IO.File]::ReadAllText($p)
      if ($words.Length -gt 1200) { $words = $words.Substring(0, 1200) }
      $leaf = [Uri]::EscapeDataString((Split-Path -Leaf $p))
      Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$Url/quote?sid=$sid&name=$leaf" -Headers @{ "Authorization" = "Bearer $Token" } `
        -ContentType "text/plain; charset=utf-8" -Body ([System.Text.Encoding]::UTF8.GetBytes($words)) -TimeoutSec 3 | Out-Null
    } catch { }
  }

  if ($send.Count -gt 0) {
    $body = [System.Text.Encoding]::UTF8.GetBytes(($send -join "`n") + "`n")
    Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$Url/transcript?sid=$sid" -Headers @{ "Authorization" = "Bearer $Token" } `
      -ContentType "application/x-ndjson" -Body $body -TimeoutSec 20 | Out-Null
  }
  Set-Content -LiteralPath $offFile -Value ($off + $use)
} finally {
  Remove-Item -LiteralPath $lock -Force -Recurse -ErrorAction SilentlyContinue
}
