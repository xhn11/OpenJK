# Dot-source: . "$PSScriptRoot\layout.ps1"
Add-Type -AssemblyName System.Windows.Forms
$script:coopArea = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$script:coopHalf = [int]($coopArea.Width / 2)
$script:coopW    = $coopHalf - 16                                   # okraj kolem rámečku okna
$script:coopH    = [Math]::Min([int]($coopW * 3 / 4), $coopArea.Height - 48)

function Get-WindowArgs([int]$slot) {
  $x = $coopArea.X + $slot * $coopHalf + 8
  $y = $coopArea.Y + 32                                             # místo pro titulek okna
  "+set r_fullscreen 0 +set r_mode -1 +set r_customwidth $coopW +set r_customheight $coopH +set vid_xpos $x +set vid_ypos $y"
}

function Get-HeadlessArgs() {
  "+set vid_hidden 1 +set s_initsound 0 +set in_mouse 0 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480"
}
