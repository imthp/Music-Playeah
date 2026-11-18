import QtQuick
import MusicPlayeah

// 32 px pill. Selected: white background, dark text. Otherwise a quiet outline. Hit area is 44 px tall.
Item {
    id: chip
    property string text
    property bool selected: false
    signal clicked()

    implicitWidth: label.implicitWidth + 28
    implicitHeight: Theme.hitTarget

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 32
        radius: height / 2
        color: chip.selected ? Theme.textPrimary : (mouse.containsMouse ? Theme.surfaceCard : "transparent")
        border { color: chip.selected ? "transparent" : Theme.chipBorder; width: 1 }
        Text {
            id: label
            anchors.centerIn: parent
            text: chip.text
            color: chip.selected ? Theme.textOnAccent : Theme.textNav
            font { family: Theme.fontUi; pixelSize: Theme.sizeMeta + 1; weight: Font.Medium }
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.clicked()
    }
}
