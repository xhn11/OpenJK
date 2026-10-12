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
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$work\host", "$work\guest" | Out-Null

function Start-Instance([string]$name, [int]$slot, [string[]]$extra) {
  $display = if ($Visible) { Get-WindowArgs $slot } else { Get-HeadlessArgs }
  $argLine = "+set fs_basepath `"$base`" +set fs_homepath `"$work\$name`" +set logfile 2 " + $display + " " + ($extra -join " ")
  Start-Process -FilePath $exe -ArgumentList $argLine -WorkingDirectory $bin -PassThru
}

$procs = @{}
$procs.host = Start-Instance "host" 0 (@("+set coop_name HostBot") + $HostArgs + @("+devmap $Map"))
if (-not $NoGuest) {
  Start-Sleep -Seconds 3
  $procs.guest = Start-Instance "guest" 1 (@("+set coop_name GuestBot") + $GuestArgs)
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
