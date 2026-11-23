// "Liquid glass": the backdrop behind the panel, blurred and brightened, clipped to a rounded shape,
// with a light rim and a soft specular sheen on top.
import QtQuick
import QtQuick.Effects

Item {
    id: glass
    property Item backdrop          // what is behind the panel (blurred)
    property real radius: 32
    property real tint: 0.10        // white veil over the blur
    default property alias content: inner.data

    // the blurred backdrop, clipped to the rounded rectangle
    ShaderEffectSource {
        id: grab
        anchors.fill: parent
        sourceItem: glass.backdrop
        sourceRect: glass.backdrop ? Qt.rect(glass.mapToItem(glass.backdrop, 0, 0).x, glass.mapToItem(glass.backdrop, 0, 0).y,
                                             glass.width, glass.height) : Qt.rect(0, 0, 0, 0)
        live: true
        visible: false
    }
    Rectangle {
        id: mask
        anchors.fill: parent
        radius: glass.radius
        visible: false
        layer.enabled: true
    }
    MultiEffect {
        anchors.fill: parent
        source: grab
        blurEnabled: true
        blur: 1.0
        blurMax: 64
        blurMultiplier: 1.2
        saturation: 0.25
        brightness: 0.06
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }
    // veil + rim + sheen
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: Qt.rgba(1, 1, 1, glass.tint)
        border.width: 1.5
        border.color: Qt.rgba(1, 1, 1, 0.32)
    }
    Rectangle {  // brighter rim at the top edge, like light caught by the glass
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1.5 }
        height: Math.min(parent.height * 0.5, 160)
        radius: glass.radius - 1.5
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.16) }
            GradientStop { position: 0.35; color: Qt.rgba(1, 1, 1, 0.04) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }
    Item { id: inner; anchors.fill: parent }
}
