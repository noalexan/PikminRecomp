// Deep forest: a radial green glow, soft leaf silhouettes and drifting light motes. All drawn.
import QtQuick
import QtQuick.Shapes

Item {
    id: root
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: -1
            fillGradient: RadialGradient {
                centerX: root.width * 0.22; centerY: root.height * 0.08
                centerRadius: Math.max(root.width, root.height) * 1.1
                focalX: centerX; focalY: centerY
                GradientStop { position: 0.0; color: "#4a7d36" }
                GradientStop { position: 0.42; color: "#1d3a1a" }
                GradientStop { position: 1.0; color: "#0a160a" }
            }
            PathRectangle { x: 0; y: 0; width: root.width; height: root.height }
        }
    }

    // soft light pools (sun through the canopy) and Pikmin-coloured glows
    Repeater {
        model: [ { x: 0.20, y: 0.22, r: 0.34, c: "#a8e060", o: 0.45 }, { x: 0.48, y: 0.72, r: 0.26, c: "#ffd84a", o: 0.30 },
                 { x: 0.06, y: 0.88, r: 0.22, c: "#ff6a4a", o: 0.26 }, { x: 0.42, y: 0.08, r: 0.20, c: "#6ab0ff", o: 0.24 } ]
        Shape {
            required property var modelData
            readonly property real r: root.width * modelData.r
            x: root.width * modelData.x - r; y: root.height * modelData.y - r
            width: r * 2; height: r * 2
            opacity: modelData.o
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: r; centerY: r; centerRadius: r; focalX: r; focalY: r
                    GradientStop { position: 0.0; color: modelData.c }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                PathRectangle { x: 0; y: 0; width: r * 2; height: r * 2 }
            }
        }
    }

    component Leaf: Shape {
        property color tint: "#5f9a3a"
        width: 220; height: 110
        opacity: 0.30
        ShapePath {
            strokeWidth: -1
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: 220; y2: 110
                GradientStop { position: 0; color: Qt.lighter(tint, 1.25) }
                GradientStop { position: 1; color: Qt.darker(tint, 1.8) }
            }
            startX: 0; startY: 55
            PathQuad { x: 220; y: 55; controlX: 110; controlY: -45 }
            PathQuad { x: 0; y: 55; controlX: 110; controlY: 155 }
        }
        // the leaf's vein
        ShapePath {
            strokeColor: Qt.rgba(1, 1, 1, 0.25); strokeWidth: 2; fillColor: "transparent"
            startX: 10; startY: 55
            PathQuad { x: 210; y: 55; controlX: 110; controlY: 45 }
        }
    }

    Leaf { x: root.width - 260; y: -30; rotation: -28; tint: "#6aa641" }
    Leaf { x: root.width - 170; y: root.height * 0.38; rotation: 24; scale: 1.25; tint: "#4f8a32" }
    Leaf { x: -90; y: root.height - 120; rotation: 160; scale: 1.4; tint: "#3e7a2a"; opacity: 0.22 }
    Leaf { x: root.width * 0.55; y: root.height - 70; rotation: -150; scale: 0.9; tint: "#5f9a3a"; opacity: 0.18 }

    // light motes floating up
    Repeater {
        model: 14
        Rectangle {
            required property int index
            readonly property real seed: (index * 7919 % 100) / 100
            width: 4 + seed * 5; height: width; radius: width / 2
            color: "#fff3a0"; opacity: 0
            x: root.width * ((index * 37 % 100) / 100)
            SequentialAnimation on y {
                loops: Animation.Infinite
                PauseAnimation { duration: index * 600 }
                NumberAnimation { from: root.height + 10; to: -20; duration: 9000 + seed * 6000 }
            }
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                PauseAnimation { duration: index * 600 }
                NumberAnimation { to: 0.55; duration: 1500 }
                NumberAnimation { to: 0.2; duration: 6000 + seed * 4000 }
                NumberAnimation { to: 0; duration: 1500 }
            }
        }
    }
}
