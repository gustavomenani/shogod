Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class W2 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$proc = Get-Process cs2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Output "no cs2"; exit 1 }
$script:hwnd = [IntPtr]::Zero
$cb = [W2+EnumProc]{ param($h,$l)
  $pid2 = 0; [W2]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $proc.Id -and [W2]::IsWindowVisible($h)) {
    $sb = New-Object System.Text.StringBuilder 256
    [W2]::GetWindowTextW($h, $sb, 256) | Out-Null
    if ($sb.Length -gt 0) { $script:hwnd = $h }
  }
  return $true
}
[W2]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:hwnd -eq [IntPtr]::Zero) { Write-Output "no window"; exit 1 }
[W2]::SetForegroundWindow($script:hwnd) | Out-Null
Start-Sleep -Milliseconds 500
[W2]::keybd_event(0x2D,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 120
[W2]::keybd_event(0x2D,0,2,[UIntPtr]::Zero); Start-Sleep -Milliseconds 800
$r = New-Object W2+RECT
[W2]::GetWindowRect($script:hwnd,[ref]$r) | Out-Null
$w = $r.Right-$r.Left; $h = $r.Bottom-$r.Top
$bmp = New-Object System.Drawing.Bitmap $w,$h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left,$r.Top,0,0,$bmp.Size)
$bmp.Save("C:\Users\User\Documents\shogod\shots\game\menu_ingame.png",[System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "saved ${w}x${h}"
