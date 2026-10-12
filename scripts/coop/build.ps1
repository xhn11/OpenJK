# Builds and installs the JK2 SP targets into build/ and install/.
#   -Tests  also builds the coop unit tests (ctest --test-dir build -C Release -R coop)
#   -JA     compile check only: builds the Jedi Academy SP engine (shares code/ with JK2) in build-ja/, no install
param([string]$Config = "Release", [switch]$Tests, [switch]$JA)
$ErrorActionPreference = "Stop"
$root  = Resolve-Path "$PSScriptRoot\..\.."
if ($JA) {
  $buildJA = Join-Path $root "build-ja"
  $flagsJA = @(
    "-DBuildMPEngine=OFF","-DBuildMPRdVanilla=OFF","-DBuildMPDed=OFF","-DBuildMPGame=OFF",
    "-DBuildMPCGame=OFF","-DBuildMPUI=OFF","-DBuildMPRend2=OFF",
    "-DBuildSPEngine=ON","-DBuildSPGame=OFF","-DBuildSPRdVanilla=OFF",
    "-DBuildJK2SPEngine=OFF","-DBuildJK2SPGame=OFF","-DBuildJK2SPRdVanilla=OFF",
    "-DBuildTests=OFF","-DBuildCoopTests=OFF"
  )
  cmake -S $root -B $buildJA -G "Visual Studio 17 2022" -A x64 @flagsJA
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  cmake --build $buildJA --config $Config --parallel -- /nologo /v:minimal
  exit $LASTEXITCODE
}
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
