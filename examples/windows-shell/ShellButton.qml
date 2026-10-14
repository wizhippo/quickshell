import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    property color accent: Theme.accent
    property color foreground: Theme.foreground
    property string hint: ""
    property bool tooltipAbove: false
    ToolTip {
        parent: control
        popupType: Popup.Window
        visible: control.hovered && control.hint !== ""
        delay: 500
        timeout: 4000
        x: 0
        y: control.tooltipAbove ? -implicitHeight - 8 : control.height + 8
        width: Math.min(360, implicitWidth)
        text: control.hint
        font: control.font
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
        }
        contentItem: Text {
            text: control.hint
            font: control.font
            color: Theme.foreground
            wrapMode: Text.WordWrap
        }
    }
    property bool selected: false
    implicitHeight: 30
    leftPadding: 12
    rightPadding: 12
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: 13
    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.muted : control.down ? Theme.background : control.selected ? control.accent : control.foreground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 0
        color: control.down ? control.accent : control.selected || control.hovered ? Theme.selection : control.flat ? "transparent" : Theme.surface
        border.color: control.activeFocus ? control.accent : control.flat ? "transparent" : Theme.border
        Behavior on color {
            ColorAnimation {
                duration: 100
            }
        }
        Rectangle {
            anchors {
                bottom: parent.bottom
                left: parent.left
                right: parent.right
            }
            height: 2
            visible: control.flat && control.selected
            color: control.accent
        }
    }
}
