# Preparing a Windows release

This Windows port remains under LGPLv3. The repository already contains the
LGPL/GPL texts and scripts needed to rebuild it. Private builds do not require
publication. Distribution requires matching source, notices and recipient
rights. This guide prepares release materials; it does not certify a particular
installer, store or dependency bundle.

## Build and verify

```powershell
.\build-windows.ps1 -BuildType Release -Tests
$env:PATH = "C:\Qt\6.11.2\msvc2022_64\bin;" + $env:PATH
& C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-quickshell `
    --output-on-failure -E windows-task-windows
.\run-windows.ps1 -Example shell -QuitAfter 4000
```

Task-window tests are excluded here because foreground activation may require
user input. Run them separately on an interactive desktop. Build and run both
default to `build-quickshell`; both accept `-BuildDirectory` overrides.

After a successful build, the build script stages project licenses and notices
in `<build>/legal`, including this guide. `BUILD-INFO.txt` records the configured
build type, Qt kit, source commit and whether the working tree has changes.
Use Release for distribution: Microsoft debug runtime DLLs are not
redistributable. Release alone does not create a deployable package.

## Source accompanying binaries

For an online release, a straightforward approach is to offer complete matching
Quickshell source alongside the binary download, with clear instructions and
equivalent access at no additional charge. Include all port changes, CMake
files, build/run scripts, required assets, configurations and license/notice
files, plus any other material required to build and install the covered work.
Preserve upstream notices and prominently identify modifications and dates.

Commit and tag the actual source used for a public release. `git archive` can
make an archive from that tag, but excludes uncommitted changes, untracked new
backends and submodule contents. Ensure all necessary source is supplied.
A modified-tree marker in `BUILD-INFO.txt` is a reminder to resolve this, not
an exact source snapshot. Original upstream source alone is insufficient for
the modified port.

Provide a release-specific source location or source with the package. A
moving branch, a project home page or this guide alone does not supply the
matching corresponding source. Keep source available for the period required
by your chosen distribution method.

## Binary release materials

- Include `legal/LICENSE`, `legal/LICENSE-GPL`, `legal/NOTICE`,
  `legal/THIRD-PARTY-NOTICES.md`, `legal/LICENSES/CPPWINRT-MIT.txt`, and clear
  source/rebuild instructions. Make notices easy for recipients to find.
- Supply corresponding Qt source for the exact libraries shipped, including
  patches, using an appropriate LGPL distribution method. For an online
  release, host matching source archives alongside the downloads and identify
  their modules and versions. A generic Qt download link alone is not a
  completed source-delivery arrangement.
- Include the applicable Qt license texts, copyright notices and third-party
  attributions for deployed DLLs, plugins, QML imports and support files.
  Review additional dependencies: the inventory in this repository is not
  exhaustive for a bundled runtime.
- Preserve the ability to use interface-compatible modified Qt libraries.
  Verify packaging permits replacement of these dynamically linked libraries.
  Do not forbid modification of the LGPL portions or reverse engineering for
  debugging those modifications.
- Quickshell modules are statically linked into `quickshell.exe`. Provide the
  matching complete source and build material so recipients can rebuild it.
  If combining with proprietary application code, separately satisfy LGPL
  section 4's relinking requirements; this all-source release procedure is
  not a proprietary-linking exception.
- Provide installation information where LGPLv3/GPLv3 require it for a User
  Product. Do not impose signing or installation restrictions that prevent
  recipients from installing and running their modified covered software.
- Use only permitted Microsoft release redistributables under Microsoft's
  terms, or direct users to Microsoft's installer. Do not redistribute debug
  runtime DLLs or arbitrary Windows system DLLs.

The scripts currently build and run against an installed Qt kit. They stage
project notices, but do not bundle Qt, archive corresponding source, assemble
all dependency attributions or produce a reviewed redistributable installer.
Those remain release preparation tasks when applicable.

References: [LGPLv3](https://www.gnu.org/licenses/lgpl-3.0.html),
[GPLv3](https://www.gnu.org/licenses/gpl-3.0.html),
[Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations),
and [Microsoft redistribution rules](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files).
