import QtQuick
import MusicPlayeah

// One track (M4-Album grid: 48 # · title · 48 heart · 80 plays · 72 time), 44 px tall.
// `indent` for movements under a work header. Playing: accent tint and title, bars instead of the number.
// Double-click (or the play icon shown over the number on hover) plays from this track.
Item {
    id: row
    property string number          // "3" or "IV."
    property string title
    property string subtitle        // optional second text after the title, tertiary (e.g. the artist)
    property int plays: 0
    property string time
    property bool current: false    // this track is loaded in the player
    property bool playing: false    // ... and audible
    property bool indent: false
    signal play()

    // Rich text needs &, < escaped (titles like "Bach & Sons").
    function esc(t) { return t.replace(/&/g, "&amp;").replace(/</g, "&lt;"); }

    height: Theme.hitTarget

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusInput
        color: row.current ? Theme.accentTint : (mouse.containsMouse ? Theme.rowHover : "transparent")
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onDoubleClicked: row.play()
    }

    // # column (48)
    Item {
        id: numberCell
        x: 0; width: 48; height: parent.height
        Text {
            anchors { right: parent.right; rightMargin: row.indent ? 8 : 16; verticalCenter: parent.verticalCenter }
            visible: !row.current && !mouse.containsMouse
            text: row.number
            color: Theme.textTertiary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
        }
        PlayingBars {
            anchors { right: parent.right; rightMargin: row.indent ? 10 : 14; verticalCenter: parent.verticalCenter }
            visible: row.current && !mouse.containsMouse
            running: row.current && row.playing
            size: 14
        }
        Item {
            anchors { right: parent.right; rightMargin: row.indent ? 6 : 10; verticalCenter: parent.verticalCenter }
            width: 24; height: 24
            visible: mouse.containsMouse
            Icon { anchors.centerIn: parent; name: row.current && row.playing ? "pause" : "play"; size: 14; filled: true; color: Theme.textPrimary }
            MouseArea {
                anchors { fill: parent; margins: -10 }
                cursorShape: Qt.PointingHandCursor
                onClicked: row.current ? Player.playPause() : row.play()
            }
        }
    }

    // title
    Text {
        id: titleText
        anchors { left: numberCell.right; leftMargin: row.indent ? 20 : 16; right: heartCell.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
        text: row.subtitle.length > 0 ? esc(row.title) + "  <font color='" + Theme.textTertiary + "'>" + esc(row.subtitle) + "</font>" : row.title
        textFormat: row.subtitle.length > 0 ? Text.StyledText : Text.PlainText
        elide: Text.ElideRight
        maximumLineCount: 1
        color: row.current ? Theme.accent : Theme.textPrimary
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; weight: row.current ? Font.DemiBold : Font.Medium }
    }

    // heart (48): likes arrive in 4b
    Item { id: heartCell; anchors { right: playsCell.left } width: 48; height: parent.height }

    Text {
        id: playsCell
        anchors { right: timeCell.left; verticalCenter: parent.verticalCenter }
        width: 80; horizontalAlignment: Text.AlignRight
        text: row.plays > 0 ? row.plays : ""
        color: Theme.textTertiary
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
    }
    Text {
        id: timeCell
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        // Columns line up with the header in or out of a work panel (rows in a panel are inset 8 px).
        width: row.indent ? 64 : 72; rightPadding: row.indent ? 8 : 16; horizontalAlignment: Text.AlignRight
        text: row.time
        color: Theme.textSecondary
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
    }
}
