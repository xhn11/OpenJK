# Builds dist\JK2Coop: the coop engine, game DLL, renderer and runtime DLLs, start scripts and README_COOP.md.
# No game assets: every player copies the folder into their own Jedi Outcast GameData directory.
#   powershell -ExecutionPolicy Bypass -File scripts/coop/package.ps1 [-NoBuild]
param([switch]$NoBuild)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
if (-not $NoBuild) {
  & "$PSScriptRoot\build.ps1"
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
$bin  = Join-Path $root "install\JediOutcast"
$dist = Join-Path $root "dist\JK2Coop"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force "$dist\OpenJK" | Out-Null

Get-ChildItem $bin -File | Where-Object { $_.Extension -in ".exe", ".dll" } | Copy-Item -Destination $dist
Copy-Item "$bin\OpenJK\jospgamex86_64.dll" "$dist\OpenJK\"
Copy-Item "$PSScriptRoot\..\..\docs\coop\README_COOP.md" $dist

# start scripts: double click, or "join-coop.bat 192.168.1.20"
Set-Content -Encoding ascii "$dist\host-coop.bat" @'
@echo off
rem Hosts a coop game: starts a new campaign on kejim_post (or the map given as the first argument).
set MAP=%1
if "%MAP%"=="" set MAP=kejim_post
start "" "%~dp0openjo_sp.x86_64.exe" +set coop_name "%USERNAME%" +coop_host +map %MAP%
'@
Set-Content -Encoding ascii "$dist\join-coop.bat" @'
@echo off
rem Joins a coop game: join-coop.bat <host IP>[:port]
if "%1"=="" (
  echo usage: join-coop.bat ^<host IP^>[:port]
  pause
  exit /b 1
)
start "" "%~dp0openjo_sp.x86_64.exe" +set coop_name "%USERNAME%" +coop_connect %1
'@
Set-Content -Encoding ascii "$dist\host-coop-load.bat" @'
@echo off
rem Hosts a coop game from a saved game: host-coop-load.bat <save name>
if "%1"=="" (
  echo usage: host-coop-load.bat ^<save name^>
  pause
  exit /b 1
)
start "" "%~dp0openjo_sp.x86_64.exe" +set coop_name "%USERNAME%" +coop_host +load %1
'@
# zz_coop.pk3: the Coop menu, plus the game's own main menu and menu list with it added. The originals
# come from the local Jedi Outcast install (game data is not part of this repository).
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$gameData = if ($env:JK2_GAMEDATA) { $env:JK2_GAMEDATA } else { "D:\GOG Galaxy\Games\Star Wars Jedi Knight II - Jedi Outcast\GameData" }
function Read-PakText([string]$pak, [string]$name) {
  $zip = [System.IO.Compression.ZipFile]::OpenRead($pak)
  try {
    $entry = $zip.Entries | Where-Object { $_.FullName -ieq $name } | Select-Object -First 1
    if (-not $entry) { throw "$name not in $pak" }
    $reader = New-Object System.IO.StreamReader($entry.Open())
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
  } finally { $zip.Dispose() }
}
$assets = Join-Path $gameData "base\assets1.pk3"
$menus = Read-PakText $assets "ui/menus.txt"
$main  = Read-PakText $assets "ui/main.menu"
$menus = $menus.Substring(0, $menus.LastIndexOf("}")) + "`tloadMenu { `"ui/coop.menu`" }`r`n}`r`n"
$coopButton = @'

		// coop (design 28): next to EXIT
		itemDef
		{
			name				coopbutton
			group				othermain
			text				"COOP"
			descText			"Kampan ve dvou pres sit"
			type				ITEM_TYPE_BUTTON
			style				WINDOW_STYLE_EMPTY
			rect				245 444 130 24
			font				3
			textscale			1
			textalign			ITEM_ALIGN_CENTER
			textstyle			3
			textalignx			65
			textaligny			-1
			forecolor			0.65 0.65 1 1
			visible				1
			action
			{
				play			"sound/interface/button1.wav" ;
				close			all ;
				open			coopMenu
			}
		}
'@
$end = $main.LastIndexOf("}")
$end = $main.LastIndexOf("}", $end - 1)		# the menuDef's closing brace
$main = $main.Substring(0, $end) + $coopButton + "`t" + $main.Substring($end)
$pk3 = Join-Path $dist "base\zz_coop.pk3"
New-Item -ItemType Directory -Force (Split-Path $pk3) | Out-Null
$zip = [System.IO.Compression.ZipFile]::Open($pk3, "Create")
try {
  foreach ($f in @(@("ui/menus.txt", $menus), @("ui/main.menu", $main), @("ui/coop.menu", (Get-Content -Raw "$PSScriptRoot\ui\coop.menu")))) {
    $w = New-Object System.IO.StreamWriter($zip.CreateEntry($f[0]).Open())
    try { $w.Write($f[1]) } finally { $w.Dispose() }
  }
} finally { $zip.Dispose() }
# the local test install gets it too (play-pair.ps1, smokes)
Copy-Item $pk3 (Join-Path $bin "base\zz_coop.pk3") -Force

Write-Host "dist ready: $dist"
Get-ChildItem -Recurse $dist | Select-Object -ExpandProperty FullName
