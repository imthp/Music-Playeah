import QtQuick
import MusicPlayeah
import "Format.js" as Format

// A track shown outside its album page (Liked, Artist, Search): cover · title + artist · album · heart · extra · time.
// 52 px tall (M4-Liked). Double-click, or the play button that appears over the cover, plays it. Playing: accent tint,
// accent title, bars over the cover. `showAlbum` / `showHeart` / `extraWidth` 0 drop columns (Search shows fewer).
// Pages with a column header use the same widths: cover 56, heart 48, extra `extraWidth`, time 64.
Item {
    id: row
    property var track                  // map as Library.albumTracks() returns it
    property bool showAlbum: true
    property bool showHeart: true
    property string extra               // text in the extra column: "today", "12 plays"
    property int extraWidth: 110
    property bool current: false
    property bool playing: false
    signal play()
    signal albumClicked()

    readonly property int coverColumn: 56
    readonly property int timeColumn: 64
    readonly property int heartColumn: showHeart ? 48 : 0
    readonly property real contentWidth: Math.max(0, width - coverColumn - heartColumn - extraWidth - timeColumn)
    readonly property real titleWidth: showAlbum ? contentWidth * 1.4 / 2.4 : contentWidth
    readonly property color rowColor: current ? Qt.tint(Theme.bg, Theme.accentTint) : (mouse.containsMouse ? Theme.rowHover : Theme.bg)
    readonly property string artHash: track ? (track.albumArtHash || "") : ""

    height: 52

    Rectangle { anchors.fill: parent; radius: Theme.radiusInput; color: row.current ? Theme.accentTint : (mouse.containsMouse ? Theme.rowHover : "transparent") }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onDoubleClicked: row.play() }

    // cover, with play / bars over it
    Item {
        id: cover
        x: 8; anchors.verticalCenter: parent.verticalCenter
        width: 40; height: 40
        Rectangle { anchors.fill: parent; radius: 4; color: Theme.surfaceCard }
        RoundedImage {
            anchors.fill: parent
            radius: 4
            backdrop: row.rowColor
            visible: row.artHash.length > 0
            source: row.artHash.length > 0 ? "image://art/" + row.artHash + "/256" : ""
            sourceSize: Qt.size(80, 80)
        }
        Rectangle {
            anchors.fill: parent; radius: 4
            color: "#99000000"
            visible: coverMouse.containsMouse || row.current
            PlayingBars {
                anchors.centerIn: parent
                visible: row.current && !coverMouse.containsMouse
                running: row.playing
                size: 16
            }
            Icon {
                anchors.centerIn: parent
                visible: coverMouse.containsMouse || !row.current
                name: row.current && row.playing ? "pause" : "play"
                size: 16; filled: true; color: Theme.textPrimary
            }
        }
        MouseArea {
            id: coverMouse
            anchors { fill: parent; margins: -2 }
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.current ? Player.playPause() : row.play()
        }
    }

    // title over artist
    Column {
        x: row.coverColumn
        width: row.titleWidth - 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Text {
            width: parent.width
            text: row.track ? row.track.title : ""
            elide: Text.ElideRight; maximumLineCount: 1
            color: row.current ? Theme.accent : Theme.textPrimary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody; weight: row.current ? Font.DemiBold : Font.Medium }
        }
        Text {
            width: parent.width
            text: row.track ? (row.track.artist || row.track.albumArtist || "") : ""
            elide: Text.ElideRight; maximumLineCount: 1
            color: Theme.textSecondary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
        }
    }

    // album (a link)
    Text {
        visible: row.showAlbum
        x: row.coverColumn + row.titleWidth
        width: row.contentWidth - row.titleWidth - 12
        anchors.verticalCenter: parent.verticalCenter
        text: row.track ? row.track.albumTitle : ""
        elide: Text.ElideRight; maximumLineCount: 1
        color: Theme.textSecondary
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; underline: albumMouse.containsMouse }
        MouseArea { id: albumMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: row.albumClicked() }
    }

    HeartButton {
        id: heart
        visible: row.showHeart
        anchors { right: extraCell.left; verticalCenter: parent.verticalCenter }
        width: 48
        kind: "track"
        refId: row.track ? row.track.id : 0
        label: row.track ? row.track.title : ""
    }
    Text {
        id: extraCell
        visible: row.extraWidth > 0
        anchors { right: timeCell.left; verticalCenter: parent.verticalCenter }
        width: row.extraWidth; horizontalAlignment: Text.AlignRight
        text: row.extra
        elide: Text.ElideRight
        color: Theme.textTertiary
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
    }
    Text {
        id: timeCell
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: row.timeColumn; rightPadding: 16; horizontalAlignment: Text.AlignRight
        text: row.track ? Format.time(row.track.durationMs) : ""
        color: Theme.textSecondary
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
    }
}
