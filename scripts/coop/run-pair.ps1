# Headless smoke test: starts a host (and a guest unless -NoGuest), waits, kills them and checks
# their qconsole.log: every -ExpectHost / -ExpectGuest regex must match, no -RejectHost / -RejectGuest
# regex may match. -HostAfterMap: host commands that run after "+devmap <Map>" (the load is synchronous).
# Several patterns need -Command, because -File passes an array as one string:
#   powershell -ExecutionPolicy Bypass -Command "& 'scripts/coop/run-pair.ps1' -ExpectHost 'a','b'"
param(
  [string]$Map = "kejim_post",
  [int]$Seconds = 25,
  [string[]]$HostArgs = @(),
  [string[]]$HostAfterMap = @(),
  [string[]]$GuestArgs = @(),
  [string[]]$ExpectHost = @(),
  [string[]]$ExpectGuest = @(),
  [string[]]$RejectHost = @(),
  [string[]]$RejectGuest = @(),
  [switch]$NoGuest,
  [switch]$GuestNoMap,	# the guest never loads a map (e.g. only coop_find): skip its game DLL check
  [switch]$Visible,
  [int]$Port = 29190	# not the default 29170: automated runs must never meet the games the user plays (play-pair.ps1)
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\layout.ps1"
$root = Resolve-Path "$PSScriptRoot\..\.."
$bin  = Join-Path $root "install\JediOutcast"
$exe  = Join-Path $bin "openjo_sp.x86_64.exe"
$base = if ($env:JK2_GAMEDATA) { $env:JK2_GAMEDATA } else { "D:\GOG Galaxy\Games\Star Wars Jedi Knight II - Jedi Outcast\GameData" }
$work = Join-Path $root "build\coop-smoke"
if (Test-Path $work) {
  try { Remove-Item -Recurse -Force $work -ErrorAction Stop }
  catch { Write-Host "[FAIL] cannot clear $work ($($_.Exception.Message)) - is a game from an earlier run still running?"; exit 1 }
}
New-Item -ItemType Directory -Force "$work\host", "$work\guest" | Out-Null

function Start-Instance([string]$name, [int]$slot, [string[]]$extra) {
  $display = if ($Visible) { Get-WindowArgs $slot } else { Get-HeadlessArgs }
  $argLine = "+set fs_basepath `"$base`" +set fs_cdpath `"$bin`" +set fs_homepath `"$work\$name`" +set logfile 2 +set coop_port $Port " + $display + " " + (($extra -join " ") -replace "29170", "$Port")
  Start-Process -FilePath $exe -ArgumentList $argLine -WorkingDirectory $bin -PassThru
}

$procs = @{}
$failed = $false
try {
  $procs.host = Start-Instance "host" 0 (@("+set coop_name HostBot") + $HostArgs + @("+devmap $Map") + $HostAfterMap)
  if (-not $NoGuest) {
    Start-Sleep -Seconds 3
    $procs.guest = Start-Instance "guest" 1 (@("+set coop_name GuestBot") + $GuestArgs)
  }
  # -Seconds is the limit: once every expected line is in both logs the test is done (rejects are checked
  # on what was logged until then)
  $deadline = (Get-Date).AddSeconds($Seconds)
  $expects = @{ host = $ExpectHost; guest = $ExpectGuest }
  $canEnd = ($ExpectHost.Count + $ExpectGuest.Count) -gt 0
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 1
    if (-not $canEnd) { continue }
    $done = $true
    foreach ($k in $procs.Keys) {
      if (-not $expects[$k].Count) { continue }
      $log = Join-Path $work "$k\base\qconsole.log"
      $text = if (Test-Path $log) { Get-Content $log -Raw -ErrorAction SilentlyContinue } else { "" }
      foreach ($p in $expects[$k]) { if ("$text" -notmatch $p) { $done = $false; break } }
      if (-not $done) { break }
    }
    if ($done) { Start-Sleep -Seconds 1; break }
  }
  foreach ($k in $procs.Keys) {
    if ($procs[$k].HasExited) { Write-Host "[FAIL] $k exited early (code $($procs[$k].ExitCode))"; $failed = $true }
  }
} finally {
  # always kill what we started, also on errors and Ctrl+C
  foreach ($p in $procs.Values) {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    try { Wait-Process -Id $p.Id -Timeout 10 -ErrorAction Stop } catch { if (-not $p.HasExited) { Write-Host "[WARN] process $($p.Id) did not exit" } }
  }
}

function Test-Log([string]$name, [string[]]$patterns, [string[]]$rejects, [bool]$needGame = $true) {
  $log = Get-ChildItem -Recurse -Filter qconsole.log "$work\$name" -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $log) { if ($patterns.Count) { Write-Host "[FAIL] ${name}: no qconsole.log"; return $false }; return $true }
  $text = Get-Content $log.FullName -Raw
  $ok = $true
  if ($text -match 'Failed to load jospgame') { Write-Host "[FAIL] ${name}: game DLL failed to load"; $ok = $false }
  # from M3 on the guest loads the host's map too, so both must have loaded the game DLL
  if ($needGame) { $patterns = @('------- Game Initialization -------') + $patterns }
  foreach ($p in $patterns) {
    if ($text -match $p) { Write-Host "[PASS] $name ~ $p" } else { Write-Host "[FAIL] $name missing: $p"; $ok = $false }
  }
  foreach ($p in $rejects) {
    if ($text -match $p) { Write-Host "[FAIL] $name contains: $p"; $ok = $false } else { Write-Host "[PASS] $name !~ $p" }
  }
  return $ok
}
if (-not (Test-Log "host" $ExpectHost $RejectHost)) { $failed = $true }
if (-not $NoGuest -and -not (Test-Log "guest" $ExpectGuest $RejectGuest (-not $GuestNoMap))) { $failed = $true }
if ($failed) { exit 1 } else { Write-Host "SMOKE PASS"; exit 0 }
