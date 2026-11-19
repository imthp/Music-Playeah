import QtQuick
import MusicPlayeah

// Placeholder Artist / Composer page (4a): every name link already leads here. Milestone 4b replaces it with the
// real pages (SPEC §8c): albums they lead / appear on and top tracks for an artist; works by family for a composer.
// Until then it shows who it is and the albums of theirs in the library (by album artist / track composer).
Item {
    id: page
    property string kind: "artist"     // "artist" | "composer"
    property string name: ""
    property string backLabel: "Back"
    signal back()
    signal albumClicked(var album)

    readonly property var albums: {
        Library.revision;   // re-read after a scan
        if (name.length === 0) return [];
        return Library.albums().filter(a => kind === "artist" ? a.artist === name : (a.composers || "").split("\n").indexOf(name) >= 0);
    }

    function initials(n) {
        const parts = n.split(/[\s,.\-]+/).filter(p => p.length > 0 && p[0].toUpperCase() !== p[0].toLowerCase());
        if (parts.length === 0) return "?";
        return (parts[0][0] + (parts.length > 1 ? parts[parts.length - 1][0] : "")).toUpperCase();
    }

    Item {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides; topMargin: Theme.pagePaddingTop }
        height: Theme.hitTarget
        Row {
            id: backLink
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevronLeft"; size: 16; color: backMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: page.backLabel
                color: backMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1; weight: Font.Medium }
            }
        }
        MouseArea { id: backMouse; anchors { fill: backLink; margins: -10 } hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.back() }
    }

    // header: avatar (initials until photos arrive in M9), kind, name, count
    Row {
        id: header
        anchors { left: parent.left; top: topBar.bottom; leftMargin: Theme.pagePaddingSides; topMargin: 16 }
        spacing: 24
        Rectangle {
            width: 120; height: 120; radius: 60
            color: "#1E1E23"
            Text {
                anchors.centerIn: parent
                text: page.initials(page.name)
                color: Theme.textSecondary
                font { family: Theme.fontDisplay; pixelSize: 40; weight: Font.DemiBold }
            }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                text: page.kind === "composer" ? "Composer" : "Artist"
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
            }
            Text {
                width: Math.min(implicitWidth, page.width - 2 * Theme.pagePaddingSides - 144)
                text: page.name
                elide: Text.ElideRight
                color: Theme.textPrimary
                font { family: Theme.fontDisplay; pixelSize: Theme.sizeAlbumTitle; weight: Font.Bold; letterSpacing: -1.1 }
            }
            Text {
                text: page.albums.length + (page.albums.length === 1 ? " album" : " albums") + " in your library · full page coming soon"
                color: Theme.textSecondary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
            }
        }
    }

    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides - 10; topMargin: 32 }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        readonly property int cover: 180
        readonly property int cols: Math.max(1, Math.floor((width - 10) / (cover + 24)))
        cellWidth: Math.floor((width - 10) / cols)
        cellHeight: cover + 56 + 24
        model: page.albums
        delegate: Item {
            required property var modelData
            width: grid.cellWidth; height: grid.cellHeight
            AlbumCard {
                album: modelData
                cover: grid.cover
                onClicked: page.albumClicked(modelData)
                onPlayClicked: Player.playTracks(Library.albumTracks(modelData.id), 0, modelData.title)
            }
        }
    }
    VScrollBar { flickable: grid; anchors { right: parent.right; rightMargin: 6; top: grid.top; bottom: grid.bottom } }
}
