qt_add_library(quickshell-network-win STATIC
	backend.cpp backend.hpp
	device.cpp device.hpp
)

target_link_libraries(quickshell-network-win PRIVATE
	Qt::Qml Qt::Gui quickshell-core
	iphlpapi ws2_32 wlanapi
)