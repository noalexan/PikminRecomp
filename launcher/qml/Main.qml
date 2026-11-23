import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 980; height: 640
    minimumWidth: 820; minimumHeight: 560
    visible: true
    title: "Pikmin Recomp"
    color: "#0a160a"

    property bool showLog: false
    readonly property bool canBuild: launcher.toolchainOk && launcher.romOk && !launcher.busy
    readonly property bool playable: launcher.gameState === "built" && !launcher.busy

    ForestBackground { id: forest; anchors.fill: parent }

    FileDialog {
        id: isoDialog
        title: "Choose your Pikmin disc image"
        nameFilters: ["GameCube disc images (*.iso *.gcm)", "All files (*)"]
        onAccepted: launcher.chooseIso(selectedFile)
    }

    // update pill
    GlassPanel {
        id: pill
        backdrop: forest
        anchors { top: parent.top; right: parent.right; margins: 24 }
        height: 38; width: pillText.implicitWidth + 36; radius: 19
        tint: launcher.updateAvailable ? 0.14 : 0.08
        Rectangle {
            anchors.fill: parent; radius: pill.radius; color: "transparent"
            border.width: 1.5; border.color: launcher.updateAvailable ? "#ffe24a" : "transparent"
        }
        Text {
            id: pillText
            anchors.centerIn: parent
            text: launcher.updateText + (launcher.updateAvailable ? "  ›  Update" : "")
            color: launcher.updateAvailable ? "#ffe24a" : Qt.rgba(1, 1, 1, 0.7)
            font.pixelSize: 13; font.bold: launcher.updateAvailable
        }
        MouseArea {
            anchors.fill: parent; enabled: launcher.updateAvailable && !launcher.busy
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: launcher.update()
        }
        SequentialAnimation on scale {
            running: launcher.updateAvailable; loops: Animation.Infinite
            NumberAnimation { to: 1.05; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
        }
    }

    GlassPanel {
        id: panel
        backdrop: forest
        radius: 36
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 28 }
        width: Math.min(parent.width * 0.66, 660)

        ColumnLayout {
            anchors { fill: parent; margins: 26 }
            spacing: 14

            ColumnLayout {
                spacing: 2
                Text {
                    text: "Pikmin Recomp"; color: "white"
                    font.pixelSize: 38; font.bold: true; font.letterSpacing: 0.5
                    style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
                }
                Text {
                    text: "Launcher " + launcher.version + "  ·  " + launcher.gameText
                    color: Qt.rgba(1, 1, 1, 0.7); font.pixelSize: 13
                }
            }

            StatusRow {
                Layout.fillWidth: true
                title: "Toolchain"
                detail: launcher.toolchainText || "Checking…"
                state_: launcher.toolchainText === "" ? "working" : launcher.toolchainOk ? "ok" : "problem"
                Rectangle {
                    visible: launcher.toolchainHint !== ""
                    Layout.fillWidth: true
                    implicitHeight: hint.implicitHeight + 16
                    radius: 14; color: Qt.rgba(0, 0, 0, 0.30)
                    Text {
                        id: hint
                        anchors { left: parent.left; right: copy.left; verticalCenter: parent.verticalCenter; margins: 8 }
                        text: launcher.toolchainHint; color: "#d9f5c4"
                        font.family: "monospace"; font.pixelSize: 12; wrapMode: Text.WrapAnywhere
                    }
                    PillButton {
                        id: copy
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter; margins: 6 }
                        kind: "glass"; text: "Copy"; fontSize: 12
                        onClicked: launcher.copyToClipboard(launcher.toolchainHint)
                    }
                }
                PillButton {
                    visible: !launcher.toolchainOk && launcher.toolchainText !== ""
                    kind: "glass"; text: "Check again"; fontSize: 12
                    onClicked: launcher.refresh()
                }
            }

            StatusRow {
                Layout.fillWidth: true
                title: "Your disc"
                detail: launcher.romText + (launcher.isoPath ? "\n" + launcher.isoPath : "")
                state_: launcher.romOk ? "ok" : launcher.isoPath ? "problem" : "idle"
                PillButton {
                    kind: "glass"; text: launcher.isoPath ? "Choose another…" : "Choose disc image…"; fontSize: 12
                    enabled_: !launcher.busy
                    onClicked: isoDialog.open()
                }
            }

            StatusRow {
                Layout.fillWidth: true
                title: "Game"
                detail: launcher.busy ? launcher.stepText : launcher.stepText !== "" ? launcher.stepText + "\n" + launcher.gameText
                                                                                    : launcher.gameText
                state_: launcher.busy ? "working" : launcher.gameState === "built" ? "ok"
                        : launcher.stepText !== "" && launcher.gameState !== "built" ? "problem" : "idle"
                Rectangle {  // progress
                    visible: launcher.busy
                    Layout.fillWidth: true
                    height: 12; radius: 6; color: Qt.rgba(0, 0, 0, 0.30)
                    Rectangle {
                        width: parent.width * launcher.progress; height: parent.height; radius: 6
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "#f5d000" }
                            GradientStop { position: 1; color: "#fff27a" }
                        }
                        Behavior on width { NumberAnimation { duration: 250 } }
                    }
                }
                Text {
                    text: win.showLog ? "Hide log ▴" : "Show log ▾"
                    visible: launcher.log !== ""
                    color: "#ffe24a"; font.pixelSize: 12
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.showLog = !win.showLog }
                }
            }

            Item { Layout.fillHeight: true; visible: !win.showLog }
            ScrollView {
                visible: win.showLog
                Layout.fillWidth: true; Layout.fillHeight: true
                TextArea {
                    readOnly: true; text: win.showLog ? launcher.log : ""; wrapMode: TextArea.WrapAnywhere
                    color: "#d9f5c4"; font.family: "monospace"; font.pixelSize: 11
                    background: Rectangle { color: Qt.rgba(0, 0, 0, 0.35); radius: 18 }
                    onTextChanged: cursorPosition = length
                }
            }
        }
    }

    PillButton {
        anchors { right: parent.right; bottom: parent.bottom; margins: 34 }
        fontSize: 24
        width: 220
        kind: launcher.busy ? "red" : "yellow"
        text: launcher.busy ? "CANCEL" : win.playable ? "PLAY" : launcher.gameState === "outdated" ? "UPDATE GAME" : "BUILD"
        enabled_: launcher.busy || win.playable || win.canBuild
        onClicked: launcher.busy ? launcher.cancel() : win.playable ? launcher.play() : launcher.build()
    }
}
