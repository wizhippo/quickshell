function (qs_test name)
	add_executable(${name} ${ARGN})
	target_link_libraries(${name} PRIVATE Qt::Quick Qt::Test quickshell-window quickshell-core quickshell-ui quickshell-io)
	add_test(NAME ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" COMMAND $<TARGET_FILE:${name}>)
endfunction()

# ProxyPopupWindow has no Windows backing window when instantiated directly:
# the platform overlay registers PopupWindow as a QML type instead, and that
# path is covered by the desktop smoke tests rather than here.
if (NOT WIN32)
	qs_test(popupwindow popupwindow.cpp)
endif()

qs_test(windowattached windowattached.cpp)
