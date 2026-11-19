import QtQuick
import MusicPlayeah
import "Format.js" as Format

// Liked page (M4-Liked, SPEC §8b3): everything you hearted, in tabs: Tracks · Albums · Artists · Composers · Works.
// Newest like first. One virtualized list per tab. Unliking a row removes it at the next reload (tab switch / page
// open), so the list does not jump under the pointer.
Item {
    id: page
    signal albumClicked(var album)
    signal artistClicked(string name)
    signal composerClicked(string name)
    signal workClicked(var workId)
    signal searchRequested(string text)

    property string tab: "track"
    property var counts: ({})
    property var items: []
    property bool dirty: true

    readonly property int cover: 180
    readonly property int gap: 24
    readonly property int listWidth: Math.max(1, list.width - 10)
    readonly property int cols: Math.max(1, Math.floor((listWidth + gap) / (cover + gap)))
    readonly property real cellWidth: Math.floor(listWidth / cols)
    readonly property var currentTrackId: Player.current.id || 0

    readonly property var tabs: [
        { key: "track", label: "Tracks", count: counts.track || 0 },
        { key: "album", label: "Albums", count: counts.album || 0 },
        { key: "artist", label: "Artists", count: counts.artist || 0 },
        { key: "composer", label: "Composers", count: counts.composer || 0 },
        { key: "work", label: "Works", count: counts.work || 0 }
    ]
    readonly property int total: (counts.track || 0) + (counts.album || 0) + (counts.artist || 0) + (counts.composer || 0) + (counts.work || 0)
    readonly property bool playable: tab === "track" || tab === "album" || tab === "work"

    function reload() {
        dirty = false;
        counts = Library.likedCounts();
        items = Library.liked(tab);
    }
    function reloadKeepScroll() {
        const y = list.contentY;
        reload();
        list.contentY = y;
        list.returnToBounds();
    }
    onTabChanged: if (visible) { reload(); list.positionViewAtBeginning(); }
    onVisibleChanged: if (visible) reloadKeepScroll()
    Connections { target: Library; function onRevisionChanged() { if (page.visible) page.reloadKeepScroll(); else page.dirty = true; } }

    // Rows: track / albums (a chunk) / artist / composer / work
    readonly property var rows: {
        if (tab === "album") return Format.chunk(items, cols).map(c => ({ kind: "albums", albums: c }));
        return items.map((it, i) => ({ kind: tab, index: i, item: it }));
    }

    // Tracks: shuffled one by one. Albums and works: shuffled as whole units (movements stay in order).
    function playAll(shuffle) {
        let all = [];
        if (tab === "track") {
            all = shuffle ? Format.shuffled(items) : items.slice();
        } else {
            let ids = items.map(it => it.id);
            if (shuffle) ids = Format.shuffled(ids);
            all = tab === "album" ? Library.tracksOfAlbums(ids) : Library.tracksOfWorks(ids);
        }
        if (all.length > 0) Player.playTracks(all, 0, "Liked");
    }

    readonly property string emptyText: {
        const what = { track: "tracks", album: "albums", artist: "artists", composer: "composers", work: "works" }[tab];
        return total === 0 ? "Nothing liked yet. Tap the heart on a track, album, artist, composer or work to keep it here."
                           : "No liked " + what + " yet.";
    }

    TopBar {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides; topMargin: Theme.pagePaddingTop }
        showBack: false
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

        header: Column {
            width: list.width
            spacing: 18
            Item { width: 1; height: 4 }
            Row {
                spacing: 20
                Rectangle {
                    width: 96; height: 96; radius: 12
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Theme.accent }
                        GradientStop { position: 1; color: Qt.darker(Theme.accent, 2.2) }
                    }
                    Icon { anchors.centerIn: parent; name: "heart"; size: 40; filled: true; color: Theme.textOnAccent }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Text {
                        text: "Liked"
                        color: Theme.textPrimary
                        font { family: Theme.fontDisplay; pixelSize: Theme.sizeAlbumTitle; weight: Font.Bold; letterSpacing: -1.1 }
                    }
                    Text {
                        text: page.total === 0 ? "Your hearts live here." : Format.plural(page.total, "liked item")
                        color: Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
                    }
                }
            }
            Row {
                spacing: 16
                SegmentedControl {
                    model: page.tabs
                    current: page.tab
                    onSelected: (key) => page.tab = key
                }
                PillButton { visible: page.playable; text: "Play all"; icon: "play"; primary: true; enabledAction: page.items.length > 0; onClicked: page.playAll(false) }
                PillButton { visible: page.playable; text: "Shuffle"; icon: "shuffle"; enabledAction: page.items.length > 0; onClicked: page.playAll(true) }
            }
            Item { width: 1; height: 4 }
        }

        delegate: Item {
            id: cell
            required property var modelData
            readonly property string kind: modelData.kind || ""
            width: list.width
            height: kind === "albums" ? page.cover + 56 + page.gap : kind === "track" || kind === "work" ? 52 : 60

            Loader {
                anchors.fill: parent
                sourceComponent: cell.kind === "track" ? trackC : cell.kind === "albums" ? albumsC
                               : cell.kind === "work" ? workC : personC
            }
            Component {
                id: trackC
                SongRow {
                    track: cell.modelData.item
                    extra: Format.likedAgo(cell.modelData.item.likedAt)
                    extraWidth: 110
                    current: cell.modelData.item.id === page.currentTrackId
                    playing: Player.playing
                    onPlay: Player.playTracks(page.items, cell.modelData.index, "Liked")
                    onAlbumClicked: page.albumClicked({ id: cell.modelData.item.albumId })
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
            Component {
                id: workC
                WorkRow {
                    work: cell.modelData.item
                    meta: [cell.modelData.item.composer, Format.plural(cell.modelData.item.recordings, "recording")].filter(s => s).join(" · ")
                    onPlay: {
                        const t = Library.tracksOfWorks([cell.modelData.item.id]);
                        if (t.length > 0) Player.playTracks(t, 0, cell.modelData.item.title);
                    }
                    onClicked: page.workClicked(cell.modelData.item.id)
                }
            }
            Component {
                id: personC
                PersonRow {
                    kind: cell.kind
                    personId: cell.modelData.item.id
                    name: cell.modelData.item.name
                    meta: cell.kind === "composer"
                          ? [Format.plural(cell.modelData.item.works, "work"), Format.plural(cell.modelData.item.recordings, "recording")].join(" · ")
                          : [Format.plural(cell.modelData.item.albums, "album"), Format.plural(cell.modelData.item.tracks, "track")].join(" · ")
                    onClicked: cell.kind === "composer" ? page.composerClicked(cell.modelData.item.name) : page.artistClicked(cell.modelData.item.name)
                }
            }
        }

        footer: Item {
            width: list.width; height: emptyLabel.visible ? 100 : 0
            Text {
                id: emptyLabel
                visible: page.items.length === 0
                anchors { left: parent.left; top: parent.top; topMargin: 16 }
                width: Math.min(parent.width, 520)
                wrapMode: Text.WordWrap
                text: page.emptyText
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
            }
        }
    }
    SmoothScroll { anchors.fill: list; flickable: list }
    VScrollBar { flickable: list; anchors { right: parent.right; rightMargin: 6; top: list.top; bottom: list.bottom } }
}
