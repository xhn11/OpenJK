# Lists the places in the JK2 game code that assume a single player (entity 0), per file, for the M7
# audit (docs/coop/player-audit.md). Lines that already go through the coop helpers are not listed.
#   powershell -ExecutionPolicy Bypass -File scripts/coop/audit.ps1 [-Detail]
param([switch]$Detail)
$ErrorActionPreference = "Stop"
$game = Resolve-Path "$PSScriptRoot\..\..\codeJK2\game"
$pattern = 'g_entities\[0\]|level\.clients\[0\]|s\.number\s*==\s*0|s\.number\s*!=\s*0|!\s*\w+->s\.number\b|\bplayer->'
$skip = 'G_IsPlayer|G_PlayerNum|G_ClosestPlayer|coop'
$hits = Get-ChildItem "$game\*.cpp" | Select-String -Pattern $pattern | Where-Object { $_.Line -notmatch $skip -and $_.Line.Trim() -notmatch '^//' }
$byFile = $hits | Group-Object Filename | Sort-Object Count -Descending
foreach ($g in $byFile) {
  Write-Host ("{0,5}  {1}" -f $g.Count, $g.Name)
  if ($Detail) { $g.Group | ForEach-Object { Write-Host ("        {0}: {1}" -f $_.LineNumber, $_.Line.Trim()) } }
}
Write-Host ("total {0} lines in {1} files" -f $hits.Count, $byFile.Count)
