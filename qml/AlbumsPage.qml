import QtQuick
import QtCore
import MusicPlayeah

// Albums grid (M4-Albums): title and counts, search, sort chips, instrument filter chips, cover-size slider.
Item {
    id: page
    signal albumClicked(var album)

    property var allAlbums: []
    property string sortKey: "title"          // title | artist | year
    property var activeTags: []               // instrument tags, all must match
    property string query: ""
    property bool hiResOnly: false

    Settings {
        id: prefs
        category: "albums"
        property real coverSize: 200
    }

    function reload() { allAlbums = Library.albums(); }
    Component.onCompleted: reload()
    Connections { target: Library; function onRevisionChanged() { page.reload(); } }

    function tagsOf(a) { return a.instruments.length > 0 ? a.instruments.split(", ") : []; }

    readonly property var tagList: {
        const counts = {};
        for (const a of allAlbums) for (const t of tagsOf(a)) counts[t] = (counts[t] || 0) + 1;
        return Object.keys(counts).sort((x, y) => counts[y] - counts[x]).slice(0, 12);
    }

    readonly property var shown: {
        const q = query.trim().toLowerCase();
        let list = allAlbums.filter(a => {
            if (q.length > 0 && a.title.toLowerCase().indexOf(q) < 0 && a.artist.toLowerCase().indexOf(q) < 0) return false;
            if (hiResOnly && !a.hiRes) return false;
            const t = tagsOf(a);
            return activeTags.every(x => t.indexOf(x) >= 0);
        });
        const cmp = {
            title: (a, b) => a.title.localeCompare(b.title),
            artist: (a, b) => a.artist.localeCompare(b.artist) || a.title.localeCompare(b.title),
            year: (a, b) => (b.year - a.year) || a.title.localeCompare(b.title)
        }[sortKey];
        return list.slice().sort(cmp);
    }

    function toggleTag(t) {
        const i = activeTags.indexOf(t);
        activeTags = i >= 0 ? activeTags.filter(x => x !== t) : activeTags.concat([t]);
    }

    // ---- header
    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides; topMargin: Theme.pagePaddingTop }
        height: titleBlock.height + chips.height + 8

        Column {
            id: titleBlock
            anchors { left: parent.left; top: parent.top; topMargin: 4 }
            Text {
                text: "Albums"
                color: Theme.textPrimary
                font { family: Theme.fontDisplay; pixelSize: Theme.sizeTitle; weight: Font.Bold; letterSpacing: -0.56 }
            }
            Text {
                text: page.shown.length === page.allAlbums.length
                      ? page.allAlbums.length + " albums · " + Library.trackCount + " tracks"
                      : page.shown.length + " of " + page.allAlbums.length + " albums"
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
            }
        }
        SearchField {
            id: search
            objectName: "albumsSearch"
            anchors { right: parent.right; top: parent.top }
            width: 280
            placeholder: "Filter albums"
            onTextChanged: page.query = text
        }
        Item {
            id: chips
            anchors { left: parent.left; right: parent.right; top: titleBlock.bottom; topMargin: 12 }
            height: Theme.hitTarget
            Row {
                id: sortRow
                spacing: 8
                Repeater {
                    model: [{ k: "title", t: "A–Z" }, { k: "artist", t: "Artist" }, { k: "year", t: "Newest" }]
                    delegate: Chip { required property var modelData; text: modelData.t; selected: page.sortKey === modelData.k; onClicked: page.sortKey = modelData.k }
                }
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 1; height: 20; color: Theme.chipBorder }
                Chip { text: "Hi-res only"; selected: page.hiResOnly; onClicked: page.hiResOnly = !page.hiResOnly }
            }
            Flickable {
                anchors { left: sortRow.right; leftMargin: 24; right: sizeRow.left; rightMargin: 24; top: parent.top; bottom: parent.bottom }
                contentWidth: tagRow.width
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: tagRow
                    spacing: 8
                    Repeater {
                        model: page.tagList
                        delegate: Chip { required property string modelData; text: modelData; selected: page.activeTags.indexOf(modelData) >= 0; onClicked: page.toggleTag(modelData) }
                    }
                }
            }
            Row {
                id: sizeRow
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Cover size"
                    color: Theme.textTertiary
                    font { family: Theme.fontUi; pixelSize: Theme.sizeMeta }
                }
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "grid"; size: 14; color: Theme.textTertiary }
                LineSlider {
                    width: 120; trackHeight: 2
                    value: (prefs.coverSize - Theme.coverSizeMin) / (Theme.coverSizeMax - Theme.coverSizeMin)
                    onMoved: (v) => prefs.coverSize = Math.round(Theme.coverSizeMin + v * (Theme.coverSizeMax - Theme.coverSizeMin))
                }
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "square"; size: 18; color: Theme.textTertiary }
            }
        }
    }

    // ---- grid
    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides - 10; topMargin: 24 }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: cellHeight * 4   // four rows above and below are built ahead (fewer cards to create mid-scroll)
        readonly property int gap: 24
        readonly property int cols: Math.max(1, Math.floor((width - 10) / (prefs.coverSize + gap)))
        // Spread the leftover width evenly so the grid always fills the row.
        cellWidth: Math.floor((width - 10) / cols)
        cellHeight: prefs.coverSize + 56 + gap
        model: page.shown
        bottomMargin: Theme.pagePaddingBottom - 24
        delegate: Item {
            required property var modelData
            width: grid.cellWidth; height: grid.cellHeight
            AlbumCard {
                album: modelData
                cover: prefs.coverSize
                onClicked: page.albumClicked(modelData)
                onPlayClicked: Player.playTracks(Library.albumTracks(modelData.id), 0, modelData.title)
            }
        }
    }
    VScrollBar { flickable: grid; anchors { right: parent.right; rightMargin: 6; top: grid.top; bottom: grid.bottom } }

    Text {
        anchors.centerIn: grid
        visible: page.allAlbums.length === 0
        text: Library.available ? "No albums yet. Add a music folder under Settings." : "Library database unavailable."
        color: Theme.textTertiary
        font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
    }

    function focusSearch() { search.focusField(); }
}
