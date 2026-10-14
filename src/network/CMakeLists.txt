if (WIN32)
	# Windows reads the interfaces Windows manages; the NetworkManager backend
	# and everything it drags in (Qt DBus) are Unix only.
	set(QS_NETWORK_BACKEND_NM OFF)
else()
	set(QS_NETWORK_BACKEND_NM ON)
endif()

if (QS_NETWORK_BACKEND_NM)
	add_subdirectory(nm)
else()
	add_subdirectory(win)
endif()

qt_add_library(quickshell-network STATIC
	network.cpp
	device.cpp
	wired.cpp
	wifi.cpp
	enums.cpp
	qml.cpp
)

if (QS_NETWORK_BACKEND_NM)
	target_compile_definitions(quickshell-network PRIVATE QS_NETWORK_BACKEND_NM)
endif()

qt_add_qml_module(quickshell-network
	URI Quickshell.Networking
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-network Quickshell)
install_qml_module(quickshell-network)
target_link_libraries(quickshell-network PRIVATE Qt::Qml Qt::Gui)
if (QS_NETWORK_BACKEND_NM)
	target_link_libraries(quickshell-network PRIVATE quickshell-network-nm Qt::DBus)
	qs_add_link_dependencies(quickshell-network quickshell-dbus)
else()
	target_link_libraries(quickshell-network PRIVATE quickshell-network-win)
endif()
target_link_libraries(quickshell PRIVATE quickshell-networkplugin)