import QtQuick
import QtQuick.Window
import Quickshell
import Quickshell.Windows

PanelWindow {
    id: wallpaperWindow
    required property var preferences
    readonly property int imageStatus: wallpaper.status
    anchors {
        top: true
        bottom: true
        left: true
        right: true
    }
    exclusionMode: ExclusionMode.Ignore
    aboveWindows: false
    focusable: false
    visible: TrayHost.available
    color: preferences.backgroundColor
    Image {
        id: wallpaper
        anchors.fill: parent
        source: wallpaperWindow.preferences.wallpaperSource
        asynchronous: true
        cache: false
        fillMode: wallpaperWindow.preferences.wallpaperFill === "fit" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        sourceSize: Qt.size(Math.max(1, Math.ceil(width * Screen.devicePixelRatio)), Math.max(1, Math.ceil(height * Screen.devicePixelRatio)))
    }
}
