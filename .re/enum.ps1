Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class E {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
"@
$target = (Get-Process cs2 -ErrorAction SilentlyContinue).Id
Write-Output "cs2 pid: $target"
$cb = [E+EnumProc]{ param($h,$l)
  $pid2 = 0
  [E]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $target) {
    $sb = New-Object System.Text.StringBuilder 256
    [E]::GetWindowTextW($h, $sb, 256) | Out-Null
    Write-Output ("hwnd={0} visible={1} title='{2}'" -f $h, [E]::IsWindowVisible($h), $sb.ToString())
  }
  return $true
}
[E]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
