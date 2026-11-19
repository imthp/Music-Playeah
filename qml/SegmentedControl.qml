import QtQuick
import MusicPlayeah

// Segmented control (Liked: Tracks · Albums · Artists · Composers · Works): a rounded track, the selected segment
// raised. `model` is [{key, label, count}]; `selected(key)` fires on click. 44 px tall hit area.
Item {
    id: seg
    property var model: []
    property string current
    signal selected(string key)

    implicitWidth: row.implicitWidth + 6
    implicitHeight: Theme.hitTarget

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 40
        radius: 10
        color: Theme.surfaceCard
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: seg.model
            delegate: Item {
                id: segment
                required property var modelData
                readonly property bool on: seg.current === modelData.key
                width: label.implicitWidth + 28
                height: seg.height
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width; height: 34; radius: 8
                    color: segment.on ? Theme.track : (mouse.containsMouse ? Theme.rowHover : "transparent")
                }
                Text {
                    id: label
                    anchors.centerIn: parent
                    textFormat: Text.StyledText
                    text: segment.modelData.label + (segment.modelData.count !== undefined
                          ? "  <font color='" + (segment.on ? Theme.textSecondary : Theme.textTertiary) + "'>" + segment.modelData.count + "</font>" : "")
                    color: segment.on ? Theme.textPrimary : Theme.textSecondary
                    font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1; weight: segment.on ? Font.DemiBold : Font.Medium; features: ({ "tnum": 1 }) }
                }
                MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: seg.selected(segment.modelData.key) }
            }
        }
    }
}
