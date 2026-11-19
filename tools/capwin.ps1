param([string]$exe, [string]$out, [int]$wait = 5, [string]$args2 = "")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class PW { [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public struct RECT { public int L, T, R, B; } }
"@
[PW]::SetProcessDPIAware() | Out-Null
$p = if ($args2) { Start-Process -FilePath $exe -ArgumentList $args2 -WorkingDirectory (Split-Path $exe) -PassThru } else { Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru }
Start-Sleep -Seconds $wait
$p.Refresh(); $h = $p.MainWindowHandle
$r = New-Object PW+RECT; [PW]::GetWindowRect($h, [ref]$r) | Out-Null
$b = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T); $g = [System.Drawing.Graphics]::FromImage($b)
$dc = $g.GetHdc(); [PW]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc); $b.Save($out)
Stop-Process -Id $p.Id -Force
