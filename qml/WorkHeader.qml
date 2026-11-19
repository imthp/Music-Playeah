import QtQuick
import MusicPlayeah

// Work header (M4-Album), top of a work's panel: white "play work" button, title + catalog number, meta line.
// Movements are listed under it as indented TrackRows. The heart likes the work.
Item {
    id: head
    property string title
    property string catalog
    property string meta
    property var workId: 0           // the work the heart likes (0 = no heart)
    property int recordings: 0       // recordings of this work in the library; > 1 shows a link to all of them
    property bool current: false     // a movement of this work is playing
    signal play()
    signal recordingsClicked()        // opens the Work page (every recording you own)

    // Rich text needs &, < escaped (titles like "Bach & Sons").
    function esc(t) { return t.replace(/&/g, "&amp;").replace(/</g, "&lt;"); }

    height: 64
    // Drawn on the work's panel (AlbumPage); a hairline separates the header from its movements.
    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 12; rightMargin: 12 } height: 1; color: "#1E1E23" }

    Rectangle {
        id: playButton
        x: 12; anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 36; radius: 18
        color: playMouse.containsMouse ? Theme.textNav : Theme.textPrimary
        Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 1; name: "play"; size: 16; filled: true; color: Theme.bg }
        MouseArea {
            id: playMouse
            anchors { fill: parent; margins: -4 }
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: head.play()
        }
    }
    Column {
        anchors { left: playButton.right; leftMargin: 12; right: workHeart.left; rightMargin: 4; verticalCenter: parent.verticalCenter }
        spacing: 2
        Text {
            width: parent.width
            text: head.catalog.length > 0
                  ? esc(head.title) + "  <font color='" + Theme.textTertiary + "'>" + esc(head.catalog) + "</font>"
                  : esc(head.title)
            textFormat: Text.StyledText
            elide: Text.ElideRight; maximumLineCount: 1
            color: head.current ? Theme.accent : Theme.textPrimary
            font { family: Theme.fontUi; pixelSize: 15; weight: Font.DemiBold }
        }
        // Meta line; "N recordings in your library" looks like the rest but is clickable (brightens on hover).
        Row {
            width: parent.width
            clip: true
            Text {
                id: metaText
                width: Math.min(implicitWidth, parent.width)
                text: head.meta + (recLink.visible ? " · " : "")
                elide: Text.ElideRight
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeMeta; features: ({ "tnum": 1 }) }
            }
            Text {
                id: recLink
                visible: head.recordings > 1
                text: head.recordings + " recordings in your library"
                color: recMouse.containsMouse ? Theme.textPrimary : Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeMeta; features: ({ "tnum": 1 }) }
                MouseArea {
                    id: recMouse
                    anchors { fill: parent; topMargin: -8; bottomMargin: -8 }
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: head.recordingsClicked()
                }
            }
        }
    }
    HeartButton {
        id: workHeart
        anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
        visible: head.workId ? true : false
        kind: "work"; refId: head.workId; label: head.title
    }
}
