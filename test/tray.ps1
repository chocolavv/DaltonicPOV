param([string] $Exe = "$PSScriptRoot\..\bin\DaltonicPOV.exe", [int] $Screen = 1)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new()
# Test hand for the tray app: the real exe, limited to one output, picks every mode the way the menu
# does (WM_COMMAND, so the user's focus stays) and checks each state; one STEP line and PNG a step.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  public delegate bool Each(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(Each e, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
  public static List<string> Overlays(uint pid) {
    var found = new List<string>();
    EnumWindows((h, l) => {
      var s = new StringBuilder(64); uint p; RECT r;
      GetClassName(h, s, 64); GetWindowThreadProcessId(h, out p);
      if (p == pid && s.ToString() == "DaltonicFilter" && IsWindowVisible(h) && GetWindowRect(h, out r))
        found.Add(r.L + "," + r.T + "," + (r.R - r.L) + "x" + (r.B - r.T));
      return true; }, IntPtr.Zero);
    return found;
  }
  public static IntPtr Tray(uint pid) {  // this app's own tray window, not another running copy's
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      var s = new StringBuilder(64); uint p;
      GetClassName(h, s, 64); GetWindowThreadProcessId(h, out p);
      if (p == pid && s.ToString() == "DaltonicPOV") found = h;
      return found == IntPtr.Zero; }, IntPtr.Zero);
    return found;
  }
}
'@
[void][W]::SetProcessDpiAwarenessContext([IntPtr]::new(-4))
$out = Join-Path $PSScriptRoot '..\Temp\tray'
New-Item -ItemType Directory -Force $out | Out-Null
$s = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
$want = "$($s.Bounds.X),$($s.Bounds.Y),$($s.Bounds.Width)x$($s.Bounds.Height)"
$fg = [W]::GetForegroundWindow()
# A quiet colour field under the overlay (never activated): every hue left to right, white at the top.
Add-Type -ReferencedAssemblies System.Windows.Forms, System.Drawing, System.ComponentModel.Primitives -TypeDefinition @'
public class Quiet : System.Windows.Forms.Form {
  protected override bool ShowWithoutActivation { get { return true; } }
  protected override System.Windows.Forms.CreateParams CreateParams { get {
    var p = base.CreateParams; p.ExStyle |= 0x08000000 | 0x00000008 | 0x80; return p; } }
}
'@
$field = [System.Drawing.Bitmap]::new($s.Bounds.Width, $s.Bounds.Height)
$g = [System.Drawing.Graphics]::FromImage($field)
$full = [System.Drawing.Rectangle]::new(0, 0, $field.Width, $field.Height)
$hues = [System.Drawing.Drawing2D.LinearGradientBrush]::new($full, 'Red', 'Red', 0.0)
$blend = [System.Drawing.Drawing2D.ColorBlend]::new(7)
$blend.Colors = [System.Drawing.Color[]]@('Red', 'Yellow', 'Lime', 'Cyan', 'Blue', 'Magenta', 'Red')
$blend.Positions = [single[]]@(0, (1 / 6), (2 / 6), (3 / 6), (4 / 6), (5 / 6), 1)
$hues.InterpolationColors = $blend
$g.FillRectangle($hues, $full)
$g.FillRectangle([System.Drawing.Drawing2D.LinearGradientBrush]::new($full, 'White', [System.Drawing.Color]::FromArgb(0, 255, 255, 255), 90.0), $full)
$form = [Quiet]::new()
$form.FormBorderStyle = 'None'; $form.StartPosition = 'Manual'; $form.Bounds = $s.Bounds; $form.TopMost = $true
$form.BackgroundImage = $field
$form.Show(); [System.Windows.Forms.Application]::DoEvents()
$env:DALTONICPOV_SCREEN = "$Screen"; $env:DALTONICPOV_MARK = '1'
$app = Start-Process $Exe -PassThru
$failed = 0

function Shot($path) {
  $b = [System.Drawing.Bitmap]::new($s.Bounds.Width, $s.Bounds.Height)
  $g = [System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen($s.Bounds.Location, [System.Drawing.Point]::Empty, $b.Size)
  $c = $b.GetPixel(2, 2); $b.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $b.Dispose()
  return $c.R -eq 255 -and $c.G -eq 0 -and $c.B -eq 255
}

# One step: send a menu command, wait for the expected overlays, check and record the state.
function Step($n, $name, $cmd, [int] $overlays) {
  $t0 = Get-Date
  if ($cmd) { [void][W]::SendMessage([W]::Tray($app.Id), 0x111, [IntPtr]$cmd, [IntPtr]::Zero) }
  do { $o = @([W]::Overlays($app.Id)); if ($o.Count -eq $overlays) { break }; Start-Sleep -Milliseconds 100 }
  while (((Get-Date) - $t0).TotalSeconds -lt 3)
  Start-Sleep -Milliseconds 400  # the first frame reaches the overlay
  $mark = Shot (Join-Path $out "$n-$name.png")
  $ok = $o.Count -eq $overlays -and ($overlays -eq 0 -or ($o[0] -eq $want -and $mark)) -and
        ($overlays -ne 0 -or -not $mark) -and [W]::GetForegroundWindow() -eq $fg
  if (-not $ok) { $script:failed++ }
  "STEP $n $name | overlays $($o.Count) [$($o -join ' ')] want $overlays [$want] | mark seen $mark | " +
    "focus kept $([W]::GetForegroundWindow() -eq $fg) | $([int]((Get-Date) - $t0).TotalMilliseconds) ms | $(if ($ok) {'ok'} else {'FAIL'})"
}

$t0 = Get-Date
while (-not [W]::Tray($app.Id) -and ((Get-Date) - $t0).TotalSeconds -lt 5) { Start-Sleep -Milliseconds 100 }
Step 0 'start' 0 0
Step 1 'plugin-0' 100 1
# A topmost window shown after the overlay, as a taskbar thumbnail is: the overlay must stay above it.
$top = [Quiet]::new(); $top.FormBorderStyle = 'None'; $top.StartPosition = 'Manual'; $top.TopMost = $true
$top.BackColor = 'Lime'; $top.Bounds = [System.Drawing.Rectangle]::new($s.Bounds.X + 600, $s.Bounds.Y + 300, 400, 300)
$top.Show(); [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 400
$b = [System.Drawing.Bitmap]::new($s.Bounds.Width, $s.Bounds.Height); $g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($s.Bounds.Location, [System.Drawing.Point]::Empty, $b.Size); $c = $b.GetPixel(800, 450)
$b.Save((Join-Path $out '2-topmost.png'), [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $b.Dispose(); $top.Close()
$ok = "$($c.R),$($c.G),$($c.B)" -ne '0,255,0' -and [W]::GetForegroundWindow() -eq $fg
if (-not $ok) { $failed++ }
"STEP 2 topmost | window under it $($c.R),$($c.G),$($c.B) (bare 0,255,0) | focus kept $([W]::GetForegroundWindow() -eq $fg) | $(if ($ok) {'ok'} else {'FAIL'})"
Step 3 'plugin-1' 101 1
Step 4 'plugin-2' 102 1
Step 5 'normal' 99 0
[void][W]::SendMessage([W]::Tray($app.Id), 0x111, [IntPtr]3, [IntPtr]::Zero)
$gone = $app.WaitForExit(3000)
if (-not $gone) { $failed++; $app.Kill() }
"STEP 6 exit | process ended $gone | overlays $(@([W]::Overlays($app.Id)).Count) | $(if ($gone) {'ok'} else {'FAIL'})"
$form.Close()
"failed $failed"
exit $failed
