import QtQuick
import Quickshell
import Quickshell.Windows

// Run with quickshell -p src/windows/test/manual/shell-example.qml --quit-after 15000.
// Exercises the actual example without launching apps or changing volume/power.
ShellRoot {
    id: test
    property var desktop: null
    property int step: 0
    property var panels: ["apps", "audio", "power", "calendar", "control", "notifications", "tray", "wallpaper", ""]
    property bool originalBottom: false
    Component.onCompleted: {
        console.log("LAUNCHER SHORTCUT: " + LauncherShortcut.shortcutText + " " + LauncherShortcut.errorString);
        const component = Qt.createComponent("file:///" + Quickshell.shellPath("../../../../examples/windows-shell/shell.qml").replace(/\\/g, "/"));
        if (component.status !== Component.Ready) {
            console.error("SHELL EXAMPLE FAIL: " + component.errorString());
            Qt.quit();
            return;
        }
        desktop = component.createObject(test);
        originalBottom = desktop.preferences.bottomBar;
        desktop.activeScreen = Quickshell.screens[0];
    }
    Timer {
        interval: 1200
        running: test.desktop !== null
        repeat: true
        onTriggered: {
            if (test.step < test.panels.length) {
                test.desktop.activePanel = test.panels[test.step];
                console.log("SHELL EXAMPLE PANEL: " + test.panels[test.step]);
                if (test.panels[test.step] === "notifications")
                    console.log("NOTIFICATION HISTORY: available=" + NotificationHistory.available
                        + " count=" + NotificationHistory.notifications.values.length
                        + " error=" + NotificationHistory.errorString);
            } else if (test.step === test.panels.length) {
                test.desktop.preferences.bottomBar = !test.originalBottom;
                test.desktop.services.showOsd("Shell preview");
            } else if (test.step === test.panels.length + 1) {
                test.desktop.preferences.bottomBar = test.originalBottom;
                test.desktop.services.refreshApps();
            } else if (test.step >= test.panels.length + 3 && !test.desktop.services.loading) {
                if (test.desktop.services.appError !== "" || test.desktop.services.applications.length === 0)
                    console.error("SHELL EXAMPLE FAIL: " + test.desktop.services.appError);
                else
                    console.log("SHELL EXAMPLE PASS: " + test.desktop.services.applications.length + " applications");
                Qt.quit();
            }
            test.step++;
        }
    }
}
