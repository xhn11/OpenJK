param(
  [string]$Map = "kejim_post",
  [string[]]$HostArgs = @(),
  [string[]]$GuestArgs = @(),
  [switch]$NoSound	# obe hry bez zvuku
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\layout.ps1"
$root    = Resolve-Path "$PSScriptRoot\..\.."
$bin     = Join-Path $root "install\JediOutcast"
$exe     = Join-Path $bin "openjo_sp.x86_64.exe"
$base    = if ($env:JK2_GAMEDATA) { $env:JK2_GAMEDATA } else { "D:\GOG Galaxy\Games\Star Wars Jedi Knight II - Jedi Outcast\GameData" }
$homeDir = Join-Path $root "build\coop-play"                       # zůstává mezi spuštěními (configy, savy)
$sound   = if ($NoSound) { "0" } else { "1" }                      # vzdy explicitne: s_initsound se uklada do configu
New-Item -ItemType Directory -Force "$homeDir\host", "$homeDir\guest" | Out-Null

function Start-Instance([string]$name, [int]$slot, [string[]]$extra) {
  $argLine = "+set fs_basepath `"$base`" +set fs_cdpath `"$bin`" +set fs_homepath `"$homeDir\$name`" +set logfile 2 +set s_initsound $sound " + (Get-WindowArgs $slot) + " " + ($extra -join " ")
  Start-Process -FilePath $exe -ArgumentList $argLine -WorkingDirectory $bin -PassThru
}

$h = Start-Instance "host" 0 (@("+set coop_name Host", "+coop_host") + $HostArgs + @("+devmap $Map"))
Start-Sleep -Seconds 3
$g = Start-Instance "guest" 1 (@("+set coop_name Guest") + $GuestArgs + @("+coop_connect 127.0.0.1:29170"))
Write-Host "Host vlevo (PID $($h.Id)), guest vpravo (PID $($g.Id)). Ukončení: Stop-Process -Id $($h.Id),$($g.Id)"
