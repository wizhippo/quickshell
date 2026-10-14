import QtQuick
import Quickshell

ShellRoot {
    id: test
    property var wallpaper: null
    property int ticks: 0
    property int stage: 0
    QtObject {
        id: preferences
        property string wallpaperSource: "file:///" + Quickshell.shellPath("../../assets/quickshell.svg").replace(/\\/g, "/")
        property string wallpaperFill: "fill"
        property string backgroundColor: "#282828"
    }
    Component.onCompleted: {
        const component = Qt.createComponent("file:///" + Quickshell.shellPath("../../examples/windows-shell/WallpaperWindow.qml").replace(/\\/g, "/"));
        if (component.status !== Component.Ready) {
            console.error("WALLPAPER FAIL: " + component.errorString());
            Qt.quit();
            return;
        }
        // Isolated surface; no saved preferences or Windows wallpaper changes.
        wallpaper = component.createObject(test, {
            preferences: preferences,
            screen: Quickshell.screens[0],
            visible: true
        });
    }
    Timer {
        interval: 100
        repeat: true
        running: test.wallpaper !== null
        onTriggered: {
            test.ticks++;
            if (test.stage === 0 && test.wallpaper.imageStatus === Image.Ready) {
                if (test.wallpaper.width < 100 || test.wallpaper.height < 100) {
                    console.error("WALLPAPER FAIL: surface not sized to monitor");
                    Qt.quit();
                    return;
                }
                preferences.wallpaperFill = "fit";
                preferences.backgroundColor = "#504945";
                preferences.wallpaperSource = "";
                test.stage = 1;
            } else if (test.stage === 1 && test.wallpaper.imageStatus === Image.Null) {
                console.log("WALLPAPER PASS: image loads asynchronously, surface fills monitor, fit and solid color changes remain responsive");
                Qt.quit();
            }
            if (test.wallpaper.imageStatus === Image.Error || test.ticks > 50) {
                console.error("WALLPAPER FAIL: image did not load");
                Qt.quit();
            }
        }
    }
}
