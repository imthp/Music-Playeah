import QtQuick
import MusicPlayeah

// Application shell (milestone 4): sidebar | page, player bar below.
// Pages built so far: Albums, Album. Settings shows the milestone 3a library page (watched folders, scanning) until M10.
// Pages not built yet show a placeholder. Keys: Space play/pause · Left/Right seek 5 s · Ctrl+Left/Right previous/next · Ctrl+K search.
Window {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1000
    minimumHeight: 620
    visible: true
    title: Player.title.length > 0 ? Player.title + " · " + AppInfo.name : AppInfo.name
    color: Theme.bg

    property string page: "albums"
    property bool nowPlayingOpen: false
    property var albumId: 0
    property string personKind: "artist"     // placeholder Artist / Composer page (4b builds the real ones)
    property string personName: ""
    // Back stack. Each entry is a full snapshot {page, albumId, personKind, personName, label}, so going back
    // returns to *that* album or person, not just that kind of page (album1 → composer → album2 → back → back = album1).
    property var history: []

    function snapshot() {
        const labels = { albums: "Albums", settings: "Settings" };
        return {
            page: page, albumId: albumId, personKind: personKind, personName: personName,
            label: page === "album" ? (albumPage.info.title || "Album")
                 : page === "person" ? personName
                 : (labels[page] || "Back")
        };
    }
    readonly property string backLabel: history.length > 0 ? history[history.length - 1].label : "Albums"

    // Opens a page; the current one goes on the back stack unless nothing would change.
    function go(key, albumIdArg, kindArg, nameArg) {
        const nextAlbum = albumIdArg !== undefined ? albumIdArg : albumId;
        const nextKind = kindArg !== undefined ? kindArg : personKind;
        const nextName = nameArg !== undefined ? nameArg : personName;
        const same = key === page
                     && (key !== "album" || nextAlbum === albumId)
                     && (key !== "person" || (nextKind === personKind && nextName === personName));
        if (same) return;
        history = history.concat([snapshot()]).slice(-30);
        albumId = nextAlbum;
        personKind = nextKind;
        personName = nextName;
        page = key;
    }
    function openAlbum(id) {
        if (!id) return;
        go("album", id);
    }
    function openPerson(kind, name) {
        if (!name) return;
        nowPlayingOpen = false;
        go("person", undefined, kind, name);
    }
    function back() {
        if (history.length === 0) { page = "albums"; return; }
        const e = history[history.length - 1];
        history = history.slice(0, -1);
        albumId = e.albumId;
        personKind = e.personKind;
        personName = e.personName;
        page = e.page;
    }
    // Sidebar navigation starts a fresh stack.
    function navigate(key) {
        history = [];
        page = key;
    }
    readonly property var placeholders: ({
        artists: "Artists", songs: "Songs", composers: "Composers", liked: "Liked",
        foryou: "For You", stats: "History & Stats", newplaylist: "Playlists"
    })

    Shortcut { sequence: "Space"; onActivated: Player.playPause() }
    Shortcut {
        sequence: "Esc"
        enabled: root.nowPlayingOpen || root.page === "album" || root.page === "person"
        onActivated: root.nowPlayingOpen ? root.nowPlayingOpen = false : root.back()
    }
    Shortcut { sequence: "Left"; onActivated: Player.seekBy(-5000) }
    Shortcut { sequence: "Right"; onActivated: Player.seekBy(5000) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: Player.previous() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: Player.next() }
    Shortcut {
        sequence: "Ctrl+K"
        onActivated: { root.navigate("albums"); albums.focusSearch(); }   // M4 search page replaces this
    }

    Column {
        anchors.fill: parent

        Row {
            width: parent.width
            height: parent.height - transport.height

            Sidebar {
                height: parent.height
                current: root.page === "album" ? "albums"
                       : root.page === "person" ? (root.personKind === "composer" ? "composers" : "artists")
                       : root.page
                onNavigate: (key) => root.navigate(key)
            }

            Item {
                width: parent.width - Theme.sidebarWidth
                height: parent.height

                AlbumsPage {
                    id: albums
                    anchors.fill: parent
                    visible: root.page === "albums"
                    onAlbumClicked: (album) => root.openAlbum(album.id)
                }
                AlbumPage {
                    id: albumPage
                    anchors.fill: parent
                    visible: root.page === "album"
                    albumId: root.albumId
                    backLabel: root.backLabel
                    onBack: root.back()
                    onTagClicked: (tag) => { albums.activeTags = [tag]; root.navigate("albums"); }
                    onArtistClicked: (name) => root.openPerson("artist", name)
                    onComposerClicked: (name) => root.openPerson("composer", name)
                }
                PersonPage {
                    anchors.fill: parent
                    visible: root.page === "person"
                    kind: root.personKind
                    name: root.personName
                    backLabel: root.backLabel
                    onBack: root.back()
                    onAlbumClicked: (album) => root.openAlbum(album.id)
                }
                LibraryTest {
                    anchors { fill: parent; margins: 24 }
                    visible: root.page === "settings"
                }
                Text {
                    anchors.centerIn: parent
                    visible: root.placeholders[root.page] !== undefined
                    text: (root.placeholders[root.page] || "") + " — not built yet"
                    color: Theme.textTertiary
                    font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
                }
            }
        }

        TransportBar {
            id: transport
            width: parent.width
            onAlbumRequested: (id) => root.openAlbum(id)
            onNowPlayingRequested: root.nowPlayingOpen = true
            onArtistRequested: (name) => root.openPerson("artist", name)
        }
    }

    // Over everything, sidebar and player bar included (M4-NowPlaying). Slides up / fades in.
    NowPlaying {
        id: nowPlaying
        width: parent.width
        height: parent.height
        y: root.nowPlayingOpen ? 0 : 40
        opacity: root.nowPlayingOpen ? 1 : 0
        visible: opacity > 0
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 180 } }
        onClose: root.nowPlayingOpen = false
        onAlbumRequested: (id) => { root.nowPlayingOpen = false; root.openAlbum(id); }
        onArtistRequested: (name) => root.openPerson("artist", name)
        onComposerRequested: (name) => root.openPerson("composer", name)
    }
}
