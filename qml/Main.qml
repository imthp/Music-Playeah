import QtQuick
import MusicPlayeah

// Application shell (milestone 4): sidebar | page, player bar below.
// Pages built so far: Albums. Settings shows the milestone 3a library page (watched folders, scanning) until M10.
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
    readonly property var placeholders: ({
        artists: "Artists", songs: "Songs", composers: "Composers", liked: "Liked",
        foryou: "For You", stats: "History & Stats", newplaylist: "Playlists"
    })

    Shortcut { sequence: "Space"; onActivated: Player.playPause() }
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
                current: root.page
                onNavigate: (key) => root.page = key
            }

            Item {
                width: parent.width - Theme.sidebarWidth
                height: parent.height

                AlbumsPage {
                    id: albums
                    anchors.fill: parent
                    visible: root.page === "albums"
                    onAlbumClicked: (album) => Player.playTracks(Library.albumTracks(album.id), 0, album.title)   // Album page comes next
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
        }
    }
}
