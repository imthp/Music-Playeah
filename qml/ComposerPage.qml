import QtQuick
import MusicPlayeah
import "Format.js" as Format

// Composer page (M4-Composer, SPEC §8c, D38/D39): header (avatar, counts, Play works / Shuffle works / heart), filter
// chips (form · instrument), then the composer's works grouped by family with the catalog range beside it. A work row
// opens to every recording you own (cover, performer, album, year, length, play).
// One virtualized list of rows: family headings, work rows, and the recordings of the work that is open.
// A composer without detected works shows their albums instead.
Item {
    id: page
    property string name: ""
    property string backLabel: "Back"
    property var focusWorkId: 0          // open (and scroll to) this work when the page opens
    signal back()
    signal albumClicked(var album)
    signal artistClicked(string name)
    signal searchRequested(string text)

    property var person: ({})
    property var families: []            // from Library.composerWorks()
    property var albumsFallback: []      // when there are no works
    property var openWorks: ({})         // work id -> true while its recordings are shown
    property var recordings: ({})        // work id -> Library.workRecordings() (loaded when first opened)
    property string formFilter: ""       // "" = all works
    property string instrumentFilter: "" // "" = any
    property bool dirty: true

    readonly property int cover: 180
    readonly property int gap: 24
    readonly property int listWidth: Math.max(1, list.width - 10)
    readonly property int cols: Math.max(1, Math.floor((listWidth + gap) / (cover + gap)))
    readonly property real cellWidth: Math.floor(listWidth / cols)

    LikeState { id: like; kind: "composer"; refId: page.person.id || 0 }

    // ---------------------------------------------------------------- data

    // A change while the page is hidden only marks it; it reloads when shown (hidden pages cost nothing).
    function invalidate() { dirty = true; if (visible) reload(); }
    function reload() {
        dirty = false;
        person = name.length > 0 ? Library.person("composer", name) : ({});
        families = (person.id || 0) ? Library.composerWorks(name) : [];
        albumsFallback = (person.id || 0) && families.length === 0 ? Library.composerAlbums(name) : [];
        recordings = ({});
        openWorks = ({});
        if (focusWorkId) {
            const o = ({}); o[focusWorkId] = true;
            openWorks = o;
            loadRecordings(focusWorkId);
        }
        formFilter = "";
        instrumentFilter = "";
        list.positionViewAtBeginning();
        if (focusWorkId) Qt.callLater(scrollToFocus);
    }
    onNameChanged: invalidate()
    onFocusWorkIdChanged: invalidate()
    onVisibleChanged: if (visible && dirty) reload()
    Connections { target: Library; function onRevisionChanged() { page.invalidate(); } }

    function loadRecordings(workId) {
        if (recordings[workId] === undefined) {
            const r = Object.assign({}, recordings);
            r[workId] = Library.workRecordings(workId);
            recordings = r;
        }
    }
    // The rows are a JS array: assigning a new one resets the ListView to the top. Changes made in place on the
    // page (open a work) keep the scroll position.
    function keepScroll(change) {
        const y = list.contentY;
        change();
        list.forceLayout();
        list.contentY = y;
        list.returnToBounds();
    }
    function toggleWork(workId) {
        keepScroll(() => {
            const o = Object.assign({}, openWorks);
            if (o[workId]) delete o[workId]; else { o[workId] = true; loadRecordings(workId); }
            openWorks = o;
        });
    }
    function scrollToFocus() {
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].kind === "work" && rows[i].work.id === focusWorkId) { list.positionViewAtIndex(i, ListView.Center); return; }
        }
    }

    // ---------------------------------------------------------------- filters

    readonly property var allWorks: {
        const out = [];
        for (const f of families) for (const w of f.works) out.push(w);
        return out;
    }
    // Chips: forms and instruments that occur, most works first.
    function topValues(field, max) {
        const counts = {};
        for (const w of allWorks) if (w[field]) counts[w[field]] = (counts[w[field]] || 0) + 1;
        return Object.keys(counts).sort((a, b) => counts[b] - counts[a] || a.localeCompare(b)).slice(0, max);
    }
    readonly property var forms: topValues("form", 6)
    readonly property var instruments: topValues("instrument", 6)

    function matches(w) {
        return (formFilter === "" || w.form === formFilter) && (instrumentFilter === "" || w.instrument === instrumentFilter);
    }
    readonly property var shownWorks: allWorks.filter(w => matches(w))

    // Rows: family heading / work / recording. Works that stand alone (no family name) share "Other works" at the end.
    readonly property var rows: {
        const out = [];
        const solo = [];
        const addWork = w => {
            out.push({ kind: "work", work: w, open: openWorks[w.id] === true });
            if (openWorks[w.id] === true) {
                const recs = recordings[w.id] || [];
                recs.forEach(r => out.push({ kind: "recording", work: w, rec: r }));
            }
        };
        for (const f of families) {
            const works = f.works.filter(w => matches(w));
            if (works.length === 0) continue;
            if (f.label.length === 0) { solo.push(...works); continue; }
            out.push({ kind: "family", label: f.label, range: f.catalogRange });
            works.forEach(addWork);
        }
        if (solo.length > 0) {
            out.push({ kind: "family", label: "Other works", range: "" });
            solo.forEach(addWork);
        }
        if (families.length === 0 && albumsFallback.length > 0) {
            out.push({ kind: "family", label: "Albums", range: "" });
            Format.chunk(albumsFallback, cols).forEach(c => out.push({ kind: "albums", albums: c }));
        }
        return out;
    }

    readonly property string subtitle: {
        const p = [];
        if (person.works) p.push(Format.plural(person.works, "work"));
        if (person.recordings) p.push(Format.plural(person.recordings, "recording"));
        if (person.albums) p.push(Format.plural(person.albums, "album"));
        return p.join(" · ");
    }

    // One recording per work (the first you own), whole works in the order shown; shuffled for "Shuffle works".
    function playWorks(shuffle) {
        let ids = shownWorks.map(w => w.id);
        if (shuffle) ids = Format.shuffled(ids);
        const all = Library.tracksOfWorks(ids);
        if (all.length > 0) Player.playTracks(all, 0, page.person.name || page.name);
    }
    function playRecording(work, rec) {
        const t = Library.workTracks(work.id, rec.albumId);
        if (t.length > 0) Player.playTracks(t, 0, work.title);
    }

    readonly property var playingWorkId: Player.current.workId || 0
    readonly property var playingAlbumId: Player.current.albumId || 0

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

        header: Column {
            width: list.width
            Item { width: 1; height: 8 }
            Row {
                id: headRow
                spacing: 28
                Avatar { diameter: 116; name: page.person.name || page.name }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Text {
                        text: "Composer"
                        color: Theme.textTertiary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
                    }
                    Text {
                        width: Math.min(implicitWidth, list.width - 116 - 28)
                        text: page.person.name || page.name
                        elide: Text.ElideRight; maximumLineCount: 1
                        color: Theme.textPrimary
                        font { family: Theme.fontDisplay; pixelSize: 40; weight: Font.Bold; letterSpacing: -1 }
                    }
                    Text {
                        text: (page.person.id || 0) ? page.subtitle : "Nothing in your library is tagged with this composer."
                        color: Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
                    }
                    Row {
                        topPadding: 4
                        spacing: 10
                        PillButton { text: "Play works"; icon: "play"; primary: true; enabledAction: page.shownWorks.length > 0; onClicked: page.playWorks(false) }
                        PillButton { text: "Shuffle works"; icon: "shuffle"; enabledAction: page.shownWorks.length > 0; onClicked: page.playWorks(true) }
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
            // filter chips: form, then instrument
            Item {
                width: parent.width
                visible: page.forms.length > 0 || page.instruments.length > 0
                height: visible ? chips.height + 22 : 0
                Flow {
                    id: chips
                    y: 22 - 6
                    width: parent.width
                    spacing: 8
                    Chip { text: "All works"; selected: page.formFilter === "" && page.instrumentFilter === ""; onClicked: { page.formFilter = ""; page.instrumentFilter = ""; } }
                    Repeater {
                        model: page.forms
                        delegate: Chip {
                            required property string modelData
                            text: Format.pluralTitle(modelData)
                            selected: page.formFilter === modelData
                            onClicked: page.formFilter = page.formFilter === modelData ? "" : modelData
                        }
                    }
                    Rectangle { visible: page.instruments.length > 0 && page.forms.length > 0; width: 1; height: 20; y: 12; color: Theme.chipBorder }
                    Repeater {
                        model: page.instruments
                        delegate: Chip {
                            required property string modelData
                            text: Format.capitalize(modelData)
                            selected: page.instrumentFilter === modelData
                            onClicked: page.instrumentFilter = page.instrumentFilter === modelData ? "" : modelData
                        }
                    }
                }
            }
            Item { width: 1; height: 8 }
        }

        delegate: Item {
            id: cell
            required property var modelData
            readonly property string kind: modelData.kind || ""
            width: list.width
            height: kind === "family" ? 56 : kind === "work" ? Theme.hitTarget : kind === "recording" ? 48 : page.cover + 56 + page.gap

            Loader {
                anchors.fill: parent
                sourceComponent: cell.kind === "family" ? familyC : cell.kind === "work" ? workC : cell.kind === "recording" ? recordingC : albumsC
            }

            Component {
                id: familyC
                Item {
                    Row {
                        anchors { left: parent.left; bottom: parent.bottom; bottomMargin: 8 }
                        spacing: 10
                        Text {
                            anchors.baseline: rangeText.baseline
                            text: cell.modelData.label
                            color: Theme.textPrimary
                            font { family: Theme.fontDisplay; pixelSize: 16; weight: Font.Bold }
                        }
                        Text {
                            id: rangeText
                            visible: (cell.modelData.range || "").length > 0
                            text: cell.modelData.range || ""
                            color: Theme.textTertiary
                            font { family: Theme.fontUi; pixelSize: Theme.sizeMeta + 1 }
                        }
                    }
                }
            }
            Component {
                id: workC
                Item {
                    readonly property var work: cell.modelData.work
                    readonly property bool open: cell.modelData.open
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusInput
                        color: parent.open ? Theme.surfaceRaised : (workMouse.containsMouse ? Theme.rowHover : "transparent")
                    }
                    MouseArea { id: workMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.toggleWork(parent.work.id) }
                    Icon {
                        x: 8; anchors.verticalCenter: parent.verticalCenter
                        name: "chevronDown"; size: 16; color: Theme.textTertiary
                        rotation: parent.open ? 0 : -90
                    }
                    Text {
                        id: catalogText
                        x: 40; width: 110 - 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.work.catalog || ""
                        elide: Text.ElideRight
                        color: Theme.textTertiary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
                    }
                    Text {
                        anchors { left: parent.left; leftMargin: 150; right: countText.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: parent.work.title
                        elide: Text.ElideRight; maximumLineCount: 1
                        color: page.playingWorkId === parent.work.id ? Theme.accent : Theme.textPrimary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; weight: Font.Medium }
                    }
                    Text {
                        id: countText
                        anchors { right: workHeart.left; verticalCenter: parent.verticalCenter }
                        width: 120; horizontalAlignment: Text.AlignRight
                        text: Format.plural(parent.work.recordings, "recording")
                        color: Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
                    }
                    HeartButton {
                        id: workHeart
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        kind: "work"; refId: parent.work.id; label: parent.work.title
                    }
                }
            }
            Component {
                id: recordingC
                Item {
                    readonly property var work: cell.modelData.work
                    readonly property var rec: cell.modelData.rec
                    readonly property bool playingNow: page.playingWorkId === work.id && page.playingAlbumId === rec.albumId && Player.playing
                    readonly property bool isCurrent: page.playingWorkId === work.id && page.playingAlbumId === rec.albumId
                    Rectangle { x: 32; width: parent.width - 32; height: parent.height; radius: Theme.radiusInput; color: recMouse.containsMouse ? Theme.rowHover : "transparent" }
                    MouseArea { id: recMouse; x: 32; width: parent.width - 32; height: parent.height; hoverEnabled: true; onDoubleClicked: page.playRecording(parent.work, parent.rec) }
                    Rectangle { id: recCover; x: 32 + 52 - 36 + 8; anchors.verticalCenter: parent.verticalCenter; width: 36; height: 36; radius: 4; color: Theme.surfaceCard
                        RoundedImage {
                            anchors.fill: parent; radius: 4
                            visible: (parent.parent.rec.artHash || "").length > 0
                            source: visible ? "image://art/" + parent.parent.rec.artHash + "/256" : ""
                            sourceSize: Qt.size(72, 72)
                        }
                    }
                    Item {
                        anchors { left: recCover.right; leftMargin: 14; right: yearText.left; rightMargin: 12; top: parent.top; bottom: parent.bottom }
                        LinkText {
                            id: performer
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            size: Theme.sizeBody
                            maxWidth: parent.width * 0.5
                            text: parent.parent.rec.albumArtist
                            color: parent.parent.isCurrent ? Theme.accent : Theme.textPrimary
                            onClicked: page.artistClicked(parent.parent.rec.albumArtist)
                        }
                        Text {
                            anchors { left: performer.right; leftMargin: 14; right: parent.right; verticalCenter: parent.verticalCenter }
                            text: parent.parent.rec.albumTitle
                            elide: Text.ElideRight; maximumLineCount: 1
                            color: Theme.textSecondary
                            font { family: Theme.fontUi; pixelSize: Theme.sizeBody; underline: albumMouse.containsMouse }
                            MouseArea { id: albumMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.albumClicked({ id: parent.parent.parent.rec.albumId, workId: parent.parent.parent.work.id }) }
                        }
                    }
                    Text {
                        id: yearText
                        anchors { right: durationText.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        width: 48
                        text: parent.rec.year > 0 ? parent.rec.year : ""
                        color: Theme.textTertiary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
                    }
                    Text {
                        id: durationText
                        anchors { right: playCircle.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        width: 56; horizontalAlignment: Text.AlignRight
                        text: Format.time(parent.rec.durationMs)
                        color: Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
                    }
                    Rectangle {
                        id: playCircle
                        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 32; height: 32; radius: 16
                        color: playMouse.containsMouse ? Theme.rowHover : "transparent"
                        border { width: 1; color: Theme.borderButton }
                        Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: parent.parent.playingNow ? 0 : 1; name: parent.parent.playingNow ? "pause" : "play"; size: 12; filled: true; color: Theme.textPrimary }
                        MouseArea {
                            id: playMouse
                            anchors { fill: parent; margins: -6 }
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: parent.parent.isCurrent ? Player.playPause() : page.playRecording(parent.parent.work, parent.parent.rec)
                        }
                    }
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
                text: page.allWorks.length > 0 ? "No works match these filters." : "No works detected for this composer yet."
                color: Theme.textTertiary
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
            }
        }
    }
    SmoothScroll { anchors.fill: list; flickable: list }
    VScrollBar { flickable: list; anchors { right: parent.right; rightMargin: 6; top: list.top; bottom: list.bottom } }
}
