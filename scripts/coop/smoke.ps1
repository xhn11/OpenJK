# All headless coop smoke tests (milestones M0 onward). Usage (from the repo root):
#   powershell -ExecutionPolicy Bypass -File scripts/coop/smoke.ps1 [-Only S5,S10]
# Every test must end with SMOKE PASS; the script prints one line per test and exits 1 if any failed.
# Notes: the host only runs game frames after a command following the map load (hence -HostAfterMap
# '+wait 100' where host frames matter), and kejim_post's intro cutscene blocks saving unless it is
# sped up with timescale.
param([string[]]$Only = @())
$ErrorActionPreference = "Stop"
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$link = "-HostArgs '+coop_host' -GuestArgs '+coop_connect 127.0.0.1:29170'"
$tests = [ordered]@{
  S0  = "-NoGuest -Seconds 20 -ExpectHost 'Server: kejim_post' -RejectHost 'COOP: '"
  S1  = "-Seconds 25 $link -ExpectHost 'COOP: hosting on UDP port 29170','COOP: link up','COOP: hello from GuestBot' -ExpectGuest 'COOP: link up','COOP: hello from HostBot' -RejectHost 'link closed','overflowed' -RejectGuest 'link closed','overflowed'"
  S2  = "-Seconds 25 -HostArgs '+coop_host' -GuestArgs '+set coop_autotest 60000','+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: test payload 60000 bytes ok' -ExpectGuest 'COOP: test payload 60000 bytes sent' -RejectHost 'corrupt','link closed' -RejectGuest 'link closed','overflowed'"
  S3  = "-Seconds 40 -HostArgs '+set coop_simLoss 10','+set coop_simLatency 80','+coop_host' -GuestArgs '+set coop_simLoss 10','+set coop_simLatency 80','+set coop_autotest 3000','+set coop_autotestCount 40','+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: link up','COOP: hello from GuestBot','COOP: test payload 3000 bytes ok' -ExpectGuest 'COOP: link up','COOP: hello from HostBot','COOP: test payload 3000 bytes sent' -RejectHost 'link closed','corrupt' -RejectGuest 'link closed','overflowed'"
  S4  = "-Seconds 40 -HostArgs '+set coop_onlink coop_reloadmap','+coop_host' -GuestArgs '+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: link up','COOP: map load started, peer notified','COOP: map loaded, peer notified' -ExpectGuest 'COOP: peer is loading a map','COOP: peer finished loading' -RejectHost 'link closed' -RejectGuest 'link closed'"
  S5  = "-Seconds 45 -HostArgs '+set coop_timeout 5','+coop_host' -GuestArgs '+set coop_timeout 5','+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: game mode host','COOP: guest entered kejim_post' -ExpectGuest 'COOP: loading kejim_post from host','Server: kejim_post','COOP: in kejim_post, waiting','COOP: game mode guest' -RejectHost 'link closed','no free entities' -RejectGuest 'link closed','no free entities'"
  S6  = "-NoGuest -Seconds 50 -HostAfterMap '+set timescale 8','+wait 2000','+set timescale 1','+save cooptest','+wait 300','+load cooptest' -ExpectHost 'Saving game .cooptest','Loading game .cooptest' -RejectHost 'Failed\.','has version','Can.t savegame'"
  S7  = "-Seconds 45 $link -ExpectHost 'COOP: guest entered kejim_post' -ExpectGuest 'COOP: configstrings synced \(\d{2,} strings\)' -RejectHost 'link closed','overflowed' -RejectGuest 'link closed','Server command overflow','overflowed'"
  S8  = "-Seconds 45 -HostArgs '+set coop_onworld coop_ents','+coop_host' -GuestArgs '+set coop_onworld coop_ents','+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: guest entered kejim_post','COOP: entities \d+ \(ours \d+, other 0, clients \d+\)' -ExpectGuest 'COOP: game mode guest','COOP: entities \d+ \(ours \d+, other [1-9]\d*, clients 1\)' -RejectHost 'link closed' -RejectGuest 'link closed'"
  S9  = "-NoGuest -Seconds 20 -HostArgs '+coop_host' -HostAfterMap '+coop_entinfo 2' -ExpectHost 'COOP: game mode host','COOP: entinfo 2 owner 1 class \S+'"
  S10 = "-Seconds 50 $link -HostAfterMap '+wait 100' -ExpectHost 'COOP: guest entered kejim_post' -ExpectGuest 'COOP: configstrings synced','COOP: world synced \(\d+ entities, \d+ npcs\)' -RejectHost 'link closed','overflowed' -RejectGuest 'link closed','bad snapshot','layout differs','Server command overflow'"
  S11 = "-Seconds 70 -HostArgs '+set coop_simLoss 10','+set coop_simLatency 80','+coop_host' -HostAfterMap '+wait 100' -GuestArgs '+set coop_simLoss 10','+set coop_simLatency 80','+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: guest entered kejim_post' -ExpectGuest 'COOP: world synced' -RejectHost 'link closed' -RejectGuest 'link closed','bad snapshot','Server command overflow'"
  S12 = "-Seconds 60 $link -HostAfterMap '+wait 400','+npc spawn stormtrooper' -ExpectHost 'COOP: guest entered kejim_post' -ExpectGuest 'COOP: world synced \(\d+ entities, [1-9]\d* npcs\)','COOP: replica npc \d+ \(\w+\)' -RejectHost 'link closed' -RejectGuest 'link closed','cannot build NPC','bad snapshot'"
  S13 = "-Seconds 70 $link -HostAfterMap '+wait 100' -ExpectHost 'COOP: guest entered kejim_post' -ExpectGuest 'COOP: world synced' -RejectHost 'link closed' -RejectGuest 'link closed','negative reference count','areaportal of \d+ off by','bad snapshot'"
  S14 = "-Seconds 50 -HostArgs '+coop_host','+coop_cptest CoopCenterPrint' -GuestArgs '+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: guest entered kejim_post' -ExpectGuest 'COOP: server command from host: cp .CoopCenterPrint.' -RejectHost 'link closed' -RejectGuest 'link closed'"
  S15 = "-Seconds 50 $link -HostAfterMap '+wait 100' -ExpectHost 'COOP: guest entered kejim_post','COOP: partner view added to snapshots' -ExpectGuest 'COOP: world synced','COOP: partner view added to snapshots' -RejectHost 'link closed' -RejectGuest 'link closed','bad snapshot'"
  # M4: partner puppets both ways, game commands
  S16 = "-Seconds 50 $link -HostAfterMap '+wait 100' -ExpectHost 'COOP: guest synced','COOP: partner puppet ready \(kyle\)' -ExpectGuest 'COOP: world synced','COOP: partner puppet ready \(kyle\)' -RejectHost 'link closed','bad snapshot','cannot build' -RejectGuest 'link closed','bad snapshot','cannot build'"
  S17 = "-Seconds 50 -HostArgs '+coop_host' -HostAfterMap '+wait 100' -GuestArgs '+set coop_onworld \`"coop_gtest fromguest\`"','+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: game command echo fromguest' -ExpectGuest 'COOP: world synced' -RejectHost 'link closed','unknown game command' -RejectGuest 'link closed'"
  S18 = "-Seconds 50 -HostArgs '+set coop_onworld \`"wait 3000; coop_entinfo 1\`"','+coop_host' -HostAfterMap '+wait 100' -GuestArgs '+set coop_onworld \`"wait 500; setviewpos -100 200 30 0\`"','+coop_connect 127.0.0.1:29170' -ExpectHost 'COOP: entinfo 1 owner 2 class player .* origin -100 200' -ExpectGuest 'COOP: world synced' -RejectHost 'link closed' -RejectGuest 'link closed'"
}
$failed = @()
foreach ($id in $tests.Keys) {
  if ($Only.Count -and $Only -notcontains $id) { continue }
  $out = powershell -ExecutionPolicy Bypass -Command "& '$PSScriptRoot\run-pair.ps1' $($tests[$id])" 2>&1
  $last = ($out | Select-Object -Last 1)
  if ("$last" -eq "SMOKE PASS") { Write-Host "$id PASS" }
  else { Write-Host "$id FAIL"; $out | Where-Object { "$_" -match '^\[FAIL\]' } | ForEach-Object { Write-Host "    $_" }; $failed += $id }
}
if ($failed.Count) { Write-Host "FAILED: $($failed -join ', ')"; exit 1 }
Write-Host "ALL SMOKES PASS"
exit 0
