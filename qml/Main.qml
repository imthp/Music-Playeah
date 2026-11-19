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
    property var history: []          // pages to go back to (Album page's back link, Esc)

    function go(key) {
        if (key === page) return;
        history = history.concat([page]).slice(-20);
        page = key;
    }
    function openAlbum(id) {
        if (!id) return;
        albumId = id;
        go("album");
    }
    function back() {
        if (history.length === 0) { page = "albums"; return; }
        page = history[history.length - 1];
        history = history.slice(0, -1);
    }
    readonly property var placeholders: ({
        artists: "Artists", songs: "Songs", composers: "Composers", liked: "Liked",
        foryou: "For You", stats: "History & Stats", newplaylist: "Playlists"
    })

    Shortcut { sequence: "Space"; onActivated: Player.playPause() }
    Shortcut {
        sequence: "Esc"
        enabled: root.nowPlayingOpen || root.page === "album"
        onActivated: root.nowPlayingOpen ? root.nowPlayingOpen = false : root.back()
    }
    Shortcut { sequence: "Left"; onActivated: Player.seekBy(-5000) }
    Shortcut { sequence: "Right"; onActivated: Player.seekBy(5000) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: Player.previous() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: Player.next() }
    Shortcut {
        sequence: "Ctrl+K"
        onActivated: { root.page = "albums"; albums.focusSearch(); }   // M4 search page replaces this
    }

    Column {
        anchors.fill: parent

        Row {
            width: parent.width
            height: parent.height - transport.height

            Sidebar {
                height: parent.height
                current: root.page === "album" ? "albums" : root.page
                onNavigate: (key) => { root.history = []; root.page = key; }
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
                    anchors.fill: parent
                    visible: root.page === "album"
                    albumId: root.albumId
                    onBack: root.back()
                    onTagClicked: (tag) => { albums.activeTags = [tag]; root.history = []; root.page = "albums"; }
                    // Artist / composer pages come in 4b.
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
    }
}
