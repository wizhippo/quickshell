import QtQuick
import Quickshell
import Quickshell.Windows

ShellRoot {
    SystemClock {
        id: clock
        precision: SystemClock.Seconds
    }

    PanelWindow {
        id: panel
        anchors { top: true; left: true; right: true }
        implicitHeight: 40
        color: "#182433"
        focusable: true
        onClosed: Qt.quit()

        Text {
            anchors.centerIn: parent
            text: "Quickshell on Windows"
            color: "#f0f5fa"
            font.pixelSize: 16
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: 13
            color: PowerStatus.available
                   && PowerStatus.acStatus === PowerStatus.AcOffline
                   && PowerStatus.batteryPercent >= 0 && PowerStatus.batteryPercent <= 20
                   ? "#ffb86c" : "#9fb3c8"
            text: {
                if (!PowerStatus.available) return "Power unavailable"
                const ac = PowerStatus.acStatus === PowerStatus.AcOnline ? "AC power"
                         : PowerStatus.acStatus === PowerStatus.AcOffline ? "On battery"
                         : "Power source unknown"
                if (PowerStatus.batteryPresence === PowerStatus.NoBattery)
                    return PowerStatus.acStatus === PowerStatus.AcOffline ? "No battery" : ac
                if (PowerStatus.batteryPresence === PowerStatus.BatteryUnknown)
                    return ac + " | Battery unknown"
                const charge = PowerStatus.batteryPercent >= 0
                             ? PowerStatus.batteryPercent + "%" : "Charge unknown"
                const state = PowerStatus.charging ? "Charging" : ac
                return charge + " | " + state
                     + (PowerStatus.batterySaverEnabled ? " | Saver" : "")
            }
        }

        Row {
            anchors.right: clockDisplay.left
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Rectangle {
                width: 28
                height: 26
                radius: 4
                color: "#24364a"
                opacity: Volume.available ? 1 : 0.5
                Text { anchors.centerIn: parent; text: "-"; color: "#f0f5fa" }
                MouseArea {
                    anchors.fill: parent
                    enabled: Volume.available
                    onClicked: Volume.adjustVolume(-0.05)
                }
            }

            MouseArea {
                width: 110
                height: 26
                enabled: Volume.available
                onClicked: Volume.toggleMuted()
                onWheel: function(wheel) {
                    if (wheel.angleDelta.y !== 0) {
                        Volume.adjustVolume(wheel.angleDelta.y / 120 * 0.02)
                        wheel.accepted = true
                    } else wheel.accepted = false
                }
                Text {
                    anchors.centerIn: parent
                    text: !Volume.available ? "No audio output"
                        : (Volume.muted ? "Muted | " : "Volume ") + Volume.volumePercent + "%"
                    color: Volume.available && !Volume.muted ? "#f0f5fa" : "#9fb3c8"
                    font.pixelSize: 13
                }
            }

            Rectangle {
                width: 28
                height: 26
                radius: 4
                color: "#24364a"
                opacity: Volume.available ? 1 : 0.5
                Text { anchors.centerIn: parent; text: "+"; color: "#f0f5fa" }
                MouseArea {
                    anchors.fill: parent
                    enabled: Volume.available
                    onClicked: Volume.adjustVolume(0.05)
                }
            }
        }

        Column {
            id: clockDisplay
            anchors.right: exitButton.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                anchors.right: parent.right
                text: Qt.formatDateTime(clock.date, "HH:mm:ss")
                color: "#f0f5fa"
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Text {
                text: Qt.formatDateTime(clock.date, "ddd, dd MMM yyyy")
                color: "#9fb3c8"
                font.pixelSize: 11
            }
        }

        MouseArea {
            id: exitButton
            anchors.right: parent.right
            width: 60
            height: parent.height
            onClicked: Qt.quit()
            Text { anchors.centerIn: parent; text: "Exit"; color: "#45b6d9" }
        }
    }
}
