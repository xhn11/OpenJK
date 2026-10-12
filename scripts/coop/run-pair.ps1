# Headless smoke test: starts a host (and a guest unless -NoGuest), waits, kills them and checks
# their qconsole.log for the -ExpectHost / -ExpectGuest regex patterns.
# Several patterns need -Command, because -File passes an array as one string:
#   powershell -ExecutionPolicy Bypass -Command "& 'scripts/coop/run-pair.ps1' -ExpectHost 'a','b'"
param(
  [string]$Map = "kejim_post",
  [int]$Seconds = 25,
  [string[]]$HostArgs = @(),
  [string[]]$GuestArgs = @(),
  [string[]]$ExpectHost = @(),
  [string[]]$ExpectGuest = @(),
  [switch]$NoGuest,
  [switch]$Visible
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
  $argLine = "+set fs_basepath `"$base`" +set fs_cdpath `"$bin`" +set fs_homepath `"$work\$name`" +set logfile 2 " + $display + " " + ($extra -join " ")
  Start-Process -FilePath $exe -ArgumentList $argLine -WorkingDirectory $bin -PassThru
}

$procs = @{}
$failed = $false
try {
  $procs.host = Start-Instance "host" 0 (@("+set coop_name HostBot") + $HostArgs + @("+devmap $Map"))
  if (-not $NoGuest) {
    Start-Sleep -Seconds 3
    $procs.guest = Start-Instance "guest" 1 (@("+set coop_name GuestBot") + $GuestArgs)
  }
  Start-Sleep -Seconds $Seconds
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

function Test-Log([string]$name, [string[]]$patterns) {
  $log = Get-ChildItem -Recurse -Filter qconsole.log "$work\$name" -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $log) { if ($patterns.Count) { Write-Host "[FAIL] ${name}: no qconsole.log"; return $false }; return $true }
  $text = Get-Content $log.FullName -Raw
  $ok = $true
  if ($text -match 'Failed to load jospgame') { Write-Host "[FAIL] ${name}: game DLL failed to load"; $ok = $false }
  if ($name -eq "host") { $patterns = @('------- Game Initialization -------') + $patterns }
  foreach ($p in $patterns) {
    if ($text -match $p) { Write-Host "[PASS] $name ~ $p" } else { Write-Host "[FAIL] $name missing: $p"; $ok = $false }
  }
  return $ok
}
if (-not (Test-Log "host" $ExpectHost)) { $failed = $true }
if (-not $NoGuest -and -not (Test-Log "guest" $ExpectGuest)) { $failed = $true }
if ($failed) { exit 1 } else { Write-Host "SMOKE PASS"; exit 0 }
