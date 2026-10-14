import QtQuick
import Quickshell
import Quickshell.Windows

Scope {
    id: service
    readonly property var applications: DesktopEntries.applications.values.slice().sort((a, b) => a.name.localeCompare(b.name) || a.id.localeCompare(b.id))
    readonly property string appError: DesktopEntries.errorString
    readonly property bool loading: DesktopEntries.loading
    property string osdText: ""
    property bool osdVisible: false
    // Opening a handler can stall behind a hung application, so the native
    // side does it off the GUI thread and reports back here. One request at a
    // time keeps a stuck handler from queueing more.
    property bool settingsBusy: false
    property string settingsError: ""
    function showOsd(text) {
        osdText = text;
        osdVisible = true;
        osdTimer.restart();
    }
    function settings(page) {
        if (settingsBusy)
            return;
        settingsError = "";
        settingsBusy = true;
        System.openSettings(page);
    }
    function launch(app) {
        app.execute();
    }
    function lockSession() {
        System.lockSession();
    }
    function refreshApps() {
        DesktopEntries.refresh();
    }
    Timer {
        id: osdTimer
        interval: 1800
        onTriggered: service.osdVisible = false
    }
    Connections {
        target: System
        function onOpenFinished(success, target) {
            service.settingsBusy = false;
            if (success || !target.startsWith("ms-settings:"))
                return;
            service.settingsError = "Windows settings could not be opened.";
            service.showOsd("Windows settings could not be opened");
        }
    }
}
