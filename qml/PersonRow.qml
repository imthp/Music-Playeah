import QtQuick
import MusicPlayeah

// An artist or composer in a list (Liked, Search): avatar · name · counts · heart. 60 px tall. Click opens their page.
Item {
    id: row
    property string kind: "artist"      // artist | composer
    property var personId: 0            // the id likes use (composer: canonical, D49)
    property string name
    property string meta
    property bool showHeart: true
    signal clicked()

    height: 60

    Rectangle { anchors.fill: parent; radius: Theme.radiusInput; color: mouse.containsMouse ? Theme.rowHover : "transparent" }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: row.clicked() }

    Avatar { id: avatar; x: 8; anchors.verticalCenter: parent.verticalCenter; diameter: 44; name: row.name }
    Column {
        anchors { left: avatar.right; leftMargin: 14; right: heart.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
        spacing: 1
        Text {
            width: parent.width
            text: row.name
            elide: Text.ElideRight; maximumLineCount: 1
            color: Theme.textPrimary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody; weight: Font.Medium }
        }
        Text {
            width: parent.width
            text: row.meta
            elide: Text.ElideRight; maximumLineCount: 1
            color: Theme.textSecondary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
        }
    }
    HeartButton {
        id: heart
        visible: row.showHeart
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        kind: row.kind
        refId: row.personId
        label: row.name
    }
}
