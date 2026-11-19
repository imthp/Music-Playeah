import QtQuick
import MusicPlayeah
import "Format.js" as Format

// Search page (M4-Search, SPEC §10 Search): Ctrl K or the field on any page. The index lives in memory, so every key
// press answers at once. Chips narrow to one kind; "All" shows the top result card, then Songs, Albums, Works,
// Artists, Composers. Enter opens (or plays) the top result. Esc goes back.
Item {
    id: page
    property string query: ""             // set by Main; edits go out through queryEdited
    property int focusTick: 0             // Main bumps this to focus the field (Ctrl K)
    signal back()
    signal queryEdited(string text)
    signal albumClicked(var album)
    signal artistClicked(string name)
    signal composerClicked(string name)
    signal workClicked(var workId)

    property string kindFilter: ""        // "" = All, else song | album | artist | composer | work
    property var result: ({})
    property var songs: []                // track maps for the song hits (SongRow wants those)
    property var totals: ({})             // counts per kind for the chips (from the All search)

    readonly property int cover: 128
    readonly property int gap: 20
    readonly property int listWidth: Math.max(1, list.width - 10)
    readonly property int cols: Math.max(1, Math.floor((listWidth + gap) / (cover + gap)))
    readonly property real cellWidth: Math.floor(listWidth / cols)
    readonly property var currentTrackId: Player.current.id || 0
    readonly property bool hasQuery: query.trim().length > 0

    function run() {
        if (!visible) { dirty = true; return; }
        dirty = false;
        if (!hasQuery) { result = ({}); songs = []; totals = ({}); return; }
        const all = Library.search(query, "");
        totals = all.totals;
        result = kindFilter === "" ? all : Library.search(query, kindFilter);
        const ids = (result.song || []).map(i => i.id);
        songs = ids.length > 0 ? Library.tracksByIds(ids) : [];
    }
    property bool dirty: true
    onQueryChanged: { if (field.text !== query) field.text = query; run(); list.positionViewAtBeginning(); }
    onKindFilterChanged: { run(); list.positionViewAtBeginning(); }
    onFocusTickChanged: field.focusEnd()
    Component.onCompleted: { field.text = query; run(); }
    onVisibleChanged: if (visible) { if (dirty) run(); field.focusEnd(); }
    Connections { target: Library; function onRevisionChanged() { page.run(); } }

    readonly property var chips: {
        const t = totals;
        const n = k => (t[k] || 0);
        const sum = n("song") + n("album") + n("artist") + n("composer") + n("work");
        return [
            { key: "", label: "All" },
            { key: "song", label: "Songs" + (hasQuery ? " " + n("song") : "") },
            { key: "album", label: "Albums" + (hasQuery ? " " + n("album") : "") },
            { key: "artist", label: "Artists" + (hasQuery ? " " + n("artist") : "") },
            { key: "composer", label: "Composers" + (hasQuery ? " " + n("composer") : "") },
            { key: "work", label: "Works" + (hasQuery ? " " + n("work") : "") }
        ];
    }

    // ---------------------------------------------------------------- actions

    function open(item) {
        if (!item) return;
        switch (item.kind) {
        case "song": {
            const t = Library.tracksByIds([item.id]);
            if (t.length > 0) Player.playTracks(t, 0, item.title);
            break;
        }
        case "album": albumClicked({ id: item.id }); break;
        case "artist": artistClicked(item.title); break;
        case "composer": composerClicked(item.title); break;
        case "work": workClicked(item.id); break;
        }
    }
    function playWork(item) {
        const t = Library.tracksOfWorks([item.id]);
        if (t.length > 0) Player.playTracks(t, 0, item.title);
    }
    readonly property var topItem: {
        if (kindFilter === "") return result.top || null;
        const l = result[kindFilter] || [];
        return l.length > 0 ? l[0] : null;
    }

    // A search hit as the map AlbumCard expects.
    function albumCard(a) { return { id: a.id, title: a.title, artist: a.subtitle, artHash: a.artHash }; }

    function typeLabel(kind) {
        return { song: "Song", album: "Album", artist: "Artist", composer: "Composer", work: "Work" }[kind] || "";
    }

    // ---------------------------------------------------------------- rows

    readonly property var rows: {
        const out = [];
        if (!hasQuery) return out;
        const r = result;
        const addHeading = (title, more) => out.push({ kind: "heading", title: title, more: more || "" });
        if (kindFilter === "") {
            if (r.top) out.push({ kind: "top", item: r.top });
            const t = totals;
            if ((r.song || []).length > 0) {
                addHeading("Songs", t.song > r.song.length ? "Show all " + t.song : "");
                songs.forEach((s, i) => out.push({ kind: "song", index: i, track: s }));
            }
            if ((r.album || []).length > 0) {
                addHeading("Albums", t.album > r.album.length ? "Show all " + t.album : "");
                Format.chunk(r.album.map(albumCard), cols).forEach(c => out.push({ kind: "albums", albums: c }));
            }
            if ((r.work || []).length > 0) {
                addHeading("Works", t.work > r.work.length ? "Show all " + t.work : "");
                r.work.forEach(w => out.push({ kind: "work", item: w }));
            }
            if ((r.artist || []).length > 0) {
                addHeading("Artists", t.artist > r.artist.length ? "Show all " + t.artist : "");
                r.artist.forEach(a => out.push({ kind: "person", item: a }));
            }
            if ((r.composer || []).length > 0) {
                addHeading("Composers", t.composer > r.composer.length ? "Show all " + t.composer : "");
                r.composer.forEach(a => out.push({ kind: "person", item: a }));
            }
        } else if (kindFilter === "song") {
            songs.forEach((s, i) => out.push({ kind: "song", index: i, track: s }));
        } else if (kindFilter === "album") {
            Format.chunk((r.album || []).map(albumCard), cols).forEach(c => out.push({ kind: "albums", albums: c }));
        } else if (kindFilter === "work") {
            (r.work || []).forEach(w => out.push({ kind: "work", item: w }));
        } else {
            (r[kindFilter] || []).forEach(a => out.push({ kind: "person", item: a }));
        }
        return out;
    }
    function showAll(title) {
        const k = { Songs: "song", Albums: "album", Works: "work", Artists: "artist", Composers: "composer" }[title];
        if (k) kindFilter = k;
    }

    // ---------------------------------------------------------------- layout

    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides; topMargin: Theme.pagePaddingTop }
        height: Theme.hitTarget + 8 + Theme.hitTarget + 8

        Row {
            id: backLink
            anchors { left: parent.left; verticalCenter: field.verticalCenter }
            spacing: 6
            Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevronLeft"; size: 16; color: backMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Back"
                color: backMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1; weight: Font.Medium }
            }
        }
        MouseArea { id: backMouse; anchors { fill: backLink; margins: -10 } hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.back() }

        SearchField {
            id: field
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top }
            width: Math.min(560, parent.width - 2 * (backLink.width + 40))
            placeholder: "Search your library"
            onTextChanged: if (text !== page.query) page.queryEdited(text)
            onAccepted: { page.open(page.topItem); release(); }
        }

        Flow {
            anchors { left: parent.left; right: parent.right; top: field.bottom; topMargin: 8 }
            spacing: 8
            Repeater {
                model: page.chips
                delegate: Chip {
                    required property var modelData
                    text: modelData.label
                    selected: page.kindFilter === modelData.key
                    onClicked: page.kindFilter = modelData.key
                }
            }
        }
    }

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: Theme.pagePaddingBottom
        cacheBuffer: 1200
        reuseItems: true          // rows scrolled out are reused for rows scrolling in
        model: page.rows

        delegate: Item {
            id: cell
            required property var modelData
            readonly property string kind: modelData.kind || ""
            width: list.width
            height: kind === "heading" ? 52 : kind === "top" ? 124 + 16 : kind === "song" || kind === "work" ? 52
                  : kind === "person" ? 60 : page.cover + 56 + page.gap

            Loader {
                anchors.fill: parent
                sourceComponent: cell.kind === "heading" ? headingC : cell.kind === "top" ? topC : cell.kind === "song" ? songC
                               : cell.kind === "albums" ? albumsC : cell.kind === "work" ? workC : personC
            }
            Component {
                id: headingC
                Item {
                    Text {
                        anchors { left: parent.left; bottom: parent.bottom; bottomMargin: 8 }
                        text: cell.modelData.title
                        color: Theme.textPrimary
                        font { family: Theme.fontDisplay; pixelSize: Theme.sizeSection; weight: Font.Bold; letterSpacing: -0.2 }
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 16; bottom: parent.bottom; bottomMargin: 10 }
                        visible: text.length > 0
                        text: cell.modelData.more
                        color: moreMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
                        MouseArea { id: moreMouse; anchors { fill: parent; margins: -8 } hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.showAll(cell.modelData.title) }
                    }
                }
            }
            Component {
                id: topC
                Item {
                    readonly property var item: cell.modelData.item
                    Rectangle {
                        id: card
                        width: Math.min(380, parent.width); height: 124
                        radius: 12
                        color: topMouse.containsMouse ? Theme.surfaceRaised : Theme.surfaceCard
                        MouseArea { id: topMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.open(parent.parent.item) }
                        Item {
                            id: art
                            x: 16; anchors.verticalCenter: parent.verticalCenter
                            width: 92; height: 92
                            Avatar {
                                visible: item.kind === "artist" || item.kind === "composer"
                                anchors.fill: parent; diameter: 92; name: item.title
                            }
                            Rectangle {
                                visible: item.kind !== "artist" && item.kind !== "composer"
                                anchors.fill: parent; radius: 6; color: Theme.bg
                                RoundedImage {
                                    anchors.fill: parent; radius: 6
                                    visible: (item.artHash || "").length > 0
                                    source: visible ? "image://art/" + item.artHash + "/256" : ""
                                    sourceSize: Qt.size(184, 184)
                                }
                                Icon { visible: (item.artHash || "").length === 0; anchors.centerIn: parent; name: "songs"; size: 28; color: Theme.textTertiary }
                            }
                        }
                        Column {
                            anchors { left: art.right; leftMargin: 16; right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 4
                            Text {
                                text: "Top result · " + page.typeLabel(item.kind)
                                color: Theme.textTertiary
                                font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
                            }
                            Text {
                                width: parent.width
                                text: item.title
                                elide: Text.ElideRight; maximumLineCount: 1
                                color: Theme.textPrimary
                                font { family: Theme.fontDisplay; pixelSize: 20; weight: Font.Bold }
                            }
                            Text {
                                width: parent.width
                                text: page.describe(item)
                                elide: Text.ElideRight; maximumLineCount: 1
                                color: Theme.textSecondary
                                font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
                            }
                        }
                    }
                }
            }
            Component {
                id: songC
                SongRow {
                    track: cell.modelData.track
                    current: cell.modelData.track.id === page.currentTrackId
                    playing: Player.playing
                    extraWidth: 0
                    onPlay: Player.playTracks([cell.modelData.track], 0, cell.modelData.track.title)
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
            Component {
                id: workC
                WorkRow {
                    work: ({ id: cell.modelData.item.id, title: cell.modelData.item.title, catalog: cell.modelData.item.detail })
                    meta: page.describe(cell.modelData.item)
                    onPlay: page.playWork(cell.modelData.item)
                    onClicked: page.open(cell.modelData.item)
                }
            }
            Component {
                id: personC
                PersonRow {
                    kind: cell.modelData.item.kind
                    personId: cell.modelData.item.id
                    name: cell.modelData.item.title
                    meta: page.describe(cell.modelData.item)
                    onClicked: page.open(cell.modelData.item)
                }
            }
        }

        footer: Item {
            width: list.width; height: hint.visible ? 120 : 0
            Text {
                id: hint
                visible: page.rows.length === 0
                anchors { left: parent.left; top: parent.top; topMargin: 24 }
                text: page.hasQuery ? "Nothing matches “" + page.query.trim() + "”." : "Type to search your songs, albums, artists, composers and works."
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
            }
        }
    }
    SmoothScroll { anchors.fill: list; flickable: list }
    VScrollBar { flickable: list; anchors { right: parent.right; rightMargin: 6; top: list.top; bottom: list.bottom } }

    // Second line for a hit: "Artist · Album", "3 albums · 31 tracks", "Bach · 12 recordings"...
    function describe(item) {
        switch (item.kind) {
        case "song": return [item.subtitle, item.detail].filter(s => s).join(" · ");
        case "album": return [item.subtitle, item.count1 > 0 ? Format.plural(item.count1, "track") : ""].filter(s => s).join(" · ");
        case "artist": return [Format.plural(item.count1, "album"), Format.plural(item.count2, "track")].join(" · ");
        case "composer": return [Format.plural(item.count1, "work"), Format.plural(item.count2, "recording")].join(" · ");
        case "work": return [item.subtitle, Format.plural(item.count1, "recording")].filter(s => s).join(" · ");
        }
        return "";
    }
}
