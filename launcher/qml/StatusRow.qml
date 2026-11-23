// One check: a glowing dot (ok / problem / working), a title, a detail line and optional extras.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: row
    property string title
    property string detail
    property string state_: "ok"   // ok | problem | working | idle
    default property alias extra: extras.data
    radius: 22
    color: Qt.rgba(1, 1, 1, 0.07)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.14)
    implicitHeight: content.implicitHeight + 24

    readonly property color dotColor: state_ === "ok" ? "#8ae05a" : state_ === "problem" ? "#ff8a5c"
                                     : state_ === "working" ? "#ffe24a" : "#9fb39a"

    RowLayout {
        id: content
        anchors { fill: parent; margins: 12; leftMargin: 16 }
        spacing: 14
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 4
            width: 14; height: 14; radius: 7
            color: row.dotColor
            Rectangle {  // glow
                anchors.centerIn: parent
                width: 26; height: 26; radius: 13
                color: row.dotColor; opacity: 0.25
                SequentialAnimation on scale {
                    running: row.state_ === "working"; loops: Animation.Infinite
                    NumberAnimation { to: 1.4; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutSine }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text { text: row.title; color: "white"; font.pixelSize: 15; font.bold: true }
            Text {
                Layout.fillWidth: true
                text: row.detail; color: Qt.rgba(1, 1, 1, 0.78); font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            ColumnLayout { id: extras; Layout.fillWidth: true; spacing: 6 }
        }
    }
}
