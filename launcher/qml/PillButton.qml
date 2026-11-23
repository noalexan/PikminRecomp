// The big rounded "Pikmin yellow" button with a pressed-down 3D edge; `kind` recolours it.
import QtQuick

Item {
    id: btn
    property string text
    property string kind: "yellow"   // yellow | red | glass
    property bool enabled_: true
    property real fontSize: 20
    signal clicked()
    implicitWidth: label.implicitWidth + 64
    implicitHeight: fontSize * 2.6

    readonly property color cTop: kind === "red" ? "#ff7a5c" : kind === "glass" ? Qt.rgba(1, 1, 1, 0.18) : "#ffe24a"
    readonly property color cBottom: kind === "red" ? "#d8442a" : kind === "glass" ? Qt.rgba(1, 1, 1, 0.08) : "#f0b400"
    readonly property color cEdge: kind === "red" ? "#8f2412" : kind === "glass" ? Qt.rgba(0, 0, 0, 0.3) : "#a37c00"
    readonly property color ink: kind === "glass" ? "white" : kind === "red" ? "white" : "#3a2a00"
    opacity: enabled_ ? 1 : 0.45

    Rectangle {  // the 3D edge
        anchors.fill: face; anchors.topMargin: 5
        radius: height / 2; color: btn.cEdge
    }
    Rectangle {
        id: face
        width: parent.width; height: parent.height - 5
        y: mouse.pressed && btn.enabled_ ? 4 : 0
        radius: height / 2
        border.color: btn.kind === "glass" ? Qt.rgba(1, 1, 1, 0.3) : "transparent"
        gradient: Gradient {
            GradientStop { position: 0; color: mouse.containsMouse && btn.enabled_ ? Qt.lighter(btn.cTop, 1.08) : btn.cTop }
            GradientStop { position: 1; color: btn.cBottom }
        }
        Behavior on y { NumberAnimation { duration: 60 } }
        Text {
            id: label
            anchors.centerIn: parent
            text: btn.text; color: btn.ink
            font.pixelSize: btn.fontSize; font.bold: true; font.letterSpacing: 1.5
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent; hoverEnabled: true
        cursorShape: btn.enabled_ ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (btn.enabled_) btn.clicked()
    }
}
