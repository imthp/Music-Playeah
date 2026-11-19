import QtQuick
import QtCore
import MusicPlayeah
import "Format.js" as Format

// Work page (M4b): one work and every recording of it you own. Each recording is the work's section of its album
// (cover, album, performer, year, the movements), not the whole album. Opened from "N recordings in your library"
// on an album, and from work rows in Search and Liked.
Item {
    id: page
    property var workId: 0
    property string backLabel: "Back"
    signal back()
    signal albumClicked(var album)
    signal artistClicked(string name)
    signal composerClicked(string name)
    signal searchRequested(string text)

    property var info: ({})
    property var sections: []            // [{rec, tracks}] in album order
    property bool dirty: true
    property string instrument: ""       // "" = every recording; else only albums tagged with this instrument

    // The sort is remembered; the instrument filter is per work (it resets when another work opens).
    Settings { id: prefs; category: "workPage"; property string sortKey: "performer" }
    readonly property var sorts: [
        { k: "performer", t: "A–Z" }, { k: "plays", t: "Most played" }, { k: "recent", t: "Recently played" },
        { k: "newest", t: "Newest" }, { k: "oldest", t: "Oldest" }, { k: "short", t: "Shortest" }, { k: "long", t: "Longest" }
    ]
    function compare(a, b) {
        const x = a.rec, y = b.rec;
        const byName = () => (x.albumArtist || "").localeCompare(y.albumArtist || "") || (x.albumTitle || "").localeCompare(y.albumTitle || "");
        // Unknown years go last whichever way the years are sorted.
        const year = (v, dir) => v.year > 0 ? dir * v.year : Infinity;
        switch (prefs.sortKey) {
        case "plays": return y.plays - x.plays || byName();
        case "recent": return y.lastPlayed - x.lastPlayed || y.plays - x.plays || byName();
        case "newest": return year(x, -1) - year(y, -1) || byName();
        case "oldest": return year(x, 1) - year(y, 1) || byName();
        case "short": return x.durationMs - y.durationMs || byName();
        case "long": return y.durationMs - x.durationMs || byName();
        }
        return byName();
    }
    function instrumentsOf(rec) { return (rec.instruments || "").length > 0 ? rec.instruments.split(", ") : []; }
    // Instruments that occur, most recordings first (only worth showing when they tell recordings apart).
    readonly property var instrumentList: {
        const counts = {};
        for (const s of sections) for (const i of instrumentsOf(s.rec)) counts[i] = (counts[i] || 0) + 1;
        const keys = Object.keys(counts).sort((a, b) => counts[b] - counts[a] || a.localeCompare(b));
        return keys.some(k => counts[k] < sections.length) ? keys : [];
    }
    readonly property var shownSections: sections
        .filter(s => instrument === "" || instrumentsOf(s.rec).indexOf(instrument) >= 0)
        .sort((a, b) => compare(a, b))

    readonly property var currentTrackId: Player.current.id || 0
    readonly property var playingWorkId: Player.current.workId || 0
    readonly property var playingAlbumId: Player.current.albumId || 0

    LikeState { id: like; kind: "work"; refId: page.workId }

    function invalidate() { dirty = true; if (visible) reload(); }
    function reload() {
        dirty = false;
        info = workId ? Library.work(workId) : ({});
        const out = [];
        if (workId) for (const r of Library.workRecordings(workId)) out.push({ rec: r, tracks: Library.workTracks(workId, r.albumId) });
        sections = out;
        instrument = "";
        list.positionViewAtBeginning();
    }
    onWorkIdChanged: invalidate()
    onVisibleChanged: if (visible && dirty) reload()
    Connections { target: Library; function onRevisionChanged() { page.invalidate(); } }

    // Rows: a recording (album section header), then its movements.
    readonly property var rows: {
        const out = [];
        shownSections.forEach((s, si) => {
            out.push({ kind: "rec", section: si, rec: s.rec, firstRow: si === 0 });
            s.tracks.forEach((t, i) => out.push({ kind: "track", section: si, index: i, track: t, last: i === s.tracks.length - 1 }));
        });
        return out;
    }

    function playSection(si, index) {
        const s = shownSections[si];
        if (s && s.tracks.length > 0) Player.playTracks(s.tracks, index || 0, info.title || "");
    }
    function trackNumber(t, i) { return t.movementNo > 0 ? Format.roman(t.movementNo) + "." : String(i + 1); }
    function trackTitle(t) { return (t.movementName || "").length > 0 ? t.movementName : t.title; }

    readonly property string subtitle: {
        const p = [];
        if (info.recordings) p.push(Format.plural(info.recordings, "recording") + " in your library");
        return p.join(" · ");
    }

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

        header: Column {
            width: list.width
            spacing: 8
            Item { width: 1; height: 8 }
            Text {
                text: "Work"
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
            }
            Text {
                width: parent.width
                text: page.info.title || (page.workId ? "" : "No work")
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                lineHeight: 1.05
                color: Theme.textPrimary
                font { family: Theme.fontDisplay; pixelSize: 40; weight: Font.Bold; letterSpacing: -1 }
            }
            // catalog · composer (link) · N recordings
            Row {
                width: parent.width
                spacing: 0
                Text {
                    visible: (page.info.catalog || "").length > 0
                    text: (page.info.catalog || "") + ((page.info.composer || "").length > 0 ? " · " : "")
                    color: Theme.textSecondary
                    font { family: Theme.fontUi; pixelSize: 16; weight: Font.Medium }
                }
                LinkText {
                    visible: (page.info.composer || "").length > 0
                    text: page.info.composer || ""
                    maxWidth: parent.width * 0.5
                    onClicked: page.composerClicked(page.info.composer)
                }
                Text {
                    visible: page.subtitle.length > 0
                    text: " · " + page.subtitle
                    color: Theme.textSecondary
                    font { family: Theme.fontUi; pixelSize: 16; weight: Font.Medium }
                }
            }
            Row {
                topPadding: 6
                spacing: 10
                PillButton { text: "Play"; icon: "play"; primary: true; enabledAction: page.shownSections.length > 0; onClicked: page.playSection(0, 0) }
                PillButton {
                    icon: "heart"
                    enabledAction: page.workId ? true : false
                    iconFilled: like.liked
                    iconColor: like.liked ? Theme.accent : Theme.textPrimary
                    onClicked: like.toggle()
                }
            }
            // sort chips, then instrument chips (guitar / lute / violin ... versions of the same work)
            Flow {
                width: parent.width
                visible: page.sections.length > 1
                topPadding: 10
                spacing: 8
                Repeater {
                    model: page.sorts
                    delegate: Chip { required property var modelData; text: modelData.t; selected: prefs.sortKey === modelData.k; onClicked: prefs.sortKey = modelData.k }
                }
                Rectangle { visible: page.instrumentList.length > 0; width: 1; height: 20; y: 12; color: Theme.chipBorder }
                Repeater {
                    model: page.instrumentList
                    delegate: Chip {
                        required property string modelData
                        text: Format.capitalize(modelData)
                        selected: page.instrument === modelData
                        onClicked: page.instrument = page.instrument === modelData ? "" : modelData
                    }
                }
            }
            Item { width: 1; height: 8 }
        }

        delegate: Item {
            id: cell
            required property var modelData
            readonly property string kind: modelData.kind || ""
            readonly property bool isCurrentRec: kind === "rec" && page.playingWorkId === page.workId && page.playingAlbumId === modelData.rec.albumId
            width: list.width
            height: kind === "rec" ? (modelData.firstRow ? 0 : 20) + 72 : Theme.hitTarget + (modelData.last ? 8 : 0)

            // ---- recording: the album section's header
            Item {
                visible: cell.kind === "rec"
                y: cell.modelData.firstRow ? 0 : 20
                width: parent.width; height: 72
                Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 8; rightMargin: 8 } height: 1; color: Theme.borderSubtle }
                Rectangle {
                    id: recCover
                    x: 8; anchors.verticalCenter: parent.verticalCenter
                    width: 52; height: 52; radius: 6; color: Theme.surfaceCard
                    RoundedImage {
                        anchors.fill: parent; radius: 6
                        visible: cell.kind === "rec" && (cell.modelData.rec.artHash || "").length > 0
                        source: visible ? "image://art/" + cell.modelData.rec.artHash + "/256" : ""
                        sourceSize: Qt.size(104, 104)
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.albumClicked({ id: cell.modelData.rec.albumId, workId: page.workId }) }
                }
                Column {
                    anchors { left: recCover.right; leftMargin: 14; right: recTime.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    spacing: 3
                    LinkText {
                        size: Theme.sizeBody + 1
                        maxWidth: parent.width
                        text: cell.kind === "rec" ? cell.modelData.rec.albumArtist : ""
                        color: cell.isCurrentRec ? Theme.accent : Theme.textPrimary
                        onClicked: page.artistClicked(cell.modelData.rec.albumArtist)
                    }
                    Text {
                        id: albumLine
                        width: Math.min(implicitWidth, parent.width)
                        text: cell.kind === "rec" ? cell.modelData.rec.albumTitle + (cell.modelData.rec.year > 0 ? " · " + cell.modelData.rec.year : "") : ""
                        elide: Text.ElideRight; maximumLineCount: 1
                        color: albumMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
                        MouseArea { id: albumMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.albumClicked({ id: cell.modelData.rec.albumId, workId: page.workId }) }
                    }
                }
                Text {
                    id: recTime
                    anchors { right: recPlay.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    text: cell.kind === "rec" ? Format.time(cell.modelData.rec.durationMs) : ""
                    color: Theme.textSecondary
                    font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
                }
                Rectangle {
                    id: recPlay
                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    width: 36; height: 36; radius: 18
                    color: recPlayMouse.containsMouse ? Theme.textNav : Theme.textPrimary
                    Icon {
                        anchors.centerIn: parent; anchors.horizontalCenterOffset: cell.isCurrentRec && Player.playing ? 0 : 1
                        name: cell.isCurrentRec && Player.playing ? "pause" : "play"; size: 16; filled: true; color: Theme.bg
                    }
                    MouseArea {
                        id: recPlayMouse
                        anchors { fill: parent; margins: -4 }
                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: cell.isCurrentRec ? Player.playPause() : page.playSection(cell.modelData.section, 0)
                    }
                }
            }

            // ---- a movement of that recording
            TrackRow {
                visible: cell.kind === "track"
                x: 8; width: parent.width - 16
                indent: true
                trackId: cell.kind === "track" ? cell.modelData.track.id : 0
                number: cell.kind === "track" ? page.trackNumber(cell.modelData.track, cell.modelData.index) : ""
                title: cell.kind === "track" ? page.trackTitle(cell.modelData.track) : ""
                plays: cell.kind === "track" ? (cell.modelData.track.plays || 0) : 0
                time: cell.kind === "track" ? Format.time(cell.modelData.track.durationMs) : ""
                current: cell.kind === "track" && cell.modelData.track.id === page.currentTrackId
                playing: Player.playing
                onPlay: page.playSection(cell.modelData.section, cell.modelData.index)
            }
        }

        footer: Item {
            width: list.width; height: emptyText.visible ? 80 : 0
            Text {
                id: emptyText
                visible: page.workId && page.shownSections.length === 0
                anchors { left: parent.left; top: parent.top; topMargin: 24 }
                text: page.sections.length > 0 ? "No recordings with this instrument." : "No recordings of this work are in your library right now."
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
            }
        }
    }
    SmoothScroll { anchors.fill: list; flickable: list }
    VScrollBar { flickable: list; anchors { right: parent.right; rightMargin: 6; top: list.top; bottom: list.bottom } }
}
