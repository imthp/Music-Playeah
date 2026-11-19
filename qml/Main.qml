import QtQuick
import MusicPlayeah

// Application shell (milestone 4): sidebar | page, player bar below.
// Pages built so far: Albums, Album, Artist, Composer, Liked, Search. Settings shows the milestone 3a library page (watched folders, scanning) until M10.
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
    property string personKind: "artist"     // artist | composer (the page shown when page === "person")
    property string personName: ""
    property var focusWorkId: 0              // composer page: open and scroll to this work
    // What each person page shows. Kept apart so going artist -> composer -> back does not reload (and scroll) the
    // artist page; only the page whose person changed reloads.
    property string artistName: ""
    property string composerName: ""
    function setPerson(kind, name) {
        personKind = kind;
        personName = name;
        if (kind === "composer") composerName = name; else artistName = name;
    }
    property var albumWorkId: 0              // the album page scrolls to this work
    property var workPageId: 0               // the Work page's work
    property string searchQuery: ""
    property int searchTick: 0               // bumped to focus the Search field again
    // Back stack. Each entry is a full snapshot {page, albumId, personKind, personName, workId, query, label}, so going back
    // returns to *that* album or person, not just that kind of page (album1 → composer → album2 → back → back = album1).
    property var history: []

    function snapshot() {
        const labels = { albums: "Albums", settings: "Settings", liked: "Liked", search: "Search" };
        return {
            page: page, albumId: albumId, personKind: personKind, personName: personName,
            workId: focusWorkId, workPage: workPageId, albumWork: albumWorkId, query: searchQuery,
            label: page === "album" ? (albumPage.info.title || "Album")
                 : page === "person" ? personName
                 : page === "work" ? (workPage.info.title || "Work")
                 : (labels[page] || "Back")
        };
    }
    readonly property string backLabel: history.length > 0 ? history[history.length - 1].label : "Albums"

    // Opens a page; the current one goes on the back stack unless nothing would change.
    function go(key, albumIdArg, kindArg, nameArg, workArg) {
        const nextAlbum = albumIdArg !== undefined ? albumIdArg : albumId;
        const nextKind = kindArg !== undefined ? kindArg : personKind;
        const nextName = nameArg !== undefined ? nameArg : personName;
        // Leaving for a non-person page keeps the composer's open work, so Back returns to the same view.
        const nextWork = workArg !== undefined ? workArg : (key === "person" && nextKind === "composer" ? 0 : focusWorkId);
        const same = key === page
                     && (key !== "album" || nextAlbum === albumId)
                     && (key !== "person" || (nextKind === personKind && nextName === personName && nextWork === focusWorkId));
        if (same) return;
        history = history.concat([snapshot()]).slice(-30);
        albumId = nextAlbum;
        focusWorkId = nextWork;
        setPerson(nextKind, nextName);
        page = key;
    }
    // `workId` (optional): the album opens scrolled to that work (from a Work or Composer page).
    function openAlbum(id, workId) {
        if (!id) return;
        const w = workId || 0;
        if (page === "album" && albumId === id && albumWorkId === w) return;
        history = history.concat([snapshot()]).slice(-30);
        albumWorkId = w;
        albumId = id;
        page = "album";
    }
    function openPerson(kind, name, workId) {
        if (!name) return;
        nowPlayingOpen = false;
        go("person", undefined, kind, name, kind === "composer" ? (workId || 0) : undefined);
    }
    // Work page: one work, every recording you own (just the work's part of each album).
    function openWork(id) {
        if (!id) return;
        nowPlayingOpen = false;
        if (page === "work" && workPageId === id) return;
        history = history.concat([snapshot()]).slice(-30);
        workPageId = id;
        page = "work";
    }
    // Search page: Ctrl+K, or typing in the field on any page. `text` is what was typed so far.
    function openSearch(text) {
        nowPlayingOpen = false;
        if (page !== "search") go("search");
        searchQuery = text || "";
        searchTick += 1;
    }
    function back() {
        if (history.length === 0) { page = "albums"; return; }
        const e = history[history.length - 1];
        history = history.slice(0, -1);
        albumId = e.albumId;
        focusWorkId = e.workId || 0;
        workPageId = e.workPage || 0;
        albumWorkId = e.albumWork || 0;
        setPerson(e.personKind, e.personName);
        searchQuery = e.query || "";
        page = e.page;
    }
    // Sidebar navigation starts a fresh stack.
    function navigate(key) {
        history = [];
        page = key;
    }
    readonly property var placeholders: ({
        artists: "Artists", songs: "Songs", composers: "Composers",
        foryou: "For You", stats: "History & Stats", newplaylist: "Playlists"
    })

    Shortcut { sequence: "Space"; onActivated: Player.playPause() }
    Shortcut {
        sequence: "Esc"
        enabled: root.nowPlayingOpen || root.page === "album" || root.page === "person" || root.page === "search" || root.page === "work"
        onActivated: root.nowPlayingOpen ? root.nowPlayingOpen = false : root.back()
    }
    Shortcut { sequence: "Left"; onActivated: Player.seekBy(-5000) }
    Shortcut { sequence: "Right"; onActivated: Player.seekBy(5000) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: Player.previous() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: Player.next() }
    Shortcut {
        sequence: "Ctrl+K"
        onActivated: root.openSearch(root.page === "search" ? root.searchQuery : "")
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
                       : root.page === "search" || root.page === "work" ? ""
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
                    focusWorkId: root.albumWorkId
                    albumId: root.albumId
                    backLabel: root.backLabel
                    onBack: root.back()
                    onTagClicked: (tag) => { albums.activeTags = [tag]; root.navigate("albums"); }
                    onArtistClicked: (name) => root.openPerson("artist", name)
                    onComposerClicked: (name) => root.openPerson("composer", name)
                    onWorkClicked: (workId) => root.openWork(workId)
                    onSearchRequested: (text) => root.openSearch(text)
                }
                ArtistPage {
                    anchors.fill: parent
                    visible: root.page === "person" && root.personKind === "artist"
                    name: root.artistName
                    backLabel: root.backLabel
                    onBack: root.back()
                    onAlbumClicked: (album) => root.openAlbum(album.id)
                    onSearchRequested: (text) => root.openSearch(text)
                }
                ComposerPage {
                    anchors.fill: parent
                    visible: root.page === "person" && root.personKind === "composer"
                    focusWorkId: root.focusWorkId
                    name: root.composerName
                    backLabel: root.backLabel
                    onBack: root.back()
                    onAlbumClicked: (album) => root.openAlbum(album.id, album.workId)
                    onArtistClicked: (name) => root.openPerson("artist", name)
                    onSearchRequested: (text) => root.openSearch(text)
                }
                WorkPage {
                    id: workPage
                    anchors.fill: parent
                    visible: root.page === "work"
                    workId: root.workPageId
                    backLabel: root.backLabel
                    onBack: root.back()
                    onAlbumClicked: (album) => root.openAlbum(album.id, album.workId)
                    onArtistClicked: (name) => root.openPerson("artist", name)
                    onComposerClicked: (name) => root.openPerson("composer", name)
                    onSearchRequested: (text) => root.openSearch(text)
                }
                LikedPage {
                    anchors.fill: parent
                    visible: root.page === "liked"
                    onAlbumClicked: (album) => root.openAlbum(album.id)
                    onArtistClicked: (name) => root.openPerson("artist", name)
                    onComposerClicked: (name) => root.openPerson("composer", name)
                    onWorkClicked: (workId) => root.openWork(workId)
                    onSearchRequested: (text) => root.openSearch(text)
                }
                SearchPage {
                    anchors.fill: parent
                    visible: root.page === "search"
                    query: root.searchQuery
                    focusTick: root.searchTick
                    onQueryEdited: (text) => root.searchQuery = text
                    onBack: root.back()
                    onAlbumClicked: (album) => root.openAlbum(album.id)
                    onArtistClicked: (name) => root.openPerson("artist", name)
                    onComposerClicked: (name) => root.openPerson("composer", name)
                    onWorkClicked: (workId) => root.openWork(workId)
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
        showRemaining: transport.showRemaining
        onRemainingToggled: transport.showRemaining = !transport.showRemaining
        onAlbumRequested: (id) => { root.nowPlayingOpen = false; root.openAlbum(id); }
        onArtistRequested: (name) => root.openPerson("artist", name)
        onComposerRequested: (name) => root.openPerson("composer", name)
    }
}
