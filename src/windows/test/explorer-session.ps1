param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\..\..\build-quickshell'),
    [string]$QtRoot = 'C:\Qt',
    [string]$QtVersion = '6.11.2',
    [switch]$ReplaceThenRestore
)
$ErrorActionPreference = 'Stop'
$env:PATH = "$QtRoot\$QtVersion\msvc2022_64\bin;" + $env:PATH
$env:QS_NO_RELOAD_POPUP = '1'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ExplorerDialogSmoke {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr GetShellWindow();
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint message, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
    public static bool Responsive(IntPtr h) {
        IntPtr result;
        return h != IntPtr.Zero && SendMessageTimeoutW(h,0,IntPtr.Zero,IntPtr.Zero,2,1000,out result) != IntPtr.Zero;
    }
    public static IntPtr Dialog(uint pid, string title) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((h,p) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if(owner != pid) return true;
            var text = new StringBuilder(256); GetWindowTextW(h,text,text.Capacity);
            if(text.ToString() == title) { result=h; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
'@
function Desktop-Explorer {
    foreach ($handle in @([ExplorerDialogSmoke]::GetShellWindow(), [ExplorerDialogSmoke]::FindWindow('Shell_TrayWnd', $null))) {
        [uint32]$ownerId = 0
        if ($handle -eq [IntPtr]::Zero) { continue }
        [void][ExplorerDialogSmoke]::GetWindowThreadProcessId($handle, [ref]$ownerId)
        $ownerProcess = Get-Process -Id $ownerId -ErrorAction SilentlyContinue
        if ($ownerProcess -and $ownerProcess.ProcessName -eq 'explorer') { return $ownerId }
    }
    return 0
}
function Wait-Dialog([uint32]$ownerId, [string]$title, [int]$attempts = 80) {
    for ($attempt = 0; $attempt -lt $attempts; $attempt++) {
        $handle = [ExplorerDialogSmoke]::Dialog($ownerId, $title)
        if ($handle -ne [IntPtr]::Zero) { return $handle }
        Start-Sleep -Milliseconds 100
    }
    throw "Dialog did not appear: $title"
}
$before = Desktop-Explorer
$config = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\examples\windows-shell\shell.qml'))
$executable = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'quickshell.exe'))
if ($ReplaceThenRestore -and $before -eq 0) { throw 'Explorer must already own the desktop to exercise replacement.' }
$duration = if ($ReplaceThenRestore) { '15000' } else { '800' }
$artifacts = Join-Path $BuildDirectory ('explorer-session-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($artifacts)
$stdout = Join-Path $artifacts 'stdout.log'
$stderr = Join-Path $artifacts 'stderr.log'
$child = Start-Process -FilePath $executable -ArgumentList @('-p', ('"' + $config + '"'), '--manage-explorer', '--quit-after', $duration) -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    if ($ReplaceThenRestore) {
        # Explicit opt-in test: Explorer closes temporarily and is restored below.
        $dialog = Wait-Dialog $child.Id 'Quickshell - Replace Explorer?'
        [void][ExplorerDialogSmoke]::PostMessageW($dialog, 0x111, [IntPtr]6, [IntPtr]0) # IDYES
        $bar = Wait-Dialog $child.Id 'Quickshell Windows Panel' 150
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            if ((Desktop-Explorer) -ne 0) { throw 'Explorer restarted while Quickshell was running.' }
            if (-not [ExplorerDialogSmoke]::Responsive($bar)) { throw 'Quickshell panel stopped responding.' }
            Start-Sleep -Milliseconds 250
        }
        $dialog = Wait-Dialog $child.Id 'Quickshell - Start Explorer?' 200
        [void][ExplorerDialogSmoke]::PostMessageW($dialog, 0x111, [IntPtr]6, [IntPtr]0) # IDYES
        if (-not $child.WaitForExit(10000)) { throw 'Shell did not exit after confirming restoration.' }
        for ($attempt = 0; $attempt -lt 100 -and (Desktop-Explorer) -eq 0; $attempt++) { Start-Sleep -Milliseconds 100 }
        if ((Desktop-Explorer) -eq 0) { throw 'Explorer did not restore its desktop.' }
        if ($child.ExitCode -ne 0) { throw "Shell exited with $($child.ExitCode)" }
        Write-Output 'PASS: Yes shuts down Explorer, the shell stays responsive without a restart, and exit Yes restores Explorer.'
    } elseif ($before -ne 0) {
        $dialog = Wait-Dialog $child.Id 'Quickshell - Replace Explorer?'
        # Test refusal only. Never stop or start the user's Explorer automatically.
        [void][ExplorerDialogSmoke]::PostMessageW($dialog, 0x111, [IntPtr]7, [IntPtr]0) # IDNO
    } else {
        $dialog = Wait-Dialog $child.Id 'Quickshell - Start Explorer?'
        $tray = [ExplorerDialogSmoke]::FindWindow('Shell_TrayWnd', $null)
        [uint32]$trayOwner = 0
        if ($tray -ne [IntPtr]::Zero) { [void][ExplorerDialogSmoke]::GetWindowThreadProcessId($tray, [ref]$trayOwner) }
        if ($trayOwner -eq $child.Id) { throw 'Own tray was not released before Explorer restoration prompt' }
        [void][ExplorerDialogSmoke]::PostMessageW($dialog, 0x111, [IntPtr]7, [IntPtr]0) # IDNO
    }
    if (-not $ReplaceThenRestore) {
        if (-not $child.WaitForExit(10000)) { throw 'Shell did not exit after declining prompt' }
        if ($child.ExitCode -ne 0) { throw "Shell exited with $($child.ExitCode)" }
        if ((Desktop-Explorer) -ne $before) { throw 'Declining changed Explorer state' }
        if ($before -ne 0) {
            Write-Output 'PASS: declining replacement preserves Explorer and avoids a duplicate restart prompt.'
        } else {
            Write-Output 'PASS: declining restoration leaves Explorer stopped; own tray is released before prompting.'
        }
    }
} finally {
    if (-not $child.HasExited) { Stop-Process -Id $child.Id }
    if ($ReplaceThenRestore -and $before -ne 0 -and (Desktop-Explorer) -eq 0) {
        # Restore the original desktop even if the Yes-path assertion failed.
        Start-Process -FilePath (Join-Path $env:WINDIR 'explorer.exe') -WindowStyle Normal
    }
    Write-Output "Logs: $artifacts"
}
if ($before -ne 0 -and -not $ReplaceThenRestore) {
    $cancelled = Start-Process -FilePath $executable -ArgumentList @('-p', ('"' + $config + '"'), '--manage-explorer', '--quit-after', '800') -PassThru -WindowStyle Hidden
    try {
        $dialog = Wait-Dialog $cancelled.Id 'Quickshell - Replace Explorer?'
        [void][ExplorerDialogSmoke]::PostMessageW($dialog, 0x111, [IntPtr]2, [IntPtr]0) # IDCANCEL
        if (-not $cancelled.WaitForExit(10000)) { throw 'Cancel did not stop launch' }
        if ((Desktop-Explorer) -ne $before) { throw 'Cancel changed Explorer state' }
        Write-Output 'PASS: cancelling startup leaves Explorer running.'
    } finally {
        if (-not $cancelled.HasExited) { Stop-Process -Id $cancelled.Id }
    }
}
