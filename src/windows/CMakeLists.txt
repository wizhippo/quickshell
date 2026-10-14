# Keep private Windows shell interfaces and symbol loading away from Qt headers.
add_library(quickshell-windows-immersive-host STATIC immersive_host.cpp immersive_host.hpp)
set_target_properties(quickshell-windows-immersive-host PROPERTIES AUTOMOC FALSE)
target_link_libraries(quickshell-windows-immersive-host PRIVATE
	dbghelp winhttp ole32 user32 uuid shell32 dwmapi
)

qt_add_library(quickshell-windows STATIC
	panel_window.cpp panel_window.hpp
	power_status.cpp power_status.hpp power_reading.hpp
	endpointvolume.cpp endpointvolume.hpp
	volume.cpp volume.hpp
	inputvolume.cpp inputvolume.hpp
	system.cpp system.hpp
	notification_history.cpp notification_history.hpp
	launcher_shortcut.cpp launcher_shortcut.hpp
	tray_host.cpp tray_host.hpp
	task_windows.cpp task_windows.hpp
	qml.cpp qml.hpp
)

# The plugin constructor must survive static library link pruning.
add_library(quickshell-windows-init OBJECT init.cpp)

qt_add_qml_module(quickshell-windows
	URI Quickshell.Windows
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-windows Quickshell)

install_qml_module(quickshell-windows)

target_link_libraries(quickshell-windows PRIVATE
	Qt::Quick Qt::QuickPrivate quickshell-core
	user32 shell32 ole32 uuid rstrtmgr dwmapi WindowsApp
)

target_link_libraries(quickshell-windows-init PRIVATE Qt::Qml Qt::Quick)

target_link_libraries(quickshell PRIVATE
	quickshell-windowsplugin quickshell-windows-init Qt::Quick Qt::Network
	quickshell-windows-immersive-host
)

if (BUILD_TESTING)
	add_subdirectory(test)
endif()
