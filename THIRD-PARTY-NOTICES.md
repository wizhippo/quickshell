# Windows dependency notices

Quickshell and this Windows port are covered by [LICENSE](LICENSE) (LGPLv3)
and the incorporated [LICENSE-GPL](LICENSE-GPL). Dependencies retain their own
licenses. This is an inventory of the current build, not a complete attribution
inventory for a future bundled runtime.

## Qt

The verified kit is Qt 6.11.2, MSVC 2022 64-bit. The application dynamically
links Qt libraries; Quickshell's own modules are linked statically. Qt Test is
used for tests, and Shader Tools is used for build-time shader processing.

For an open-source LGPL deployment, include applicable Qt license texts,
copyright notices and third-party attributions for every deployed library,
QML module, plugin and support file. Supply matching Qt source and any patches
through a license-compliant source-delivery method. The project's license
texts alone do not cover all Qt third-party notices.

The installed kit's `sbom` directory can help identify dependencies. Verify it
against the final package and the matching Qt source. The build script does
not bundle Qt or collect its complete notices.

References: [Qt licensing](https://doc.qt.io/qt-6/licensing.html),
[LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations),
and [Shader Tools licensing](https://doc.qt.io/qt-6/qtshadertools-index.html#licenses-and-attributions).

## Microsoft C++/WinRT

The notification-history backend incorporates C++/WinRT projection headers
from the Windows SDK. C++/WinRT is copyright Microsoft Corporation and
MIT-licensed. The complete notice is included in
[LICENSES/CPPWINRT-MIT.txt](LICENSES/CPPWINRT-MIT.txt).

Upstream: [microsoft/cppwinrt](https://github.com/microsoft/cppwinrt)
and [its license](https://github.com/microsoft/cppwinrt/blob/master/LICENSE).

## Windows APIs and Visual C++ runtime

Windows SDK import libraries, including `WindowsApp.lib`, resolve APIs provided
by Windows. These dependencies do not license Windows itself under LGPLv3.
Do not copy operating-system DLLs into the release merely because they appear
in a dependency list.

Visual C++ runtime redistribution is governed separately by Microsoft's terms.
Use permitted release redistributables; debug runtime DLLs are not
redistributable. The build script neither installs nor bundles these files.

References: [Microsoft redistribution rules](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files),
[supported redistributables](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170),
and [debug runtime restrictions](https://learn.microsoft.com/en-us/cpp/windows/preparing-a-test-machine-to-run-a-debug-executable?view=msvc-170).
