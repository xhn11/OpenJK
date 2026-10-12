param(
  [string]$Map = "kejim_post",
  [int]$Seconds = 25,
  [string[]]$HostArgs = @(),
  [string[]]$GuestArgs = @(),
  [string[]]$ExpectHost = @(),
  [string[]]$ExpectGuest = @(),
  [switch]$NoGuest
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$bin  = Join-Path $root "install\JediOutcast"
$exe  = Join-Path $bin "openjo_sp.x86_64.exe"
$base = if ($env:JK2_GAMEDATA) { $env:JK2_GAMEDATA } else { "D:\GOG Galaxy\Games\Star Wars Jedi Knight II - Jedi Outcast\GameData" }
$work = Join-Path $root "build\coop-smoke"
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$work\host", "$work\guest" | Out-Null

function Start-Instance([string]$name, [string[]]$extra) {
  $argLine = "+set fs_basepath `"$base`" +set fs_homepath `"$work\$name`" +set r_fullscreen 0 +set r_mode 3 +set logfile 2 " + ($extra -join " ")
  Start-Process -FilePath $exe -ArgumentList $argLine -WorkingDirectory $bin -PassThru
}

$procs = @{}
$procs.host = Start-Instance "host" (@("+set coop_name HostBot") + $HostArgs + @("+devmap $Map"))
if (-not $NoGuest) {
  Start-Sleep -Seconds 3
  $procs.guest = Start-Instance "guest" (@("+set coop_name GuestBot") + $GuestArgs)
}
Start-Sleep -Seconds $Seconds

$failed = $false
foreach ($k in $procs.Keys) {
  if ($procs[$k].HasExited) { Write-Host "[FAIL] $k exited early (code $($procs[$k].ExitCode))"; $failed = $true }
  else { Stop-Process -Id $procs[$k].Id -Force }
}

function Test-Log([string]$name, [string[]]$patterns) {
  $log = Get-ChildItem -Recurse -Filter qconsole.log "$work\$name" -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $log) { if ($patterns.Count) { Write-Host "[FAIL] ${name}: no qconsole.log"; return $false }; return $true }
  $text = Get-Content $log.FullName -Raw
  $ok = $true
  foreach ($p in $patterns) {
    if ($text -match $p) { Write-Host "[PASS] $name ~ $p" } else { Write-Host "[FAIL] $name missing: $p"; $ok = $false }
  }
  return $ok
}
if (-not (Test-Log "host" $ExpectHost)) { $failed = $true }
if (-not $NoGuest -and -not (Test-Log "guest" $ExpectGuest)) { $failed = $true }
if ($failed) { exit 1 } else { Write-Host "SMOKE PASS"; exit 0 }
