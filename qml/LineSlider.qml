import QtQuick
import MusicPlayeah

// Thin line slider: `trackHeight` px line (2 for the cover-size slider, 4 in the player bar), accent fill, 12 px white knob.
// `value` is 0..1. `moved` fires while dragging, `released` at the end. The hit area is at least Theme.hitTarget tall.
Item {
    id: slider
    property real value: 0
    property int trackHeight: 4
    property bool showKnob: true
    readonly property bool dragging: area.pressed
    property real dragValue: 0
    readonly property real shown: dragging ? dragValue : value
    signal moved(real v)
    signal released(real v)

    implicitHeight: Theme.hitTarget
    implicitWidth: 120

    function valueAt(x) { return Math.max(0, Math.min(1, (x - 6) / width)); }   // x is in the hit area, 6 px wider on each side

    Rectangle {
        id: line
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: slider.trackHeight
        radius: height / 2
        color: Theme.track
        Rectangle {
            width: parent.width * slider.shown
            height: parent.height
            radius: parent.radius
            color: Theme.accent
        }
    }
    Rectangle {
        visible: slider.showKnob
        width: 12; height: 12; radius: 6
        color: Theme.knob
        x: slider.width * slider.shown - width / 2
        anchors.verticalCenter: parent.verticalCenter
    }
    // Hit area: at least Theme.hitTarget tall even when the slider is laid out thinner (player bar: 12 px).
    MouseArea {
        id: area
        anchors { left: parent.left; right: parent.right; leftMargin: -6; rightMargin: -6; verticalCenter: parent.verticalCenter }
        height: Math.max(parent.height, Theme.hitTarget)
        cursorShape: Qt.PointingHandCursor
        onPressed: (m) => { slider.dragValue = slider.valueAt(m.x); slider.moved(slider.dragValue); }
        onPositionChanged: (m) => { if (pressed) { slider.dragValue = slider.valueAt(m.x); slider.moved(slider.dragValue); } }
        onReleased: (m) => slider.released(slider.valueAt(m.x))
    }
}
