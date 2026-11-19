import QtQuick
import MusicPlayeah
import "Format.js" as Format

// Artist page (M4-Artist, SPEC §8c): header (avatar, counts, Play / Shuffle / heart), the artist's top tracks,
// the albums they lead, and the albums they appear on. Everything comes from the library, offline; photo and bio
// arrive with milestone 9. One virtualized list: heading rows, track rows, and album rows of `cols` cards.
Item {
    id: page
    property string name: ""
    property string backLabel: "Back"
    signal back()
    signal albumClicked(var album)
    signal searchRequested(string text)

    property var person: ({})
    property var tracks: []
    property var led: []
    property var appearsOn: []
    property bool showAll: false
    property bool dirty: true

    readonly property int cover: 180
    readonly property int gap: 24
    readonly property int listWidth: Math.max(1, list.width - 10)
    readonly property int cols: Math.max(1, Math.floor((listWidth + gap) / (cover + gap)))
    readonly property real cellWidth: Math.floor(listWidth / cols)
    readonly property int previewCount: 5
    readonly property bool hasPlays: (person.plays || 0) > 0
    readonly property var currentTrackId: Player.current.id || 0

    LikeState { id: like; kind: "artist"; refId: page.person.id || 0 }

    // ---------------------------------------------------------------- data

    function invalidate() { dirty = true; if (visible) reload(); }
    function reload() {
        dirty = false;
        person = name.length > 0 ? Library.person("artist", name) : ({});
        const id = person.id || 0;
        tracks = id ? Library.artistTracks(id, showAll ? 0 : previewCount) : [];
        led = id ? Library.artistAlbums(id, false) : [];
        appearsOn = id ? Library.artistAlbums(id, true) : [];
        list.positionViewAtBeginning();
    }
    onNameChanged: { showAll = false; invalidate(); }
    onVisibleChanged: if (visible && dirty) reload()
    // Only the track list changes; the rest of the page stays where it is.
    function toggleAll() {
        const y = list.contentY;   // a new row array resets the ListView to the top; stay where the link was
        showAll = !showAll;
        const id = person.id || 0;
        tracks = id ? Library.artistTracks(id, showAll ? 0 : previewCount) : [];
        list.forceLayout();
        list.contentY = y;
        list.returnToBounds();
    }
    Connections { target: Library; function onRevisionChanged() { page.invalidate(); } }

    // Rows: heading / track / albums
    readonly property var rows: {
        const out = [];
        const total = person.tracks || 0;
        if (tracks.length > 0) {
            out.push({ kind: "heading", title: hasPlays ? "Top tracks" : "Tracks",
                       link: total > previewCount ? (showAll ? "Show fewer" : "All " + total + " tracks") : "" });
            tracks.forEach((t, i) => out.push({ kind: "track", index: i, track: t }));
        }
        if (led.length > 0) {
            out.push({ kind: "heading", title: "Albums", link: "" });
            Format.chunk(led, cols).forEach(c => out.push({ kind: "albums", albums: c }));
        }
        if (appearsOn.length > 0) {
            out.push({ kind: "heading", title: "Appears on", link: "" });
            Format.chunk(appearsOn, cols).forEach(c => out.push({ kind: "albums", albums: c }));
        }
        return out;
    }

    readonly property string subtitle: {
        const p = [];
        if (person.albums) p.push(Format.plural(person.albums, "album"));
        if (person.appearances) p.push(person.appearances + (person.appearances === 1 ? " appearance" : " appearances"));
        if (person.tracks) p.push(Format.plural(person.tracks, "track"));
        if (person.plays) p.push(Format.plural(person.plays, "play"));
        return p.join(" · ");
    }

    function playAll(shuffle) {
        const id = person.id || 0;
        if (!id) return;
        let all = Library.artistTracks(id, 0);
        if (shuffle) all = Format.shuffled(all);
        if (all.length > 0) Player.playTracks(all, 0, page.name);
    }

    // ---------------------------------------------------------------- layout

    TopBar {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides; topMargin: Theme.pagePaddingTop }
        backLabel: page.backLabel
        onBack: page.back()
        onSearchRequested: (text) => page.searchRequested(text)
    }

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: parent.bottom; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: Theme.pagePaddingBottom
        cacheBuffer: 1200
        reuseItems: true          // rows scrolled out are reused for rows scrolling in
        model: page.rows

        header: Item {
            width: list.width
            height: headRow.height + 8 + 28
            Row {
                id: headRow
                y: 8
                spacing: 28
                Avatar { diameter: 132; name: page.name }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Text {
                        text: "Artist"
                        color: Theme.textTertiary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
                    }
                    Text {
                        width: Math.min(implicitWidth, list.width - 132 - 28)
                        text: page.name
                        elide: Text.ElideRight; maximumLineCount: 1
                        color: Theme.textPrimary
                        font { family: Theme.fontDisplay; pixelSize: Theme.sizeAlbumTitle; weight: Font.Bold; letterSpacing: -1.1 }
                    }
                    Text {
                        text: (page.person.id || 0) ? page.subtitle : "Nothing in your library is tagged with this name."
                        color: Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
                    }
                    Row {
                        topPadding: 4
                        spacing: 10
                        PillButton { text: "Play"; icon: "play"; primary: true; enabledAction: (page.person.id || 0) > 0; onClicked: page.playAll(false) }
                        PillButton { text: "Shuffle"; icon: "shuffle"; enabledAction: (page.person.id || 0) > 0; onClicked: page.playAll(true) }
                        PillButton {
                            icon: "heart"
                            enabledAction: (page.person.id || 0) > 0
                            iconFilled: like.liked
                            iconColor: like.liked ? Theme.accent : Theme.textPrimary
                            onClicked: like.toggle()
                        }
                    }
                }
            }
        }

        delegate: Item {
            id: cell
            required property var modelData
            readonly property string kind: modelData.kind || ""
            width: list.width
            height: kind === "heading" ? 56 : kind === "track" ? 52 : page.cover + 56 + page.gap

            Loader {
                anchors.fill: parent
                sourceComponent: cell.kind === "heading" ? headingC : cell.kind === "track" ? trackC : albumsC
            }
            Component {
                id: headingC
                Item {
                    Text {
                        anchors { left: parent.left; bottom: parent.bottom; bottomMargin: 10 }
                        text: cell.modelData.title || ""
                        color: Theme.textPrimary
                        font { family: Theme.fontDisplay; pixelSize: Theme.sizeSection; weight: Font.Bold; letterSpacing: -0.2 }
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 16; bottom: parent.bottom; bottomMargin: 12 }
                        visible: (cell.modelData.link || "").length > 0
                        text: cell.modelData.link || ""
                        color: linkMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
                        MouseArea { id: linkMouse; anchors { fill: parent; margins: -8 } hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.toggleAll() }
                    }
                }
            }
            Component {
                id: trackC
                SongRow {
                    track: cell.modelData.track
                    extra: (cell.modelData.track.plays || 0) > 0 ? Format.plural(cell.modelData.track.plays, "play") : ""
                    extraWidth: 90
                    current: cell.modelData.track.id === page.currentTrackId
                    playing: Player.playing
                    onPlay: Player.playTracks(page.tracks, cell.modelData.index, page.name)
                    onAlbumClicked: page.albumClicked({ id: cell.modelData.track.albumId })
                }
            }
            Component {
                id: albumsC
                AlbumRow {
                    albums: cell.modelData.albums
                    cover: page.cover
                    cellWidth: page.cellWidth
                    onAlbumClicked: (album) => page.albumClicked(album)
                }
            }
        }

        footer: Item {
            width: list.width; height: emptyText.visible ? 80 : 0
            Text {
                id: emptyText
                visible: (page.person.id || 0) > 0 && page.rows.length === 0
                anchors { left: parent.left; top: parent.top; topMargin: 24 }
                text: "No albums or tracks to show yet."
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
            }
        }
    }
    SmoothScroll { anchors.fill: list; flickable: list }
    VScrollBar { flickable: list; anchors { right: parent.right; rightMargin: 6; top: list.top; bottom: list.bottom } }
}
