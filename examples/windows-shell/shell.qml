import QtQuick
import QtCore
import QtQuick.Shapes
import Quickshell
import Quickshell.Windows

ShellRoot {
    id: shell
    property string activePanel: ""
    property var activeScreen: null
    property alias preferences: preferences
    property alias services: services
    property alias clock: clock
    // The bar draws icons only, so the removed labels move into tooltips.
    readonly property string powerHint: !PowerStatus.available ? "Power status unavailable" : PowerStatus.batteryPresence === PowerStatus.NoBattery ? "No battery · " + (PowerStatus.acStatus === PowerStatus.AcOnline ? "Running on AC power" : "Running on battery") : PowerStatus.batteryPresence === PowerStatus.BatteryUnknown ? "Battery status unknown" : PowerStatus.batteryPercent < 0 ? "Charge unknown" : "Battery " + PowerStatus.batteryPercent + "%" + (PowerStatus.charging ? " · Charging" : PowerStatus.acStatus === PowerStatus.AcOffline ? " · Discharging" : "") + (PowerStatus.batteryPercent <= 20 && !PowerStatus.charging ? " · Low" : "")
    // The bar draws icons only, so these labels move into tooltips.
    readonly property string volumeHint: !Volume.available ? "No audio output" : Volume.muted ? "Muted · " + Volume.deviceName : Volume.deviceName + " · " + Volume.volumePercent + "%"
    component SpeakerIcon: Item {
        id: speaker
        property color tint: Theme.foreground
        property bool muted: false
        implicitWidth: 20
        implicitHeight: 18
        Shape {
            anchors.fill: parent
            antialiasing: true
            // Body and cone as one filled outline.
            ShapePath {
                fillColor: speaker.tint
                strokeColor: "transparent"
                startX: 3
                startY: 7
                PathLine {
                    x: 6
                    y: 7
                }
                PathLine {
                    x: 10
                    y: 3
                }
                PathLine {
                    x: 10
                    y: 15
                }
                PathLine {
                    x: 6
                    y: 11
                }
                PathLine {
                    x: 3
                    y: 11
                }
                PathLine {
                    x: 3
                    y: 7
                }
            }
            ShapePath {
                strokeColor: speaker.muted ? "transparent" : speaker.tint
                strokeWidth: 1.6
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 11.8
                startY: 5.6
                PathQuad {
                    x: 14
                    y: 9
                    controlX: 12.6
                    controlY: 7.1
                }
            }
            ShapePath {
                strokeColor: speaker.muted ? "transparent" : speaker.tint
                strokeWidth: 1.6
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 15
                startY: 2.8
                PathQuad {
                    x: 17.6
                    y: 9
                    controlX: 16
                    controlY: 5.2
                }
            }
            // Muted: cross out where the waves would be.
            ShapePath {
                strokeColor: speaker.muted ? speaker.tint : "transparent"
                strokeWidth: 1.6
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 12
                startY: 5
                PathLine {
                    x: 17
                    y: 13
                }
            }
            ShapePath {
                strokeColor: speaker.muted ? speaker.tint : "transparent"
                strokeWidth: 1.6
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 17
                startY: 5
                PathLine {
                    x: 12
                    y: 13
                }
            }
        }
    }
    Component.onCompleted: TrayHost.start()

    function togglePanel(name, screen) {
        activeScreen = screen;
        activePanel = activePanel === name ? "" : name;
    }
    Connections {
        target: LauncherShortcut
        function onCloseActivated() {
            TaskWindows.closeForeground();
        }
        function onActivated(screenName) {
            let targetScreen = Quickshell.screens[0];
            for (const screen of Quickshell.screens)
                if (screen.name === screenName)
                    targetScreen = screen;
            shell.togglePanel("apps", targetScreen);
        }
    }
    Settings {
        id: preferences
        location: "file:///" + String(Quickshell.env("LOCALAPPDATA")).replace(/\\/g, "/") + "/quickshell-windows-shell.ini"
        category: "windows-shell-example"
        property bool bottomBar: false
        property bool showSeconds: false
        property string accent: "#7daea3"
        property string wallpaperSource: ""
        property string wallpaperFill: "fill"
        property string backgroundColor: "#282828"
        property int themeVersion: 0
        onAccentChanged: Theme.accent = accent
        Component.onCompleted: {
            if (themeVersion < 1) {
                accent = "#7daea3";
                themeVersion = 1;
            }
            Theme.accent = accent;
        }
    }
    SystemClock {
        id: clock
        precision: SystemClock.Seconds
    }
    WindowsServices {
        id: services
    }
    Variants {
        model: Quickshell.screens
        WallpaperWindow {
            required property var modelData
            screen: modelData
            preferences: shell.preferences
        }
    }
    Variants {
        model: Quickshell.screens
        PanelWindow {
            id: bar
            required property var modelData
            screen: modelData
            anchors {
                top: !preferences.bottomBar
                bottom: preferences.bottomBar
                left: true
                right: true
            }
            implicitHeight: 34
            color: Theme.background
            focusable: false
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                ShellButton {
                    flat: true
                    text: "◈"
                    hint: "Applications"
                    tooltipAbove: preferences.bottomBar
                    Accessible.name: "Applications"
                    accent: preferences.accent
                    selected: shell.activePanel === "apps"
                    onClicked: shell.togglePanel("apps", bar.screen)
                }
                Flickable {
                    id: taskScroll
                    width: Math.max(0, Math.min(tasksRow.implicitWidth, bar.width / 2 - 90))
                    height: 30
                    contentWidth: tasksRow.implicitWidth
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    Row {
                        id: tasksRow
                        Repeater {
                            model: TaskWindows.groups
                            delegate: ShellButton {
                                required property var modelData
                                width: 32
                                flat: true
                                // One button per application, so the title names
                                // the app rather than any single window.
                                hint: modelData.windowCount > 1 ? modelData.title + " · " + modelData.windowCount + " windows" : modelData.title
                                tooltipAbove: preferences.bottomBar
                                selected: modelData.active
                                Accessible.name: hint
                                onClicked: modelData.activate()
                                Image {
                                    anchors.centerIn: parent
                                    width: 20
                                    height: 20
                                    source: modelData.iconSource
                                }
                                // Window count, drawn as a badge like Windows'
                                // own grouped buttons.
                                Text {
                                    visible: modelData.windowCount > 1
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 1
                                    text: modelData.windowCount
                                    color: parent.selected ? parent.accent : Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignRight
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onClicked: modelData.showContextMenu()
                                }
                            }
                        }
                    }
                    MouseArea {
                        // Flickable adopts bare children as content, so scroll input
                        // needs the viewport itself rather than the scrolled content.
                        parent: taskScroll
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        onWheel: function (event) {
                            const step = event.angleDelta.y || event.angleDelta.x;
                            parent.contentX = Math.max(0, Math.min(parent.contentWidth - parent.width, parent.contentX - step));
                            event.accepted = true;
                        }
                    }
                }
            }
            ShellButton {
                flat: true
                anchors.centerIn: parent
                visible: parent.width > 900
                text: Qt.formatDateTime(clock.date, preferences.showSeconds ? "dddd HH:mm:ss" : "dddd HH:mm")
                selected: shell.activePanel === "calendar"
                onClicked: shell.togglePanel("calendar", bar.screen)
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                ShellButton {
                    flat: true
                    visible: bar.width > 700
                    implicitWidth: 34
                    hint: "Network"
                    tooltipAbove: preferences.bottomBar
                    Accessible.name: "Network"
                    selected: shell.activePanel === "network"
                    onClicked: shell.togglePanel("network", bar.screen)
                    // Signal bars, drawn instead of a glyph so the bar never
                    // depends on a font shipping this codepoint.
                    Item {
                        id: networkBars
                        anchors.centerIn: parent
                        width: 18
                        height: 14
                        property color tint: parent.selected ? parent.accent : parent.foreground
                        Repeater {
                            model: 4
                            delegate: Rectangle {
                                required property int index
                                width: 3
                                height: 3 + index * 3.5
                                color: networkBars.tint
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.horizontalCenterOffset: -7.5 + index * 5
                            }
                        }
                    }
                }
                ShellButton {
                    flat: true
                    implicitWidth: 34
                    selected: shell.activePanel === "audio"
                    hint: shell.volumeHint
                    tooltipAbove: preferences.bottomBar
                    Accessible.name: "Audio"
                    onClicked: shell.togglePanel("audio", bar.screen)
                    SpeakerIcon {
                        anchors.centerIn: parent
                        muted: Volume.available && Volume.muted
                        tint: parent.selected ? parent.accent : Volume.available && Volume.muted ? Theme.red : Theme.foreground
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        onWheel: function (event) {
                            if (Volume.available && event.angleDelta.y) {
                                Volume.adjustVolume(event.angleDelta.y / 120 * 0.02);
                                services.showOsd("Volume " + Volume.volumePercent + "%");
                                event.accepted = true;
                            } else
                                event.accepted = false;
                        }
                    }
                }
                ShellButton {
                    flat: true
                    visible: bar.width > 580
                    implicitWidth: 34
                    selected: shell.activePanel === "power"
                    hint: powerHint
                    tooltipAbove: preferences.bottomBar
                    Accessible.name: "Power and session"
                    onClicked: shell.togglePanel("power", bar.screen)
                    // Omarchy's battery widget: an outlined cell whose fill
                    // tracks the level, with a bolt knocked out while charging.
                    Item {
                        id: batteryIcon
                        anchors.centerIn: parent
                        width: 22
                        height: 14
                        readonly property bool hasBattery: PowerStatus.available && PowerStatus.batteryPresence === PowerStatus.HasBattery
                        readonly property bool known: hasBattery && PowerStatus.batteryPercent >= 0
                        readonly property real level: known ? Math.max(0, Math.min(1, PowerStatus.batteryPercent / 100)) : 0
                        readonly property color tint: parent.selected ? parent.accent
                            : !known ? Theme.muted
                            : PowerStatus.charging ? Theme.green
                            : level <= 0.2 ? Theme.red
                            : Theme.foreground
                        Rectangle {
                            x: batteryIcon.width - 2
                            width: 2
                            height: 6
                            anchors.verticalCenter: parent.verticalCenter
                            color: batteryIcon.known ? batteryIcon.tint : "transparent"
                        }
                        Rectangle {
                            id: batteryBody
                            width: 18
                            height: 11
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 2
                            color: "transparent"
                            border.color: batteryIcon.known ? batteryIcon.tint : Theme.muted
                            border.width: 1.5
                            Rectangle {
                                x: 2
                                y: 2
                                height: parent.height - 4
                                width: Math.max(0, (parent.width - 4) * batteryIcon.level)
                                color: batteryIcon.tint
                            }
                            Rectangle {
                                visible: PowerStatus.charging && batteryIcon.hasBattery
                                width: 6
                                height: 1.6
                                rotation: 135
                                color: Theme.background
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.horizontalCenterOffset: 0.5
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.verticalCenterOffset: -1
                            }
                            Rectangle {
                                visible: PowerStatus.charging && batteryIcon.hasBattery
                                width: 3.6
                                height: 1.6
                                rotation: 45
                                color: Theme.background
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.horizontalCenterOffset: 0.5
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.verticalCenterOffset: 2.5
                            }
                        }
                    }
                }
                ShellButton {
                    flat: true
                    text: "☷"
                    hint: "Control center"
                    tooltipAbove: preferences.bottomBar
                    Accessible.name: "Control center"
                    selected: shell.activePanel === "control"
                    onClicked: shell.togglePanel("control", bar.screen)
                }
                ShellButton {
                    flat: true
                    implicitWidth: 34
                    selected: shell.activePanel === "tray"
                    hint: "System tray"
                    tooltipAbove: preferences.bottomBar
                    Accessible.name: "System tray"
                    onClicked: {
                        if (!TrayHost.available)
                            TrayHost.start();
                        shell.togglePanel("tray", bar.screen);
                    }
                    // The "^" overflow chevron Windows uses for hidden icons.
                    Item {
                        id: trayChevron
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        property color tint: parent.selected ? parent.accent : parent.foreground
                        Rectangle {
                            width: 7
                            height: 2
                            rotation: 135
                            color: trayChevron.tint
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.horizontalCenterOffset: -2.5
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.verticalCenterOffset: -1.5
                        }
                        Rectangle {
                            width: 7
                            height: 2
                            rotation: 45
                            color: trayChevron.tint
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.horizontalCenterOffset: 2.5
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.verticalCenterOffset: -1.5
                        }
                    }
                }
                Repeater {
                    model: TrayHost.items
                    delegate: ShellButton {
                        required property int index
                        required property var modelData
                        visible: index < 4 && !modelData.hidden
                        width: visible ? 30 : 0
                        flat: true
                        hint: modelData.tooltip
                        tooltipAbove: preferences.bottomBar
                        Image {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
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
            }
        }
    }
    ShellPanel {
        host: shell
    }
    PanelWindow {
        screen: shell.activeScreen || Quickshell.screens[0]
        anchors {
            bottom: true
            left: true
            right: true
        }
        margins {
            bottom: preferences.bottomBar ? 50 : 24
        }
        exclusionMode: ExclusionMode.Ignore
        implicitHeight: 54
        color: "transparent"
        visible: services.osdVisible
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(400, parent.width - 24)
            height: 48
            radius: 0
            color: Theme.background
            border.color: preferences.accent
            Text {
                anchors.centerIn: parent
                text: services.osdText
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 16
            }
        }
    }
}
