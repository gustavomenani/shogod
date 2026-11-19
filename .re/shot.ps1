Add-Type -AssemblyName System.Windows.Forms,System.Drawing
$p = Get-Process cs2 -ErrorAction SilentlyContinue | Where-Object {$_.MainWindowHandle -ne 0} | Select-Object -First 1
if (-not $p) { Write-Output "no window"; exit 1 }
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[W]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 400
[W]::keybd_event(0x2D, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 120
[W]::keybd_event(0x2D, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 700
$r = New-Object W+RECT
[W]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$bmp.Save("$PSScriptRoot\..\shots\game\menu_ingame.png", [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "saved ${w}x${h}"
