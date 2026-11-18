import QtQuick
import MusicPlayeah

// Clickable name (artist, composer, album): underlines on hover. Elides at `maxWidth`.
Text {
    id: link
    property real maxWidth: 10000
    property int size: 16
    signal clicked()

    width: Math.min(implicitWidth, maxWidth)
    elide: Text.ElideRight
    maximumLineCount: 1
    color: Theme.textPrimary
    font { family: Theme.fontUi; pixelSize: size; weight: Font.DemiBold; underline: mouse.containsMouse }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: link.clicked()
    }
}
