import QtQuick
import MusicPlayeah

// Square cover with title and artist under it. `cover` is the side of the square in px.
Item {
    id: card
    property var album            // map from Library.albums()
    property int cover: 200
    signal clicked()
    signal playClicked()
    property bool hoveredOnce: false

    width: cover
    height: cover + 56

    Rectangle {
        id: frame
        width: card.cover; height: card.cover
        radius: Theme.radiusCover
        color: Theme.surfaceCard
        RoundedImage {
            anchors.fill: parent
            visible: card.album && card.album.artHash.length > 0
            source: card.album && card.album.artHash.length > 0 ? "image://art/" + card.album.artHash + "/" + (card.cover > 200 ? 600 : 256) : ""
            sourceSize: Qt.size(card.cover * 2, card.cover * 2)
        }
        Loader {
            anchors.centerIn: parent
            active: !card.album || card.album.artHash.length === 0
            sourceComponent: Icon { name: "albums"; size: Math.round(card.cover / 4); color: Theme.heartIdle }
        }
        // Hover dim + play button, created on first hover only (keeps each card light while the grid scrolls).
        Loader {
            anchors.fill: parent
            active: card.hoveredOnce
            sourceComponent: Item {
                opacity: mouse.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
                Rectangle { anchors.fill: parent; radius: Theme.radiusCover; color: "#66000000" }
                Rectangle {
                    width: 44; height: 44; radius: 22
                    anchors { right: parent.right; bottom: parent.bottom; margins: 10 }
                    color: Theme.accent
                    Icon { anchors.centerIn: parent; name: "play"; size: 18; color: Theme.textOnAccent; filled: true }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: card.playClicked()
                    }
                }
            }
        }
    }
    Text {
        anchors { left: parent.left; right: parent.right; top: frame.bottom; topMargin: 10 }
        text: card.album ? card.album.title : ""
        color: Theme.textPrimary
        elide: Text.ElideRight
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; weight: Font.DemiBold }
    }
    Text {
        anchors { left: parent.left; right: parent.right; top: frame.bottom; topMargin: 32 }
        text: card.album ? card.album.artist : ""
        color: Theme.textSecondary
        elide: Text.ElideRight
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        z: -1
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: if (containsMouse) card.hoveredOnce = true
        onClicked: card.clicked()
    }
}
