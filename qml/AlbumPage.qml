import QtQuick
import QtQuick.Effects
import MusicPlayeah

// Album detail (M4-Album). Header (cover, title, performer · composer, meta + format, instrument chips, actions),
// then the tracks. Classical albums: consecutive movements of one work are grouped under a WorkHeader (SPEC §7b);
// everything else is plain TrackRows. Several discs get a "Disc N" line.
// The whole page is one ListView (header = album header), so long box sets stay virtualized.
Item {
    id: page
    property var albumId: 0
    property string backLabel: "Albums"
    signal back()
    signal tagClicked(string tag)
    signal artistClicked(string name)
    signal composerClicked(string name)

    property var info: ({})
    property var tracks: []
    property var rows: []

    function reload() {
        if (!albumId) { info = {}; tracks = []; rows = []; return; }
        info = Library.album(albumId);
        tracks = Library.albumTracks(albumId);
        rows = buildRows(tracks);
        list.positionViewAtBeginning();
    }
    onAlbumIdChanged: reload()
    Connections { target: Library; function onRevisionChanged() { page.reload(); } }

    // ---------------------------------------------------------------- formatting

    function time(ms) {
        const s = Math.round(ms / 1000);
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60;
        return (h > 0 ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(ss).padStart(2, "0");
    }
    function longTime(ms) {
        const min = Math.round(ms / 60000);
        return min >= 60 ? Math.floor(min / 60) + " h " + (min % 60) + " min" : min + " min";
    }
    function roman(n) {
        const t = [[10, "X"], [9, "IX"], [5, "V"], [4, "IV"], [1, "I"]];
        let out = "";
        if (n >= 40) return String(n);
        for (const [v, r] of t) while (n >= v) { out += r; n -= v; }
        return out;
    }
    // "Suite in E minor, BWV 996" + "BWV 996" -> "Suite in E minor" (the catalog is shown beside it).
    function stripCatalog(title, catalog) {
        if (!catalog) return title;
        const i = title.lastIndexOf(catalog);
        if (i <= 0) return title;
        return (title.slice(0, i) + title.slice(i + catalog.length)).replace(/[\s,;:–-]+$/, "").replace(/\s*,\s*,/, ",").trim();
    }
    function plural(n, word) { return n + " " + word + (n === 1 ? "" : "s"); }

    // ---------------------------------------------------------------- rows

    // Tracks -> rows: {kind: "disc"|"work"|"track", ...}. A work header only when ≥ 2 consecutive tracks share it.
    function buildRows(list) {
        const out = [];
        const discs = new Set(list.map(t => t.discNo || 1));
        let lastDisc = -1;
        let i = 0;
        while (i < list.length) {
            const t = list[i];
            const disc = t.discNo || 1;
            if (discs.size > 1 && disc !== lastDisc) {
                out.push({ kind: "disc", label: "Disc " + disc });
                lastDisc = disc;
            }
            let j = i + 1;
            if (t.workId) {
                while (j < list.length && list[j].workId === t.workId && (list[j].discNo || 1) === disc) ++j;
            }
            if (t.workId && j - i >= 2) {
                const group = list.slice(i, j);
                const total = group.reduce((a, g) => a + g.durationMs, 0);
                let meta = plural(group.length, "movement") + " · " + time(total);
                if (t.workRecordings > 1) meta += " · " + t.workRecordings + " recordings in your library";
                out.push({ kind: "work", workId: t.workId, title: stripCatalog(t.workTitle, t.catalog), catalog: t.catalog,
                           meta: meta, first: i, count: j - i, ids: group.map(g => g.id), firstRow: out.length === 0 });
                group.forEach((g, k) => out.push({
                    kind: "track", index: i + k, track: g, indent: true, last: k === group.length - 1,
                    number: g.movementNo > 0 ? roman(g.movementNo) + "." : String(k + 1),
                    title: g.movementName.length > 0 ? g.movementName : g.title
                }));
            } else {
                for (let k = i; k < j; ++k) {
                    const g = list[k];
                    out.push({ kind: "track", index: k, track: g, indent: false, last: false,
                               number: String(g.trackNo > 0 ? g.trackNo : k + 1), title: g.title });
                }
            }
            i = j;
        }
        return out;
    }

    function playFrom(index) { Player.playTracks(tracks, index, info.title); }
    function playWork(first, count) { Player.playTracks(tracks.slice(first, first + count), 0, info.title); }
    // Shuffle works: whole works (and single tracks) in random order, movements kept in order (SPEC §7b).
    function shuffleWorks() {
        const units = [];
        for (const r of rows) {
            if (r.kind === "work") units.push(tracks.slice(r.first, r.first + r.count));
            else if (r.kind === "track" && !r.indent) units.push([tracks[r.index]]);
        }
        for (let i = units.length - 1; i > 0; --i) {
            const j = Math.floor(Math.random() * (i + 1));
            [units[i], units[j]] = [units[j], units[i]];
        }
        Player.playTracks([].concat(...units), 0, info.title);
    }

    readonly property var currentTrackId: Player.current.id || 0
    readonly property bool hasWorks: rows.some(r => r.kind === "work")

    // ---------------------------------------------------------------- top bar: back + search

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
        SearchField { anchors { right: parent.right; verticalCenter: parent.verticalCenter } width: 300; placeholder: "Search albums, artists, songs, works" }
    }

    // ---------------------------------------------------------------- list

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: parent.bottom; leftMargin: Theme.pagePaddingSides; rightMargin: Theme.pagePaddingSides }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.rows
        bottomMargin: Theme.pagePaddingBottom
        cacheBuffer: 800

        header: Column {
            width: list.width
            Item { width: 1; height: 12 }

            // ---- album header: cover left, text bottom-aligned beside it
            Item {
                width: parent.width
                height: Math.max(cover.height, infoCol.implicitHeight)

                RectangularShadow {
                    anchors.fill: cover
                    offset.y: 16
                    blur: 48
                    radius: 10
                    color: "#8C000000"
                }
                Rectangle {
                    id: cover
                    anchors.bottom: parent.bottom
                    width: 220; height: 220; radius: 10
                    color: Theme.surfaceCard
                    RoundedImage {
                        anchors.fill: parent
                        radius: 10
                        visible: (page.info.artHash || "").length > 0
                        source: visible ? "image://art/" + page.info.artHash + "/600" : ""
                        sourceSize: Qt.size(440, 440)
                    }
                    Icon { anchors.centerIn: parent; visible: !(page.info.artHash || "").length; name: "albums"; size: 56; color: Theme.heartIdle }
                }

                Column {
                    id: infoCol
                    anchors { left: cover.right; leftMargin: 32; right: parent.right; bottom: parent.bottom }
                    spacing: 8

                    Text {
                        text: "Album"
                        color: Theme.textTertiary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
                    }
                    Text {
                        width: parent.width
                        text: page.info.title || ""
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                        lineHeight: 1.05
                        font { family: Theme.fontDisplay; pixelSize: Theme.sizeAlbumTitle; weight: Font.Bold; letterSpacing: -1.1 }
                    }
                    // Performer · composed by Composer (each a link)
                    Row {
                        width: parent.width
                        spacing: 0
                        readonly property string composer: (page.info.composers || [])[0] || ""
                        readonly property bool showComposer: composer.length > 0 && composer !== (page.info.artist || "")
                        LinkText {
                            text: page.info.artist || ""
                            maxWidth: parent.width * (parent.showComposer ? 0.5 : 1)
                            onClicked: page.artistClicked(page.info.artist)
                        }
                        Text {
                            visible: parent.showComposer
                            text: " · composed by "
                            color: Theme.textTertiary
                            font { family: Theme.fontUi; pixelSize: 16; weight: Font.Medium }
                        }
                        LinkText {
                            visible: parent.showComposer
                            text: parent.composer
                            maxWidth: parent.width * 0.45
                            onClicked: page.composerClicked(parent.composer)
                        }
                    }
                    // meta + format line
                    Row {
                        spacing: 14
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                const p = [];
                                if (page.info.year) p.push(page.info.year);
                                if (page.info.genre) p.push(page.info.genre);
                                if (page.hasWorks) p.push(page.plural(page.info.works || 0, "work"));
                                p.push(page.plural(page.info.tracks || 0, "track"));
                                p.push(page.longTime(page.info.durationMs || 0));
                                return p.join(" · ");
                            }
                            color: Theme.textSecondary
                            font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
                        }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: (page.info.format || "").length > 0
                            spacing: 5
                            Icon { anchors.verticalCenter: parent.verticalCenter; name: "file"; size: 12; strokeWidth: 2; color: Theme.textTertiary }
                            Text {
                                text: page.info.format || ""
                                color: Theme.textTertiary
                                font { family: Theme.fontUi; pixelSize: Theme.sizeMeta; features: ({ "tnum": 1 }) }
                            }
                        }
                    }
                    // instrument chips (click: Albums filtered by that instrument)
                    Flow {
                        width: parent.width
                        visible: (page.info.instruments || "").length > 0
                        spacing: 8
                        Repeater {
                            model: (page.info.instruments || "").length > 0 ? page.info.instruments.split(", ") : []
                            delegate: Chip { required property string modelData; text: modelData; onClicked: page.tagClicked(modelData) }
                        }
                    }
                    // actions
                    Row {
                        topPadding: 4
                        spacing: 10
                        PillButton { text: "Play"; icon: "play"; primary: true; onClicked: page.playFrom(0) }
                        PillButton { text: page.hasWorks ? "Shuffle works" : "Shuffle"; icon: "shuffle"; onClicked: page.shuffleWorks() }
                        PillButton { icon: "heart"; enabledAction: false }      // likes: 4b
                        PillButton { icon: "addQueue"; enabledAction: false }   // queue: M5
                        PillButton { icon: "more"; enabledAction: false }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 6
                            visible: (page.info.plays || 0) > 0
                            text: "Played " + page.plural(page.info.plays || 0, "time")
                            color: Theme.textTertiary
                            font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
                        }
                    }
                }
            }

            // ---- column header
            Item { width: 1; height: 24 }
            Item {
                width: parent.width; height: 28
                Text { x: 12; text: "#"; color: Theme.textTertiary; font { family: Theme.fontUi; pixelSize: Theme.sizeMeta } }
                Text { x: 64; text: "Title"; color: Theme.textTertiary; font { family: Theme.fontUi; pixelSize: Theme.sizeMeta } }
                Text { anchors.right: parent.right; anchors.rightMargin: 72; width: 80; horizontalAlignment: Text.AlignRight; text: "Plays"; color: Theme.textTertiary; font { family: Theme.fontUi; pixelSize: Theme.sizeMeta } }
                Text { anchors.right: parent.right; width: 72; rightPadding: 16; horizontalAlignment: Text.AlignRight; text: "Time"; color: Theme.textTertiary; font { family: Theme.fontUi; pixelSize: Theme.sizeMeta } }
                Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: Theme.borderSubtle }
            }
            Item { width: 1; height: 8 }
        }

        // A work reads as one block: header, then its movements indented under it, with space above and below
        // (no panel behind it: tried, too boxy).
        delegate: Item {
            id: cell
            required property var modelData
            readonly property string kind: modelData.kind || ""
            readonly property var t: kind === "track" ? (modelData.track || null) : null
            readonly property bool inWork: kind === "track" && (modelData.indent || false)
            readonly property bool lastInWork: inWork && (modelData.last || false)
            readonly property int gapAbove: kind === "work" && !modelData.firstRow ? 16 : 0
            readonly property int padBelow: 0
            readonly property int gapBelow: lastInWork ? 16 : 0
            width: list.width
            height: kind === "work" ? gapAbove + 64
                  : kind === "disc" ? 52
                  : Theme.hitTarget + padBelow + gapBelow

            WorkHeader {
                visible: cell.kind === "work"
                x: 0; y: cell.gapAbove
                width: parent.width
                title: cell.modelData.title || ""
                catalog: cell.modelData.catalog || ""
                meta: cell.modelData.meta || ""
                current: (cell.modelData.ids || []).indexOf(page.currentTrackId) >= 0
                onPlay: page.playWork(cell.modelData.first, cell.modelData.count)
            }
            Text {
                visible: cell.kind === "disc"
                anchors { left: parent.left; leftMargin: 12; bottom: parent.bottom; bottomMargin: 8 }
                text: cell.modelData.label || ""
                color: Theme.textSecondary
                font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
            }
            TrackRow {
                visible: cell.t !== null
                x: cell.inWork ? 8 : 0; y: 0
                width: parent.width - (cell.inWork ? 16 : 0)
                number: cell.modelData.number || ""
                title: cell.modelData.title || ""
                indent: cell.inWork
                plays: cell.t ? cell.t.plays : 0
                time: cell.t ? page.time(cell.t.durationMs) : ""
                current: cell.t !== null && cell.t.id === page.currentTrackId
                playing: Player.playing
                onPlay: page.playFrom(cell.modelData.index)
            }
        }
    }
    VScrollBar { flickable: list; anchors { right: parent.right; rightMargin: 6; top: list.top; bottom: list.bottom } }
}
