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
using System.Text;
using System.Runtime.InteropServices;
public static class LauncherSmoke {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint message, IntPtr w, IntPtr l);
    public static IntPtr Find(uint pid, string title, bool visibleOnly) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h,p) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            var text = new StringBuilder(256); GetWindowTextW(h, text, text.Capacity);
            bool matches = title == "apps" ? text.ToString().StartsWith("Windows shell") && text.ToString().EndsWith("apps") : text.ToString() == title;
            if(owner == pid && matches && (!visibleOnly || IsWindowVisible(h))) {
                found = h; return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
function Wait-Window([uint32]$ownerId, [string]$title, [bool]$visibleOnly) {
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $handle = [LauncherSmoke]::Find($ownerId, $title, $visibleOnly)
        if ($handle -ne [IntPtr]::Zero) { return $handle }
        Start-Sleep -Milliseconds 100
    }
    throw "Window not found: $title"
}
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\examples\windows-shell'))
$artifacts = Join-Path $BuildDirectory ('launcher-smoke-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($artifacts)
$config = Join-Path $artifacts 'config'
Copy-Item -LiteralPath $source -Destination $config -Recurse
$executable = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'quickshell.exe'))
$entry = Join-Path $config 'shell.qml'
# Record the registration result without altering the real example.
Add-Content -LiteralPath $entry -Value '// Launcher shortcut integration test'
$stdout = Join-Path $artifacts 'stdout.log'
$stderr = Join-Path $artifacts 'stderr.log'
$child = Start-Process -FilePath $executable -ArgumentList @('-p', ('"' + $entry + '"'), '--quit-after', '20000') `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    $receiver = Wait-Window $child.Id 'Quickshell Launcher Shortcut' $false
    Start-Sleep -Milliseconds 600
    # Exercise WM_HOTKEY routing without injecting physical keys or switching input languages.
    if (-not [LauncherSmoke]::PostMessageW($receiver, 0x312, [IntPtr]1, [IntPtr]0)) { throw 'Cannot post hotkey message' }
    # STARTF_USESHOWWINDOW/SW_HIDE suppresses the child's first ordinary window.
    # Consume that initial show/hide, then verify subsequent visible activation.
    [void](Wait-Window $child.Id 'apps' $false)
    [void][LauncherSmoke]::PostMessageW($receiver, 0x312, [IntPtr]1, [IntPtr]0)
    Start-Sleep -Milliseconds 400
    [void][LauncherSmoke]::PostMessageW($receiver, 0x312, [IntPtr]1, [IntPtr]0)
    [void](Wait-Window $child.Id 'apps' $true)
    [void][LauncherSmoke]::PostMessageW($receiver, 0x312, [IntPtr]1, [IntPtr]0)
    Start-Sleep -Milliseconds 400
    if ([LauncherSmoke]::Find($child.Id, 'apps', $true) -ne [IntPtr]::Zero) { throw 'Shortcut did not close launcher' }
    Add-Content -LiteralPath $entry -Value '// Trigger hot reload'
    Start-Sleep -Milliseconds 2000
    $afterReload = Wait-Window $child.Id 'Quickshell Launcher Shortcut' $false
    if ($afterReload -ne $receiver) { throw 'Shortcut receiver changed during reload' }
    [void][LauncherSmoke]::PostMessageW($receiver, 0x312, [IntPtr]1, [IntPtr]0)
    [void](Wait-Window $child.Id 'apps' $true)
    Write-Output 'PASS: launcher shortcut opens/closes apps and survives hot reload.'
} finally {
    if (-not $child.HasExited) { Stop-Process -Id $child.Id }
    Get-Content -LiteralPath $stdout
    Get-Content -LiteralPath $stderr
}
$diagnostics = (Get-Content -LiteralPath $stdout -Raw) + (Get-Content -LiteralPath $stderr -Raw)
if ($diagnostics -match 'ERROR|WARN scene') { throw "QML diagnostics: $diagnostics" }
Write-Output "Logs: $artifacts"
