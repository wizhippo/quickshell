if (WIN32)
	qt_add_executable(quickshell windows/main.cpp windows/explorer_session.cpp)
else()
	qt_add_executable(quickshell main.cpp)
endif()

install(TARGETS quickshell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_subdirectory(build)
if (NOT WIN32)
	add_subdirectory(launch)
endif()
add_subdirectory(core)
add_subdirectory(debug)
add_subdirectory(ipc)
add_subdirectory(window)
add_subdirectory(io)
add_subdirectory(widgets)
add_subdirectory(ui)
add_subdirectory(windowmanager)

if (WIN32)
	add_subdirectory(windows)
endif()

if (CRASH_HANDLER)
	add_subdirectory(crash)
endif()

if (DBUS)
	add_subdirectory(dbus)
endif()

if (WAYLAND)
	add_subdirectory(wayland)
endif()

if (X11)
	add_subdirectory(x11)
endif()

if (I3)
	add_subdirectory(x11/i3)
endif()

add_subdirectory(services)

if (BLUETOOTH)
	add_subdirectory(bluetooth)
endif()

if (NETWORK)
	add_subdirectory(network)
endif()
