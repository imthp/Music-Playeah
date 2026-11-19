import QtQuick
import MusicPlayeah

// "Now playing" equalizer: four bars bouncing at different speeds while `running`; when paused they rest low.
// Animations run only while the item is visible and playing (no work for off-screen rows).
Item {
    id: bars
    property bool running: false
    property color color: Theme.accent
    property int size: 16

    width: size; height: size

    Row {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Math.max(1, Math.round(bars.size / 8))
        Repeater {
            model: [{ d: 520, lo: 0.25, hi: 0.9 }, { d: 680, lo: 0.4, hi: 1.0 }, { d: 440, lo: 0.2, hi: 0.75 }, { d: 600, lo: 0.3, hi: 0.95 }]
            delegate: Rectangle {
                id: bar
                required property var modelData
                required property int index
                readonly property real rest: [0.35, 0.6, 0.45, 0.3][index]
                width: Math.max(2, Math.round(bars.size / 6))
                radius: width / 2
                color: bars.color
                anchors.bottom: parent.bottom
                height: Math.round(bars.size * rest)
                antialiasing: true

                SequentialAnimation on height {
                    running: bars.running && bars.visible
                    loops: Animation.Infinite
                    alwaysRunToEnd: false
                    NumberAnimation { to: bars.size * bar.modelData.hi; duration: bar.modelData.d / 2; easing.type: Easing.InOutSine }
                    NumberAnimation { to: bars.size * bar.modelData.lo; duration: bar.modelData.d / 2; easing.type: Easing.InOutSine }
                    onRunningChanged: if (!running) bar.height = Math.round(bars.size * bar.rest)
                }
            }
        }
    }
}
