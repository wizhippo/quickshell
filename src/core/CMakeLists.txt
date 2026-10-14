if (NOT WIN32)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(libdrm REQUIRED IMPORTED_TARGET libdrm)
endif()
qt_add_library(quickshell-core STATIC
	plugin.cpp
	shell.cpp
	variants.cpp
	rootwrapper.cpp
	reload.cpp
	rootwrapper.cpp
	qmlglobal.cpp
	qmlscreen.cpp
	region.cpp
	persistentprops.cpp
	singleton.cpp
	generation.cpp
	scan.cpp
	scanenv.cpp
	qsintercept.cpp
	incubator.cpp
	lazyloader.cpp
	easingcurve.cpp
	iconimageprovider.cpp
	imageprovider.cpp
	transformwatcher.cpp
	boundcomponent.cpp
	model.cpp
	elapsedtimer.cpp
	desktopentry.cpp
	desktopentrymonitor.cpp
	platformmenu.cpp
	qsmenu.cpp
	retainable.cpp
	popupanchor.cpp
	types.cpp
	qsmenuanchor.cpp
	clock.cpp
	paths.cpp
	instanceinfo.cpp
	common.cpp
	iconprovider.cpp
	scriptmodel.cpp
	colorquantizer.cpp
	toolsupport.cpp
	streamreader.cpp
)

if (WIN32)
	# Keep Shell SDK declarations completely out of quickshell-core's Qt
	# compilation environment. This scanner has only native SDK/STL headers.
	add_library(quickshell-windows-app-scanner STATIC
		../windows/desktop_entry_scan.cpp ../windows/desktop_entry_scan.hpp)
	set_target_properties(quickshell-windows-app-scanner PROPERTIES
		AUTOMOC OFF AUTOUIC OFF AUTORCC OFF DISABLE_PRECOMPILE_HEADERS ON
		COMPILE_OPTIONS "/W4;/permissive-;/Zc:preprocessor")
	target_link_libraries(quickshell-windows-app-scanner PRIVATE ole32 shell32)
	target_link_libraries(quickshell-core PRIVATE quickshell-windows-app-scanner)
	target_sources(quickshell-core PRIVATE logging.hpp ../windows/logging.cpp ../windows/debuginfo.cpp)
else()
	target_sources(quickshell-core PRIVATE logging.cpp debuginfo.cpp)
	target_link_libraries(quickshell-core PRIVATE PkgConfig::libdrm)
endif()

qt_add_qml_module(quickshell-core
	URI Quickshell
	VERSION 0.1
	DEPENDENCIES QtQuick
	OPTIONAL_IMPORTS Quickshell._Window
	DEFAULT_IMPORTS Quickshell._Window
)

install_qml_module(quickshell-core)

target_link_libraries(quickshell-core PRIVATE Qt::Quick Qt::QuickPrivate Qt::Widgets Qt::Network quickshell-build)

qs_module_pch(quickshell-core SET large)

target_link_libraries(quickshell PRIVATE quickshell-coreplugin)

if (BUILD_TESTING)
	add_subdirectory(test)
endif()
