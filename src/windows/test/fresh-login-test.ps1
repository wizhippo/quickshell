param(
    [Parameter(Mandatory)]
    [ValidateSet('Enable', 'Restore', 'Launch')]
    [string]$Action,
    [int]$QuitAfter = 0
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$executable = Join-Path $repository 'build-quickshell\quickshell.exe'
$configuration = Join-Path $repository 'examples\windows-shell\shell.qml'
$qtDirectory = 'C:\Qt\6.11.2\msvc2022_64\bin'
$stateDirectory = Join-Path $env:LOCALAPPDATA 'quickshell\fresh-login-test'
$backupPath = Join-Path $stateDirectory 'policy-backup.json'
$policyPath = 'Software\Microsoft\Windows\CurrentVersion\Policies\System'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value

if ($Action -eq 'Restore') {
    if (-not (Test-Path -LiteralPath $backupPath)) { throw 'No policy backup exists for this user.' }
    $backup = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
    if ($backup.Sid -ne $identity) { throw 'The policy backup belongs to another user.' }
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($policyPath)
    try {
        if ($backup.Existed) {
            $kind = [Enum]::Parse([Microsoft.Win32.RegistryValueKind], [string]$backup.Kind)
            $key.SetValue('Shell', [string]$backup.Value, $kind)
        } else {
            $key.DeleteValue('Shell', $false)
        }
    } finally { $key.Dispose() }
    Remove-Item -LiteralPath $backupPath
    Write-Output 'Original per-user shell policy restored. The next sign-in uses the original shell.'
    exit 0
}

foreach ($path in @($executable, $configuration, (Join-Path $qtDirectory 'Qt6Core.dll'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file missing: $path" }
}

if ($Action -eq 'Enable') {
    if (Test-Path -LiteralPath $backupPath) { throw 'A test backup already exists. Restore it before enabling another test.' }
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($policyPath)
    $backup = [ordered]@{ Sid = $identity; Existed = $false; Kind = 'String'; Value = $null }
    try {
        if ($key -and $key.GetValueNames() -contains 'Shell') {
            $backup.Existed = $true
            $backup.Kind = $key.GetValueKind('Shell').ToString()
            if ($backup.Kind -notin @('String', 'ExpandString')) { throw 'Existing shell policy is not a string; it was left unchanged.' }
            $backup.Value = $key.GetValue('Shell', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        }
    } finally { if ($key) { $key.Dispose() } }
    New-Item -ItemType Directory -Path $stateDirectory -Force | Out-Null
    $backup | ConvertTo-Json | Set-Content -LiteralPath $backupPath -Encoding UTF8
    $powershell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $command = '"' + $powershell + '" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + $PSCommandPath + '" -Action Launch'
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($policyPath)
    try { $key.SetValue('Shell', $command, [Microsoft.Win32.RegistryValueKind]::String) }
    finally { $key.Dispose() }
    Write-Output 'Fresh-login test enabled for this user only. Save your work, sign out, and sign back in.'
    Write-Output "Configured shell: $command"
    Write-Output "Rollback: powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action Restore"
    exit 0
}

# Launch keeps the wrapper alive for the shell's lifetime. It neither starts nor
# stops Explorer and does not enable the private immersive host.
New-Item -ItemType Directory -Path $stateDirectory -Force | Out-Null
$env:PATH = "$qtDirectory;" + $env:PATH
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FreshLoginDesktop {
    [DllImport("user32.dll")] public static extern IntPtr GetShellWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
}
'@
[uint32]$desktopPid = 0
[void][FreshLoginDesktop]::GetWindowThreadProcessId([FreshLoginDesktop]::GetShellWindow(), [ref]$desktopPid)
[ordered]@{
    Time = (Get-Date).ToString('o')
    Sid = $identity
    DesktopPid = $desktopPid
    ExplorerPids = @(Get-Process explorer -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    PrivateHostEnabled = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stateDirectory 'startup.json') -Encoding UTF8
$launchArguments = @('--no-immersive-host', '-p', $configuration)
if ($QuitAfter -gt 0) { $launchArguments += @('--quit-after', $QuitAfter.ToString()) }
& $executable @launchArguments *> (Join-Path $stateDirectory 'quickshell.log')
exit $LASTEXITCODE
