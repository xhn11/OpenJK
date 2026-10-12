param([string]$Config = "Release", [switch]$Tests)
$ErrorActionPreference = "Stop"
$root  = Resolve-Path "$PSScriptRoot\..\.."
$build = Join-Path $root "build"
$flags = @(
  "-DBuildMPEngine=OFF","-DBuildMPRdVanilla=OFF","-DBuildMPDed=OFF","-DBuildMPGame=OFF",
  "-DBuildMPCGame=OFF","-DBuildMPUI=OFF","-DBuildMPRend2=OFF",
  "-DBuildSPEngine=OFF","-DBuildSPGame=OFF","-DBuildSPRdVanilla=OFF",
  "-DBuildJK2SPEngine=ON","-DBuildJK2SPGame=ON","-DBuildJK2SPRdVanilla=ON",
  "-DCMAKE_INSTALL_PREFIX=$root\install"
)
if ($Tests) { $flags += "-DBuildCoopTests=ON" }
cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64 @flags
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config $Config --parallel -- /nologo /v:minimal
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --install $build --config $Config | Out-Null
exit $LASTEXITCODE
