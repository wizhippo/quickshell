param([string]$BuildDirectory = (Join-Path $PSScriptRoot '..\..\..\build-quickshell'))
$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\Qt\6.11.2\msvc2022_64\bin;' + $env:PATH
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class TooltipSmoke {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr p);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    public static IntPtr Find(uint pid, bool tooltip) {
        IntPtr result=IntPtr.Zero;
        EnumWindows((h,p) => {
            uint owner; GetWindowThreadProcessId(h,out owner);
            if(owner!=pid || !IsWindowVisible(h)) return true;
            var text=new StringBuilder(256); GetWindowTextW(h,text,text.Capacity);
            if((text.ToString()=="Quickshell Windows Panel") != tooltip) { result=h; return false; }
            return true;
        },IntPtr.Zero);
        return result;
    }
}
'@
[void][TooltipSmoke]::SetThreadDpiAwarenessContext([IntPtr](-4))
$originalCursor = [TooltipSmoke+Point]::new()
[void][TooltipSmoke]::GetCursorPos([ref]$originalCursor)
$originalEdge = $env:QS_TOOLTIP_TEST_BOTTOM
$executable = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'quickshell.exe'))
$config = Join-Path $PSScriptRoot 'manual\tooltip.qml'
try {
    foreach ($bottom in @($false,$true)) {
        $env:QS_TOOLTIP_TEST_BOTTOM = if ($bottom) { '1' } else { '0' }
        $child = Start-Process -FilePath $executable -ArgumentList @('-p', ('"' + $config + '"'), '--quit-after', '6000') -WindowStyle Hidden -PassThru
        try {
            $bar = [IntPtr]::Zero
            for ($attempt=0; $attempt -lt 40 -and $bar -eq [IntPtr]::Zero; $attempt++) {
                Start-Sleep -Milliseconds 100
                $bar = [TooltipSmoke]::Find($child.Id,$false)
            }
            if ($bar -eq [IntPtr]::Zero) { throw 'Test bar did not appear.' }
            $barRect = [TooltipSmoke+Rect]::new()
            [void][TooltipSmoke]::GetWindowRect($bar,[ref]$barRect)
            [void][TooltipSmoke]::SetCursorPos($barRect.Left+30,[int](($barRect.Top+$barRect.Bottom)/2))
            $tooltip = [IntPtr]::Zero
            for ($attempt=0; $attempt -lt 30 -and $tooltip -eq [IntPtr]::Zero; $attempt++) {
                Start-Sleep -Milliseconds 100
                $tooltip = [TooltipSmoke]::Find($child.Id,$true)
            }
            if ($tooltip -eq [IntPtr]::Zero) { throw 'Separate tooltip window did not appear.' }
            $tipRect = [TooltipSmoke+Rect]::new()
            [void][TooltipSmoke]::GetWindowRect($tooltip,[ref]$tipRect)
            if ($bottom -and $tipRect.Bottom -gt $barRect.Top) { throw 'Bottom-bar tooltip overlaps the bar.' }
            if (-not $bottom -and $tipRect.Top -lt $barRect.Bottom) { throw 'Top-bar tooltip overlaps the bar.' }
            Write-Output "PASS: tooltip does not overlap the $($(if($bottom){'bottom'}else{'top'})) bar."
        } finally {
            if (-not $child.HasExited) { Stop-Process -Id $child.Id }
        }
    }
} finally {
    [void][TooltipSmoke]::SetCursorPos($originalCursor.X,$originalCursor.Y)
    $env:QS_TOOLTIP_TEST_BOTTOM = $originalEdge
}
