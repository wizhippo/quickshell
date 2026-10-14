param(
    [string]$QtRoot = 'C:\Qt',
    [string]$QtVersion = '6.11.2',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build-quickshell'),
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug',
    [switch]$Tests,
    [string[]]$CMakeArgs
)
$ErrorActionPreference = 'Stop'

$cmake = Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe'
$ninja = Join-Path $QtRoot 'Tools\Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw "CMake not found at '$cmake'." }
if (-not (Test-Path -LiteralPath $ninja)) { throw "Ninja not found at '$ninja'." }

$qtPrefix = Join-Path $QtRoot "$QtVersion\msvc2022_64"
if (-not (Test-Path -LiteralPath $qtPrefix)) {
    throw "Qt MSVC kit not found at '$qtPrefix'. Install the MSVC 2022 64-bit component with the Qt Maintenance Tool."
}

# Ninja drives cl.exe, which needs the MSVC and Windows SDK environment that
# vcvars64.bat sets up. Capture it into this process instead of requiring the
# caller to open a developer prompt first.
# vswhere's -version filter matches the Visual Studio product version (17.x for
# VS2022), not the product year, so take the latest installation that actually
# carries the C++ toolset instead of guessing a range.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = if (Test-Path -LiteralPath $vswhere) {
    $found = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($found) { ($found | Select-Object -First 1).Trim() } else { '' }
} else { '' }
if (-not $installation) {
    throw 'MSVC build tools not found. Install the "Desktop development with C++" workload.'
}
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat not found at '$vcvars'." }

$environment = cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0) { throw 'vcvars64.bat failed to initialize the MSVC environment.' }
foreach ($entry in $environment) {
    if ($entry -notmatch '^([^=]+)=(.*)$') { continue }
    [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
}
$env:PATH = "$qtPrefix\bin;" + $env:PATH

$arguments = @(
    '-S', $PSScriptRoot,
    '-B', $BuildDirectory,
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    '-DCMAKE_C_COMPILER=cl',
    '-DCMAKE_CXX_COMPILER=cl',
    # Precompiled headers add nothing here and complicate a from-scratch build.
    '-DNO_PCH=ON',
    "-DCMAKE_PREFIX_PATH=$qtPrefix"
)
if ($Tests) { $arguments += '-DBUILD_TESTING=ON' }
if ($CMakeArgs) { $arguments += $CMakeArgs }

& $cmake @arguments
if ($LASTEXITCODE -ne 0) { throw 'Quickshell configuration failed.' }
& $cmake --build $BuildDirectory --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Quickshell build failed.' }

# Stage project notices. Corresponding source and bundled Qt notices are
# separate release work, described in WINDOWS-RELEASE.md.
$legalDirectory = Join-Path $BuildDirectory 'legal'
New-Item -ItemType Directory -Path $legalDirectory -Force | Out-Null
foreach ($notice in @('LICENSE', 'LICENSE-GPL', 'NOTICE', 'THIRD-PARTY-NOTICES.md', 'WINDOWS-RELEASE.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $notice) -Destination $legalDirectory -Force
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LICENSES') -Destination $legalDirectory -Recurse -Force

$cache = Join-Path $BuildDirectory 'CMakeCache.txt'
$configuredBuild = (Select-String -LiteralPath $cache -Pattern '^CMAKE_BUILD_TYPE:STRING=(.*)$').Matches.Groups[1].Value
$configuredQt = (Select-String -LiteralPath $cache -Pattern '^Qt6_DIR:PATH=(.*)$').Matches.Groups[1].Value
$sourceCommit = 'unknown'
$sourceState = 'unknown'
if ((Test-Path -LiteralPath (Join-Path $PSScriptRoot '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) {
    $revision = & git -C $PSScriptRoot rev-parse HEAD 2>$null
    if ($LASTEXITCODE -eq 0) {
        $sourceCommit = $revision.Trim()
        $changes = & git -C $PSScriptRoot status --porcelain --untracked-files=normal
        if ($LASTEXITCODE -eq 0) {
            $sourceState = if ($changes) { 'modified (including untracked files)' } else { 'clean' }
        }
    }
}
@(
    'Quickshell Windows build information'
    "Build type: $configuredBuild"
    "Qt CMake package: $configuredQt"
    "Source commit: $sourceCommit"
    "Working tree: $sourceState"
    'This is build provenance, not a source snapshot or a compliance certification.'
    'Supply matching complete source and dependency notices with a binary release.'
) | Set-Content -LiteralPath (Join-Path $legalDirectory 'BUILD-INFO.txt') -Encoding utf8
Write-Host "Project notices staged in '$legalDirectory'. See WINDOWS-RELEASE.md before distributing binaries."
