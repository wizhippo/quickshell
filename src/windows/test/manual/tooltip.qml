import QtQuick
import Quickshell

ShellRoot {
    property bool bottom: Quickshell.env("QS_TOOLTIP_TEST_BOTTOM") === "1"
    PanelWindow {
        id: bar
        anchors {
            top: !bottom
            bottom: bottom
            left: true
            right: true
        }
        exclusionMode: ExclusionMode.Ignore
        implicitHeight: 34
        color: "#282828"
        focusable: false
    }
    Component.onCompleted: {
        const component = Qt.createComponent("file:///" + Quickshell.shellPath("../../examples/windows-shell/ShellButton.qml").replace(/\\/g, "/"));
        if (component.status !== Component.Ready) {
            console.error(component.errorString());
            Qt.quit();
            return;
        }
        component.createObject(bar.contentItem, {
            x: 10,
            y: 2,
            width: 42,
            height: 30,
            text: "◈",
            hint: "Applications",
            tooltipAbove: bottom
        });
    }
}
