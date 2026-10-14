# Mirrors src/io/test: each test compiles the module sources it covers
# directly, so the backends can be exercised without a running shell.
function (qs_test name)
	add_executable(${name} ${ARGN})
	# Keep diagnostics visible to CTest even though production Qt apps use the
	# Windows GUI subsystem.
	set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
endfunction()

qs_test(windows-task-windows task_windows.cpp ../task_windows.cpp ../launcher_shortcut.cpp
	../../core/model.cpp)
target_link_libraries(windows-task-windows PRIVATE Qt::Gui Qt::Qml Qt::Test user32 dwmapi shell32 ole32 uuid)
add_test(NAME windows-task-windows COMMAND windows-task-windows)

qs_test(windows-tray-host tray_host.cpp ../tray_host.cpp ../../core/model.cpp)
target_link_libraries(windows-tray-host PRIVATE Qt::Gui Qt::Qml Qt::Test user32 shell32)
add_test(NAME windows-tray-host COMMAND windows-tray-host)

qs_test(windows-power-status power_status.cpp)
target_link_libraries(windows-power-status PRIVATE Qt::Core Qt::Test)
add_test(NAME windows-power-status COMMAND windows-power-status)

qs_test(windows-volume volume.cpp ../volume.cpp ../endpointvolume.cpp)
target_link_libraries(windows-volume PRIVATE Qt::Gui Qt::Qml Qt::Test ole32 uuid user32)
add_test(NAME windows-volume COMMAND windows-volume)

qs_test(windows-notification-history notification_history.cpp ../notification_history.cpp)
target_link_libraries(windows-notification-history PRIVATE Qt::Gui Qt::Qml Qt::Test
	quickshell-core quickshell-window quickshell-ui quickshell-io WindowsApp)
add_test(NAME windows-notification-history COMMAND windows-notification-history)

qs_test(windows-app-scanner desktop_entry_scan.cpp)
target_link_libraries(windows-app-scanner PRIVATE Qt::Gui Qt::Qml Qt::Test
	quickshell-windows-app-scanner quickshell-core quickshell-window quickshell-ui quickshell-io)
add_test(NAME windows-app-scanner COMMAND windows-app-scanner)

qs_test(windows-system system.cpp ../system.cpp)
target_link_libraries(windows-system PRIVATE Qt::Core Qt::Qml Qt::Test
	quickshell-core quickshell-window quickshell-ui quickshell-io user32 shell32 ole32
	quickshell-windows-immersive-host)
add_test(NAME windows-system COMMAND windows-system)

# Test shell-ownership guard; optional manual mode uses the production backend.
add_executable(windows-immersive-host-probe immersive_host_probe.cpp settings_presentation.hpp
	../explorer_session.cpp)
set_target_properties(windows-immersive-host-probe PROPERTIES WIN32_EXECUTABLE FALSE AUTOMOC FALSE)
target_link_libraries(windows-immersive-host-probe PRIVATE quickshell-windows-immersive-host user32 rstrtmgr)
add_test(NAME windows-immersive-host COMMAND windows-immersive-host-probe --guard)
set_tests_properties(windows-immersive-host PROPERTIES SKIP_RETURN_CODE 77)

# The manual configurations and desktop smoke scripts in this directory are
# run against a real desktop rather than by CTest:
#   quickshell -p src/windows/test/manual/<name>.qml
#   pwsh src/windows/test/smoke.ps1
