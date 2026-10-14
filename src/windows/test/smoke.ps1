param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\..\..\build-quickshell'),
    [string]$QtRoot = 'C:\Qt',
    [string]$QtVersion = '6.11.2'
)
$ErrorActionPreference = 'Stop'
$env:PATH = "$QtRoot\$QtVersion\msvc2022_64\bin;" + $env:PATH
$env:QS_NO_RELOAD_POPUP = '1'

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class QsSmokeNative {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool SystemParametersInfoW(uint a, uint p, out Rect r, uint f);
    public static IntPtr Find(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h,p) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if(owner == pid && IsWindowVisible(h)) { found=h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static Rect WorkArea() {
        Rect r;
        if(!SystemParametersInfoW(48,0,out r,0)) throw new Exception("Cannot read work area");
        return r;
    }
    public static int CountServiceWindows(uint pid, string windowTitle) {
        int count = 0;
        EnumWindows((h,p) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if(owner == pid) {
                var title = new StringBuilder(128);
                GetWindowTextW(h, title, title.Capacity);
                if(title.ToString() == windowTitle) count++;
            }
            return true;
        }, IntPtr.Zero);
        return count;
    }
}
'@
[void][QsSmokeNative]::SetThreadDpiAwarenessContext([IntPtr](-4))

$artifactDirectory = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'windows-smoke'))
[void][IO.Directory]::CreateDirectory($artifactDirectory)
$configPath = Join-Path $artifactDirectory 'shell.qml'
$stdoutPath = Join-Path $artifactDirectory 'stdout.log'
$stderrPath = Join-Path $artifactDirectory 'stderr.log'

function Write-Config([int]$height, [string]$mode = 'Auto', [bool]$visible = $true, [string]$edge = 'top') {
    $qmlVisible = $visible.ToString().ToLowerInvariant()
    $anchors = switch ($edge) {
        'top' { 'top: true; left: true; right: true' }
        'bottom' { 'bottom: true; left: true; right: true' }
        'left' { 'left: true; top: true; bottom: true' }
        'right' { 'right: true; top: true; bottom: true' }
    }
    $qml = @"
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Widgets
import Quickshell.Windows
ShellRoot {
    Component.onCompleted: {
        PowerStatus.refresh()
        Volume.refresh()
    }
    PanelWindow {
        anchors { $anchors }
        implicitHeight: $height
        implicitWidth: $height
        visible: $qmlVisible
        exclusionMode: ExclusionMode.$mode
        color: "#182433"
        onClosed: Qt.quit()
        ClippingRectangle {
            anchors.fill: parent
            radius: 6
            color: "#182433"
            Text { anchors.centerIn: parent; text: "Windows port smoke test"; color: "white" }
        }
    }
    Process {
        command: ["cmd.exe", "/c", "echo quickshell-process-ok"]
        running: true
        stdout: StdioCollector { onStreamFinished: console.log(text.trim()) }
    }
}
"@
    [IO.File]::WriteAllText($configPath, $qml)
}

function Assert-WorkArea([string]$label, [int]$height, [string]$edge = 'top') {
    Start-Sleep -Milliseconds 1200
    if ($testProcess.HasExited) { throw "$label : process exited unexpectedly ($($testProcess.ExitCode))" }
    $work = [QsSmokeNative]::WorkArea()
    $expected = @{ Left=$baseline.Left; Top=$baseline.Top; Right=$baseline.Right; Bottom=$baseline.Bottom }
    switch ($edge) {
        'top' { $expected.Top += $height }
        'bottom' { $expected.Bottom -= $height }
        'left' { $expected.Left += $height }
        'right' { $expected.Right -= $height }
    }
    foreach ($coordinate in 'Left','Top','Right','Bottom') {
        if ($work.$coordinate -ne $expected[$coordinate]) {
            throw "$label : $coordinate=$($work.$coordinate), expected $($expected[$coordinate])"
        }
    }
    Write-Output "PASS: $label"
    if ([QsSmokeNative]::CountServiceWindows([uint32]$testProcess.Id, 'Quickshell Power Notifications') -ne 1) {
        throw "$label : power notification windows leaked or the service was not created."
    }
    if ([QsSmokeNative]::CountServiceWindows([uint32]$testProcess.Id, 'Quickshell Volume Notifications') -ne 1) {
        throw "$label : volume notification windows leaked or the service was not created."
    }
}

$baseline = [QsSmokeNative]::WorkArea()
Write-Config 40
$testProcess = Start-Process -FilePath (Join-Path $BuildDirectory 'quickshell.exe') `
    -ArgumentList @('-p', ('"' + $configPath + '"'), '--quit-after', '20000') `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
try {
    Start-Sleep -Milliseconds 1500
    $initialHandle = [QsSmokeNative]::Find([uint32]$testProcess.Id)
    if ($initialHandle -eq [IntPtr]::Zero) { throw 'No visible PanelWindow was created.' }
    $dpiRatio = [QsSmokeNative]::GetDpiForWindow($initialHandle) / 96.0
    Assert-WorkArea 'Initial AppBar reservation with DPI scaling' ([int][Math]::Round(40 * $dpiRatio))

    Write-Config 56
    Assert-WorkArea 'File watcher reload resizes reservation' ([int][Math]::Round(56 * $dpiRatio))
    if ([QsSmokeNative]::Find([uint32]$testProcess.Id) -ne $initialHandle) { throw 'Reload did not retain the native window.' }
    Write-Output 'PASS: Reload retains the HWND'

    Write-Config 40 'Ignore'
    Assert-WorkArea 'Ignore mode releases reservation' 0
    Write-Config 40 'Auto' $false
    Assert-WorkArea 'Hidden panel releases reservation' 0
    Write-Config 40
    Assert-WorkArea 'Showing panel restores reservation' ([int][Math]::Round(40 * $dpiRatio))
    foreach ($edge in 'bottom','left','right') {
        Write-Config 40 'Auto' $true $edge
        Assert-WorkArea "$edge edge reservation" ([int][Math]::Round(40 * $dpiRatio)) $edge
    }
} finally {
    $panelHandle = [QsSmokeNative]::Find([uint32]$testProcess.Id)
    if ($panelHandle -ne [IntPtr]::Zero) {
        [void][QsSmokeNative]::PostMessageW($panelHandle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    }
    # The safety timer also handles a failed startup or hidden-panel assertion.
    if (-not $testProcess.WaitForExit(22000)) { throw 'Test process did not exit.' }
    Start-Sleep -Milliseconds 500
    $restored = [QsSmokeNative]::WorkArea()
    foreach ($coordinate in 'Left','Top','Right','Bottom') {
        if ($restored.$coordinate -ne $baseline.$coordinate) { throw 'Workspace was not restored on exit.' }
    }
    if ($testProcess.ExitCode -ne 0) { throw "Process failed with exit code $($testProcess.ExitCode). See $artifactDirectory" }
    Write-Output 'PASS: Clean exit restores workspace'
}
if (-not ((Get-Content $stdoutPath -Raw) -match 'quickshell-process-ok')) { throw 'Quickshell.Io.Process did not produce the expected output.' }
if ((Get-Content $stdoutPath -Raw) -match 'TypeError:|ReferenceError:| ERROR| FATAL') { throw "Runtime errors were logged. See $stdoutPath" }
Write-Output 'PASS: Quickshell.Io.Process launches a Windows command and collects stdout'
Write-Output 'PASS: PowerStatus keeps exactly one notification window across reloads and hidden panels'
Write-Output 'PASS: Volume keeps exactly one notification window across reloads and hidden panels'
