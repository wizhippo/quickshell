import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtCore
import QtQuick.Layouts
import Quickshell
import Quickshell.Networking
import Quickshell.Windows

FloatingWindow {
    id: panel
    required property var host
    property string pendingPowerAction: ""
    // Panels follow Omarchy: a frameless popup anchored under the bar, dismissed
    // by clicking elsewhere instead of carrying Windows' own chrome.
    readonly property var backingWindow: contentItem.Window.window
    readonly property int barHeight: 34
    property int margin: 8
    // Focus is requested after mapping, so the popup ignores earlier blur events.
    property bool activated: false
    visible: host.activePanel !== ""
    screen: host.activeScreen || Quickshell.screens[0]
    title: "Windows shell — " + host.activePanel
    implicitWidth: Math.min(480, (screen ? screen.width : 480) - margin * 2)
    implicitHeight: Math.min(610, Math.max(240, (screen ? screen.height : 610) - barHeight - margin * 2))
    color: Theme.background
    onClosed: host.activePanel = ""

    // Quickshell does not expose window flags, so the decoration is replaced
    // once the backing window exists. Qt.Tool keeps the popup out of the
    // taskbar and Alt+Tab, matching Omarchy's shell-level surfaces.
    function undecorate() {
        const window = backingWindow || contentItem.Window.window;
        if (window)
            window.flags = Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint;
    }

    function place() {
        const window = backingWindow;
        const target = screen;
        if (!window || !target)
            return;
        const left = target.x + margin;
        const top = target.y + margin;
        const centered = target.x + Math.round((target.width - implicitWidth) / 2);
        const belowBar = target.y + barHeight + margin;
        window.x = Math.max(left, Math.min(centered, target.x + target.width - implicitWidth - margin));
        window.y = host.preferences.bottomBar ? top : Math.max(top, Math.min(belowBar, target.y + target.height - implicitHeight - margin));
    }

    Connections {
        target: panel
        function onWindowConnected() {
            panel.undecorate();
        }
    }

    Connections {
        target: panel.backingWindow
        function onActiveChanged() {
            if (!panel.backingWindow)
                return;
            if (panel.backingWindow.active)
                panel.activated = true;
            else if (panel.activated) {
                panel.activated = false;
                panel.host.activePanel = "";
            }
        }
    }

    FileDialog {
        id: wallpaperPicker
        title: "Choose wallpaper"
        parentWindow: panel.contentItem.Window.window
        currentFolder: StandardPaths.standardLocations(StandardPaths.PicturesLocation)[0]
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        options: FileDialog.DontUseNativeDialog | FileDialog.ReadOnly
        onAccepted: host.preferences.wallpaperSource = selectedFile.toString()
    }
    onBackingWindowVisibleChanged: {
        if (backingWindowVisible)
            Qt.callLater(function () {
                panel.undecorate();
                panel.place();
                const window = panel.contentItem.Window.window;
                if (window)
                    window.requestActivate();
                if (host.activePanel === "apps")
                    search.forceActiveFocus();
            });
    }
    onVisibleChanged: {
        pendingPowerAction = "";
        if (visible) {
            activated = false;
            undecorate();
            place();
            search.forceActiveFocus();
        } else if (host.activePanel !== "") {
            // Whatever closed the popup, the bar's selected underline must not
            // outlive it.
            host.activePanel = "";
        }
    }

    component Label: Text {
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 14
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component Action: ShellButton {
        accent: panel.host.preferences.accent
        Layout.fillWidth: true
        implicitHeight: 38
    }
    // Caption/value row, matching Omarchy's panel stat rows.
    component Field: RowLayout {
        property string caption
        property string content
        property color contentColor: Theme.foreground
        spacing: 10
        Text {
            text: parent.caption
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: 13
            Layout.preferredWidth: 116
        }
        Text {
            text: parent.content === "" ? "—" : parent.content
            color: parent.contentColor
            font.family: Theme.fontFamily
            font.pixelSize: 13
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
    Control {
        anchors.fill: parent
        font.family: Theme.fontFamily
        palette {
            window: Theme.background
            windowText: Theme.foreground
            base: Theme.darkBackground
            alternateBase: Theme.surface
            text: Theme.foreground
            button: Theme.surface
            buttonText: Theme.foreground
            highlight: Theme.accent
            highlightedText: Theme.background
            light: Theme.border
            mid: Theme.border
            dark: Theme.darkBackground
            placeholderText: Theme.muted
        }
        focus: true
        Keys.onEscapePressed: host.activePanel = ""
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: ({
                            apps: "Applications",
                            network: "Network",
                            audio: "Audio",
                            power: "Power & session",
                            calendar: "Calendar",
                            control: "Control center",
                            notifications: "Notification history",
                            tray: "System tray",
                            wallpaper: "Wallpaper & background"
                        })[host.activePanel] || "Windows shell"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                ShellButton {
                    text: "Close"
                    onClicked: host.activePanel = ""
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Label {
                        visible: host.services.settingsBusy || host.services.settingsError !== ""
                        text: host.services.settingsBusy ? "Opening Windows settings…" : host.services.settingsError
                        color: host.services.settingsBusy ? Theme.secondary : Theme.yellow
                    }
                    ColumnLayout {
                        visible: host.activePanel === "apps"
                        Layout.fillWidth: true
                        TextField {
                            id: search
                            color: Theme.foreground
                            background: Rectangle {
                                color: Theme.darkBackground
                                border.color: search.activeFocus ? Theme.accent : Theme.border
                            }
                            Layout.fillWidth: true
                            placeholderText: "Search installed applications…"
                            selectByMouse: true
                            onAccepted: {
                                if (appList.count > 0) {
                                    host.services.launch(appList.model[Math.max(0, appList.currentIndex)]);
                                    host.activePanel = "";
                                }
                            }
                            Keys.onDownPressed: appList.currentIndex = Math.min(appList.count - 1, appList.currentIndex + 1)
                            Keys.onUpPressed: appList.currentIndex = Math.max(0, appList.currentIndex - 1)
                            Keys.onEscapePressed: host.activePanel = ""
                        }
                        Label {
                            text: LauncherShortcut.available ? "Shortcut: " + LauncherShortcut.shortcutText : "Shortcut unavailable"
                            color: Theme.secondary
                        }
                        Label {
                            text: LauncherShortcut.closeShortcutText ? "Close window: " + LauncherShortcut.closeShortcutText : "Close shortcut unavailable"
                            color: LauncherShortcut.closeShortcutText === "Win+W" ? Theme.secondary : Theme.yellow
                        }
                        Label {
                            visible: LauncherShortcut.errorString !== ""
                            text: LauncherShortcut.errorString
                            color: Theme.yellow
                        }
                        Label {
                            visible: host.services.loading || host.services.appError !== ""
                            text: host.services.loading ? "Reading Windows Start applications…" : host.services.appError
                        }
                        ListView {
                            id: appList
                            Layout.fillWidth: true
                            Layout.preferredHeight: 330
                            clip: true
                            spacing: 4
                            model: host.services.applications.filter(app => app.name.toLowerCase().includes(search.text.toLowerCase()))
                            currentIndex: 0
                            onModelChanged: currentIndex = 0
                            delegate: ShellButton {
                                required property var modelData
                                required property int index
                                width: ListView.view.width
                                text: modelData.name
                                selected: appList.currentIndex === index
                                onClicked: {
                                    host.services.launch(modelData);
                                    host.activePanel = "";
                                }
                            }
                            ScrollBar.vertical: ScrollBar {}
                        }
                        Label {
                            visible: !host.services.loading && appList.count === 0
                            text: "No matching applications."
                        }
                        RowLayout {
                            Action {
                                text: "Refresh apps"
                                enabled: !host.services.loading
                                onClicked: host.services.refreshApps()
                            }
                            Action {
                                text: "Open Apps folder"
                                onClicked: Quickshell.execDetached(["explorer.exe", "shell:AppsFolder"])
                            }
                        }
                    }
                    ColumnLayout {
                        visible: host.activePanel === "notifications"
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: NotificationHistory.busy ? "Reading Windows notifications…" : NotificationHistory.errorString
                            visible: NotificationHistory.busy || NotificationHistory.errorString !== ""
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                        RowLayout {
                            Action {
                                text: "Allow notification access"
                                visible: !NotificationHistory.available
                                enabled: !NotificationHistory.busy
                                onClicked: NotificationHistory.requestAccess()
                            }
                            Action {
                                text: "Refresh"
                                enabled: !NotificationHistory.busy
                                onClicked: NotificationHistory.refresh()
                            }
                        }
                        Action {
                            text: "Notification privacy settings"
                            onClicked: host.services.settings("privacy-notifications")
                        }
                        Label {
                            text: "No notifications retained by Windows."
                            visible: NotificationHistory.available && NotificationHistory.notifications.values.length === 0
                        }
                        Repeater {
                            model: NotificationHistory.notifications
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.appName + " · " + Qt.formatDateTime(modelData.created, "ddd HH:mm")
                                    textFormat: Text.PlainText
                                    color: Theme.secondary
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.summary
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.body
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    visible: text !== ""
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 1
                                    color: Theme.border
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        id: networking
                        visible: host.activePanel === "network"
                        Layout.fillWidth: true

                        // Windows owns Wi-Fi, so this only reads what the OS
                        // reports through Quickshell.Networking; pairing and
                        // profiles stay in Windows settings.
                        readonly property var devices: Networking.devices.values
                        readonly property var device: {
                            if (networking.devices.length === 0)
                                return null;
                            // Prefer a connected wireless interface, then any
                            // connected one, so the panel leads with the link
                            // actually carrying traffic.
                            const connected = networking.devices.filter(d => d.connected);
                            const wireless = connected.filter(d => d.type === DeviceType.Wifi);
                            if (wireless.length > 0)
                                return wireless[0];
                            if (connected.length > 0)
                                return connected[0];
                            return networking.devices[0];
                        }
                        readonly property bool wired: networking.device !== null
                            && networking.device.type === DeviceType.Wired
                        // The backend keeps only the associated network, so the
                        // first entry is the one the interface is on.
                        readonly property var wifiNetwork: networking.wired
                            || networking.device === null
                            || networking.device.networks.values.length === 0
                            ? null
                            : networking.device.networks.values[0]
                        readonly property bool offline: networking.device === null
                            || !networking.device.connected
                        readonly property bool limited: Networking.connectivity === NetworkConnectivity.Limited
                        readonly property string statusText: networking.offline
                            ? "Disconnected"
                            : ConnectionState.toString(networking.device.state)
                        readonly property string securityText: networking.wifiNetwork === null
                            ? ""
                            : WifiSecurityType.toString(networking.wifiNetwork.security)
                        function linkText(mbps) {
                            return mbps > 0 ? mbps + " Mbps" : "";
                        }

                        // Omarchy leads with the connection name and a status
                        // line, then lists the link details underneath.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 14
                            Rectangle {
                                width: 18
                                height: 14
                                Layout.alignment: Qt.AlignVCenter
                                visible: !networking.wired
                                color: "transparent"
                                Item {
                                    anchors.fill: parent
                                    Repeater {
                                        model: 4
                                        delegate: Rectangle {
                                            required property int index
                                            width: 3
                                            height: 3 + index * 3.5
                                            color: networking.offline ? Theme.muted
                                                : networking.limited ? Theme.yellow
                                                : Theme.foreground
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.horizontalCenterOffset: -7.5 + index * 5
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                width: 14
                                height: 10
                                Layout.alignment: Qt.AlignVCenter
                                visible: networking.wired
                                color: "transparent"
                                border.color: networking.offline ? Theme.muted : Theme.foreground
                                border.width: 1.5
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: networking.offline
                                        ? "Disconnected"
                                        : networking.wifiNetwork !== null ? networking.wifiNetwork.name : "Ethernet"
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 18
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        if (networking.offline)
                                            return "No connection";
                                        const parts = [];
                                        if (networking.limited)
                                            parts.push("Local network only");
                                        else
                                            parts.push(networking.statusText);
                                        if (networking.wifiNetwork !== null && networking.wifiNetwork.band > 0)
                                            parts.push(networking.wifiNetwork.band + " MHz");
                                        if (!networking.wired && networking.wifiNetwork !== null
                                                && networking.wifiNetwork.signalStrength > 0) {
                                            parts.push(Math.round(
                                                networking.wifiNetwork.signalStrength * 100) + "% signal");
                                        }
                                        return parts.join(" · ");
                                    }
                                    color: networking.offline ? Theme.muted
                                        : networking.limited ? Theme.yellow : Theme.secondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        Label {
                            visible: Networking.backend === NetworkBackendType.None
                            text: "Quickshell.Networking has no backend on this system."
                            color: Theme.yellow
                        }
                        Label {
                            text: "Connection"
                            font.pixelSize: 18
                        }
                        Field {
                            caption: "Status"
                            content: networking.statusText
                            contentColor: networking.offline ? Theme.muted
                                : networking.limited ? Theme.yellow : Theme.green
                        }
                        Field {
                            caption: "Adapter"
                            content: networking.device === null ? "" : networking.device.name
                        }
                        Field {
                            caption: "MAC address"
                            content: networking.device === null ? "" : networking.device.address
                        }
                        Field {
                            caption: "Link speed"
                            content: networking.device === null
                                ? "" : networking.linkText(networking.device.linkSpeed)
                        }
                        Field {
                            caption: "IPv4 address"
                            content: networking.device === null
                                ? "" : networking.device.ipv4Addresses.join(", ")
                        }
                        Field {
                            caption: "Gateway"
                            content: networking.device === null
                                ? "" : networking.device.gateways.join(", ")
                        }
                        Field {
                            caption: "DNS"
                            content: networking.device === null
                                ? "" : networking.device.dnsServers.join(", ")
                        }
                        Field {
                            caption: "Security"
                            content: networking.securityText
                        }
                        Label {
                            text: "Windows pairs Wi-Fi networks and controls profiles. Use its own interfaces to change this connection."
                            color: Theme.secondary
                        }
                        RowLayout {
                            Action {
                                text: "Refresh"
                                onClicked: Networking.checkConnectivity()
                            }
                            Action {
                                text: "Wi-Fi settings"
                                onClicked: host.services.settings("network-wifi")
                            }
                            Action {
                                text: "All network settings"
                                onClicked: host.services.settings("network-status")
                            }
                        }
                    }
                    ColumnLayout {
                        visible: host.activePanel === "audio"
                        Layout.fillWidth: true
                        Label {
                            text: Volume.available ? Volume.deviceName || "Default playback device" : "No playback device available"
                            font.pixelSize: 18
                        }
                        Label {
                            text: Volume.errorString
                            visible: Volume.errorString !== ""
                        }
                        Label {
                            text: Volume.available ? (Volume.muted ? "Muted · " : "Volume · ") + Volume.volumePercent + "%" : "Connect an output device or open Windows sound settings."
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: Volume.available ? Volume.volume : 0
                            enabled: Volume.available
                            onMoved: {
                                Volume.volume = value;
                                host.services.showOsd("Volume " + Volume.volumePercent + "%");
                            }
                        }
                        Action {
                            text: Volume.muted ? "Unmute" : "Mute"
                            enabled: Volume.available
                            onClicked: Volume.toggleMuted()
                        }
                        Action {
                            text: "Output devices & microphone"
                            onClicked: host.services.settings("sound")
                        }
                        Action {
                            text: "Application volume mixer"
                            onClicked: host.services.settings("apps-volume")
                        }
                    }
                    ColumnLayout {
                        visible: host.activePanel === "power"
                        Layout.fillWidth: true
                        Label {
                            text: !PowerStatus.available ? "Power status unavailable" : PowerStatus.batteryPresence === PowerStatus.NoBattery ? "No system battery" : PowerStatus.batteryPresence === PowerStatus.BatteryUnknown ? "Battery status unknown" : (PowerStatus.batteryPercent >= 0 ? PowerStatus.batteryPercent + "% battery" : "Charge unknown")
                            font.pixelSize: 22
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: Math.max(0, PowerStatus.batteryPercent)
                            visible: PowerStatus.available && PowerStatus.batteryPercent >= 0
                        }
                        Label {
                            text: PowerStatus.charging ? "Charging" : PowerStatus.acStatus === PowerStatus.AcOnline ? "Connected to AC power" : PowerStatus.acStatus === PowerStatus.AcOffline ? "Running on battery" : "Power source unknown"
                            visible: PowerStatus.available
                        }
                        Label {
                            text: "Battery saver is enabled"
                            visible: PowerStatus.available && PowerStatus.batterySaverEnabled
                        }
                        Label {
                            text: "Estimated remaining: " + Math.floor(PowerStatus.remainingSeconds / 3600) + "h " + Math.floor(PowerStatus.remainingSeconds % 3600 / 60) + "m"
                            visible: PowerStatus.available && PowerStatus.remainingSeconds >= 0
                        }
                        Label {
                            text: PowerStatus.errorString
                            visible: PowerStatus.errorString !== ""
                        }
                        Action {
                            text: "Power & sleep settings"
                            onClicked: host.services.settings("powersleep")
                        }
                        Action {
                            text: "Lock Windows"
                            onClicked: {
                                host.services.lockSession();
                                host.activePanel = "";
                            }
                        }
                        RowLayout {
                            Action {
                                text: "Restart…"
                                onClicked: panel.pendingPowerAction = "restart"
                            }
                            Action {
                                text: "Shut down…"
                                onClicked: panel.pendingPowerAction = "shutdown"
                            }
                        }
                        Label {
                            visible: panel.pendingPowerAction !== ""
                            text: "Save your work. Confirm " + panel.pendingPowerAction + "?"
                        }
                        RowLayout {
                            visible: panel.pendingPowerAction !== ""
                            Action {
                                text: "Confirm"
                                onClicked: {
                                    Quickshell.execDetached(["shutdown.exe", panel.pendingPowerAction === "restart" ? "/r" : "/s", "/t", "0"]);
                                    panel.pendingPowerAction = "";
                                    host.activePanel = "";
                                }
                            }
                            Action {
                                text: "Cancel"
                                onClicked: panel.pendingPowerAction = ""
                            }
                        }
                        Action {
                            text: "Exit Quickshell"
                            onClicked: Qt.quit()
                        }
                    }
                    ColumnLayout {
                        visible: host.activePanel === "tray"
                        Layout.fillWidth: true
                        Label {
                            text: TrayHost.available ? "Hosted by Quickshell" : TrayHost.errorString
                            color: TrayHost.available ? Theme.secondary : Theme.yellow
                        }
                        Label {
                            visible: trayIcons.count === 0
                            text: "No application icons yet. Applications that handle shell restarts will re-register; others may need to be restarted."
                        }
                        Repeater {
                            id: trayIcons
                            model: TrayHost.items
                            delegate: ShellButton {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 42
                                leftPadding: 44
                                text: (modelData.tooltip || "Application") + (modelData.hidden ? " · hidden" : "")
                                Image {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 24
                                    height: 24
                                    source: modelData.iconSource
                                    sourceSize: Qt.size(32, 32)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: function (event) {
                                        modelData.activate(event.button === Qt.RightButton);
                                    }
                                    onDoubleClicked: function (event) {
                                        modelData.activate(event.button === Qt.RightButton, true);
                                    }
                                }
                            }
                        }
                        Action {
                            text: "Retry tray host"
                            visible: !TrayHost.available
                            onClicked: TrayHost.start()
                        }
                    }
                    ColumnLayout {
                        id: calendar
                        property date month: new Date(host.clock.date.getFullYear(), host.clock.date.getMonth(), 1)
                        visible: host.activePanel === "calendar"
                        Layout.fillWidth: true
                        Label {
                            text: Qt.formatDateTime(host.clock.date, "dddd, d MMMM yyyy")
                            font.pixelSize: 18
                        }
                        RowLayout {
                            ShellButton {
                                text: "‹"
                                onClicked: calendar.month = new Date(calendar.month.getFullYear(), calendar.month.getMonth() - 1, 1)
                            }
                            Label {
                                text: Qt.formatDateTime(calendar.month, "MMMM yyyy")
                                horizontalAlignment: Text.AlignHCenter
                            }
                            ShellButton {
                                text: "›"
                                onClicked: calendar.month = new Date(calendar.month.getFullYear(), calendar.month.getMonth() + 1, 1)
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 7
                            Repeater {
                                model: ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"]
                                Label {
                                    required property string modelData
                                    text: modelData
                                    horizontalAlignment: Text.AlignHCenter
                                    color: Theme.secondary
                                }
                            }
                            Repeater {
                                model: 42
                                Rectangle {
                                    required property int index
                                    property date day: new Date(calendar.month.getFullYear(), calendar.month.getMonth(), index - (calendar.month.getDay() + 6) % 7 + 1)
                                    property bool today: day.toDateString() === host.clock.date.toDateString()
                                    Layout.fillWidth: true
                                    implicitHeight: 38
                                    radius: 0
                                    color: today ? host.preferences.accent : Theme.surface
                                    Text {
                                        font.family: Theme.fontFamily
                                        anchors.centerIn: parent
                                        text: parent.day.getDate()
                                        color: parent.today ? Theme.background : parent.day.getMonth() === calendar.month.getMonth() ? Theme.foreground : Theme.muted
                                    }
                                }
                            }
                        }
                        Action {
                            text: "Today"
                            onClicked: calendar.month = new Date(host.clock.date.getFullYear(), host.clock.date.getMonth(), 1)
                        }
                        Action {
                            text: "Date & time settings"
                            onClicked: host.services.settings("dateandtime")
                        }
                    }
                    ColumnLayout {
                        visible: host.activePanel === "wallpaper"
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 170
                            color: host.preferences.backgroundColor
                            clip: true
                            border.color: Theme.border
                            Image {
                                id: wallpaperPreview
                                anchors.fill: parent
                                anchors.margins: 1
                                source: host.preferences.wallpaperSource
                                asynchronous: true
                                cache: false
                                sourceSize: Qt.size(480, 240)
                                fillMode: host.preferences.wallpaperFill === "fit" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                            }
                        }
                        Label {
                            text: host.preferences.wallpaperSource === "" ? "Solid color background" : host.preferences.wallpaperSource.replace(/^file:\/\/\//, "")
                            font.pixelSize: 12
                            color: Theme.secondary
                        }
                        Label {
                            visible: wallpaperPreview.status === Image.Error
                            text: "This image could not be loaded. Choose another image or use a solid color."
                            color: Theme.yellow
                        }
                        Action {
                            text: "Choose image…"
                            onClicked: wallpaperPicker.open()
                        }
                        Action {
                            text: "Use solid color"
                            enabled: host.preferences.wallpaperSource !== ""
                            onClicked: host.preferences.wallpaperSource = ""
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Fill screen", "Fit inside screen"]
                            currentIndex: host.preferences.wallpaperFill === "fit" ? 1 : 0
                            onActivated: host.preferences.wallpaperFill = currentIndex === 1 ? "fit" : "fill"
                        }
                        Label {
                            text: "Background color"
                        }
                        RowLayout {
                            Repeater {
                                model: ["#282828", "#1e1e1e", "#3c3836", "#504945", "#7daea3", "#d8a657", "#a9b665"]
                                ShellButton {
                                    required property string modelData
                                    text: "●"
                                    foreground: modelData
                                    selected: host.preferences.backgroundColor === modelData
                                    hint: modelData
                                    onClicked: host.preferences.backgroundColor = modelData
                                }
                            }
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: host.preferences.backgroundColor
                            placeholderText: "#282828"
                            validator: RegularExpressionValidator {
                                regularExpression: /#[0-9a-fA-F]{6}/
                            }
                            onEditingFinished: if (acceptableInput)
                                host.preferences.backgroundColor = text
                        }
                        Label {
                            text: "Your background is saved automatically and shown on every monitor while Quickshell replaces Explorer."
                            color: Theme.secondary
                        }
                    }
                    ColumnLayout {
                        visible: host.activePanel === "control"
                        Layout.fillWidth: true
                        Label {
                            text: "Connections & desktop"
                            font.pixelSize: 18
                        }
                        Action {
                            text: "Calendar"
                            onClicked: host.activePanel = "calendar"
                        }
                        Action {
                            text: "Wi-Fi & network"
                            onClicked: host.activePanel = "network"
                        }
                        Action {
                            text: "Bluetooth & devices"
                            onClicked: host.services.settings("bluetooth")
                        }
                        Action {
                            text: "Displays, scaling & brightness"
                            onClicked: host.services.settings("display")
                        }
                        Action {
                            text: "Wallpaper & background"
                            onClicked: host.activePanel = "wallpaper"
                        }
                        Action {
                            text: "Notification history"
                            onClicked: {
                                host.activePanel = "notifications";
                                NotificationHistory.refresh();
                            }
                        }
                        Action {
                            text: "Windows notification settings"
                            onClicked: host.services.settings("notifications")
                        }
                        Action {
                            text: "System tray"
                            onClicked: host.activePanel = "tray"
                        }
                        Label {
                            visible: TrayHost.errorString !== ""
                            text: TrayHost.errorString
                            color: Theme.yellow
                        }
                        Action {
                            text: "Virtual desktops (Task View)"
                            onClicked: Quickshell.execDetached(["explorer.exe", "shell:::{3080F90E-D7AD-11D9-BD98-0000947B0257}"])
                        }
                        Label {
                            text: "Shell appearance"
                            font.pixelSize: 18
                        }
                        CheckBox {
                            text: "Place bar at the bottom"
                            checked: host.preferences.bottomBar
                            onToggled: host.preferences.bottomBar = checked
                        }
                        CheckBox {
                            text: "Show clock seconds"
                            checked: host.preferences.showSeconds
                            onToggled: host.preferences.showSeconds = checked
                        }
                        RowLayout {
                            Repeater {
                                model: ["#7daea3", "#a9b665", "#d3869b", "#d8a657", "#ea6962"]
                                ShellButton {
                                    required property string modelData
                                    text: "●"
                                    accent: modelData
                                    selected: host.preferences.accent === modelData
                                    foreground: modelData
                                    onClicked: host.preferences.accent = modelData
                                }
                            }
                        }
                        Label {
                            text: "Windows manages connections, notifications and virtual desktops. Use the controls above to open its native interfaces."
                            color: Theme.secondary
                        }
                        Action {
                            text: "Power & session…"
                            onClicked: host.activePanel = "power"
                        }
                    }
                }
            }
        }
    }
    // Without a native frame there is nothing to draw the edge, so the theme's
    // border is rendered in the panel itself like Omarchy's popup surface.
    Rectangle {
        anchors.fill: parent
        // Half-pixel inset keeps the stroke inside the window instead of
        // clipping it against the edge.
        anchors.margins: 0.5
        color: "transparent"
        border.color: Theme.border
        border.width: 1
    }
}
