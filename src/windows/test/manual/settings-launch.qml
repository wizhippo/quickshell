import QtQuick
import Quickshell
import Quickshell.Windows

// Opens real Settings pages with Explorer present or already absent.
// This test does not stop or start Explorer or change settings.
ShellRoot {
    id: test
    property var pages: ["network-wifi", "powersleep"]
    property int nextPage: 0
    property int ticks: 0
    property bool failed: false
    function launchNext() {
        if (nextPage >= pages.length) {
            console.log(failed ? "SETTINGS LAUNCH FAIL" : "SETTINGS LAUNCH PASS");
            Qt.quit();
            return;
        }
        System.openSettings(pages[nextPage++]);
    }
    Component.onCompleted: Qt.callLater(launchNext)
    Connections {
        target: System
        function onOpenFinished(success, target) {
            console.log("SETTINGS LAUNCH: " + target + " success=" + success + " GUI ticks=" + test.ticks);
            test.failed = test.failed || !success;
            nextLaunch.restart();
        }
    }
    Timer {
        id: nextLaunch
        interval: 1500
        onTriggered: test.launchNext()
    }
    Timer {
        interval: 100
        repeat: true
        running: true
        onTriggered: test.ticks++
    }
    Timer {
        interval: 20000
        running: true
        onTriggered: {
            console.error("SETTINGS LAUNCH FAIL: activation did not complete; GUI ticks=" + test.ticks);
            Qt.quit();
        }
    }
}
