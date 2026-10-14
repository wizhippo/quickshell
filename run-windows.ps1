param(
    [string]$Config = (Join-Path $PSScriptRoot 'examples\windows-panel\shell.qml'),
    [string]$QtRoot = 'C:\Qt',
    [string]$QtVersion = '6.11.2',
    # Must match the directory build-windows.ps1 last used.
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build-quickshell'),
    [ValidateSet('panel', 'shell')]
    [string]$Example = 'panel',
    [ValidateRange(0, 2147483647)]
    [int]$QuitAfter = 0,
    [switch]$NoExplorerPrompts
)
if ($Example -eq 'shell' -and -not $PSBoundParameters.ContainsKey('Config')) {
    $Config = Join-Path $PSScriptRoot 'examples\windows-shell\shell.qml'
}

$qtBin = Join-Path $QtRoot "$QtVersion\msvc2022_64\bin"
if (-not (Test-Path -LiteralPath $qtBin)) {
    throw "Qt binaries not found at '$qtBin'. Install the MSVC 2022 64-bit component with the Qt Maintenance Tool."
}
$env:PATH = "$qtBin;" + $env:PATH

$executable = Join-Path $BuildDirectory 'quickshell.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Quickshell executable not found at '$executable'. Run '.\build-windows.ps1' first, or supply -BuildDirectory."
}

$launchArguments = @('-p', $Config)
if ($Example -eq 'shell' -and -not $NoExplorerPrompts -and $QuitAfter -eq 0) {
    $launchArguments += '--manage-explorer'
}
if ($QuitAfter -gt 0) {
    $launchArguments += @('--quit-after', $QuitAfter.ToString())
}
& $executable @launchArguments
exit $LASTEXITCODE