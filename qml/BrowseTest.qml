import QtQuick
import MusicPlayeah

// Milestone 3b test view (the real library screens are milestone 4):
//  - Albums: plain grid with covers (image://art/<hash>/256); click an album for its tracks (works and
//    movements shown as detected); double-click a track or "Play album" to play it through the M2 engine.
//  - Works: by composer (composer -> family such as "Brandenburg Concerto" -> work, D38) or A-Z. A work shows
//    its recordings with their movements; "Play work" plays one recording in movement order (gapless), "Album"
//    jumps to that album with the work's tracks marked.
//  - Not detected: albums whose tracks have a composer tag but ended up in no work. This is the list of
//    "wrong or missed" cases that milestone 7 (metadata fixes) works from.
Item {
    id: page

    readonly property int pad: 24
    property string section: "albums"
    property var selectedAlbum: null     // map from Library.albums()
    property var selectedWork: null      // map from Library.works()
    property string worksMode: "composer"  // "composer" (tree) or "az" (flat list)
    property var expanded: ({})          // tree rows that are open: "c:<composer>" and "c:<composer>/<family>"
    property int markedWorkId: 0         // album view: tracks of this work are marked (set by "Album")

    // The lists are plain properties reloaded by reload(): on start, and whenever the library content changed
    // (Library.revision, bumped when a scan finishes or a folder is removed). Explicit on purpose: a binding that
    // only "reads" Library.revision as a statement did not refresh reliably.
    property var albumList: []
    property var workList: []
    property var composerTree: []
    property var workRows: []            // what the works list shows, built by rebuildWorkRows()
    property var missedList: []
    property var albumTrackList: []
    property var workMovementList: []

    function reload() {
        albumList = Library.albums();
        workList = Library.works();
        composerTree = Library.worksByComposer();
        missedList = Library.undetectedClassical();
        // Keep the selection only if it still exists.
        if (selectedAlbum !== null && !albumList.some(a => a.id === selectedAlbum.id)) selectedAlbum = null;
        if (selectedWork !== null && !workList.some(w => w.id === selectedWork.id)) selectedWork = null;
        albumTrackList = selectedAlbum ? Library.albumTracks(selectedAlbum.id) : [];
        workMovementList = selectedWork ? Library.workMovements(selectedWork.id) : [];
        rebuildWorkRows();
    }

    // Flattens the composer tree (only the open parts) or the A-Z list into rows for one ListView.
    function rebuildWorkRows() {
        const rows = [];
        if (worksMode === "az") {
            for (const w of workList) rows.push({ kind: "work", work: w, depth: 0, showComposer: true });
        } else {
            for (const c of composerTree) {
                const ckey = "c:" + c.name;
                const open = expanded[ckey] === true;
                rows.push({ kind: "composer", key: ckey, label: c.name.length > 0 ? c.name : "No composer tag",
                            count: c.workCount, open: open, depth: 0 });
                if (!open) continue;
                for (const f of c.families) {
                    if (f.label.length === 0) {
                        rows.push({ kind: "work", work: f.works[0], depth: 1, showComposer: false });
                        continue;
                    }
                    const fkey = ckey + "/" + f.label;
                    const fopen = expanded[fkey] === true;
                    rows.push({ kind: "family", key: fkey, label: f.label, range: f.catalogRange, count: f.works.length, open: fopen, depth: 1 });
                    if (fopen) {
                        for (const w of f.works) rows.push({ kind: "work", work: w, depth: 2, showComposer: false });
                    }
                }
            }
        }
        // A new model resets the view to the top: keep the place.
        const y = workListView.contentY;
        workRows = rows;
        workListView.contentY = Math.max(workListView.originY, Math.min(y, workListView.originY + Math.max(0, workListView.contentHeight - workListView.height)));
    }
    function toggle(key) {
        const e = Object.assign({}, expanded);
        e[key] = !(e[key] === true);
        expanded = e;
        rebuildWorkRows();
    }
    onWorksModeChanged: rebuildWorkRows()

    // From a recording of a work to its album, with the work's tracks marked and in view.
    function showInAlbum(albumId, workId) {
        const i = albumList.findIndex(a => a.id === albumId);
        if (i < 0) return;
        markedWorkId = workId;
        selectedAlbum = albumList[i];
        section = "albums";
        grid.positionViewAtIndex(i, GridView.Contain);
        const t = albumTrackList.findIndex(r => r.workId === workId);
        if (t >= 0) Qt.callLater(() => albumTracksView.positionViewAtIndex(t, ListView.Beginning));
    }
    onSelectedAlbumChanged: albumTrackList = selectedAlbum ? Library.albumTracks(selectedAlbum.id) : []
    onSelectedWorkChanged: workMovementList = selectedWork ? Library.workMovements(selectedWork.id) : []
    Component.onCompleted: reload()
    Connections {
        target: Library
        function onRevisionChanged() { page.reload(); }
    }

    function formatTime(ms) {
        const s = Math.round(ms / 1000);
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60);
    }

    component Label: Text {
        color: Theme.textPrimary
        font { family: Theme.fontFamily; pixelSize: Theme.sizeBody }
        elide: Text.ElideRight
    }

    component CapsLabel: Text {
        color: Theme.textTertiary
        font { family: Theme.fontFamily; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.8; capitalization: Font.AllUppercase }
    }

    component Pill: Rectangle {
        id: pill
        property string text
        property bool primary: false
        signal clicked()
        implicitWidth: pillLabel.implicitWidth + 28
        height: 36
        radius: height / 2
        color: primary ? Theme.accent : (pillMouse.containsMouse ? Theme.surfaceInput : "transparent")
        border { color: primary ? "transparent" : Theme.borderButton; width: 1 }
        Text {
            id: pillLabel
            anchors.centerIn: parent
            text: pill.text
            color: pill.primary ? Theme.accentText : Theme.textPrimary
            font { family: Theme.fontFamily; pixelSize: Theme.sizeMeta + 1; weight: Font.DemiBold }
        }
        MouseArea {
            id: pillMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
        }
    }

    // ---------------------------------------------------------------- header

    Row {
        id: sections
        anchors { left: parent.left; top: parent.top; leftMargin: page.pad; topMargin: 4 }
        spacing: 8
        Repeater {
            model: [{ key: "albums", label: "Albums" }, { key: "works", label: "Works" }, { key: "missed", label: "Not detected" }]
            delegate: Pill {
                required property var modelData
                text: modelData.label
                primary: page.section === modelData.key
                onClicked: page.section = modelData.key
            }
        }
    }

    Label {
        anchors { left: sections.right; leftMargin: 20; right: parent.right; rightMargin: page.pad; verticalCenter: sections.verticalCenter }
        text: Library.worksSummary
        color: Theme.textSecondary
        font.pixelSize: Theme.sizeMeta
    }

    // ---------------------------------------------------------------- albums

    Item {
        visible: page.section === "albums"
        anchors { left: parent.left; right: parent.right; top: sections.bottom; bottom: parent.bottom; topMargin: 12 }

        GridView {
            id: grid
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; leftMargin: page.pad }
            width: parent.width * 0.6 - page.pad
            clip: true
            cellWidth: 184
            cellHeight: 250
            model: page.albumList
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                id: cell
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool selected: page.selectedAlbum !== null && page.selectedAlbum.id === modelData.id

                Rectangle {
                    anchors { fill: parent; margins: 6 }
                    radius: Theme.radiusCard
                    color: cell.selected ? Theme.surfaceInput : "transparent"
                    border { color: cell.selected ? Theme.accent : "transparent"; width: 1 }
                }
                Rectangle {
                    id: coverFrame
                    x: 12; y: 12
                    width: 160; height: 160
                    radius: Theme.radiusCover
                    color: Theme.surfaceCard
                    clip: true
                    Image {
                        anchors.fill: parent
                        visible: cell.modelData.artHash.length > 0
                        source: cell.modelData.artHash.length > 0 ? "image://art/" + cell.modelData.artHash + "/256" : ""
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize: Qt.size(256, 256)
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: cell.modelData.artHash.length === 0
                        text: "no cover"
                        color: Theme.textTertiary
                        font { family: Theme.fontFamily; pixelSize: Theme.sizeMeta }
                    }
                }
                Column {
                    x: 12; y: 180
                    width: 160
                    spacing: 2
                    Label { width: parent.width; text: cell.modelData.title; font.weight: Font.DemiBold }
                    Label { width: parent.width; text: cell.modelData.artist; color: Theme.textSecondary; font.pixelSize: Theme.sizeMeta }
                    Label {
                        width: parent.width
                        text: cell.modelData.tracks + " tracks" + (cell.modelData.works > 0 ? " · " + cell.modelData.works + " works" : "")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.sizeMeta
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { page.markedWorkId = 0; page.selectedAlbum = cell.modelData; }
                }
            }
        }

        VScrollBar { flickable: grid; anchors { left: grid.right; leftMargin: 2; top: grid.top; bottom: grid.bottom } }

        // Selected album: tracks as detected
        Item {
            anchors { left: grid.right; leftMargin: 16; right: parent.right; rightMargin: page.pad; top: parent.top; bottom: parent.bottom }
            visible: page.selectedAlbum !== null

            Column {
                id: albumHeader
                anchors { left: parent.left; right: parent.right; top: parent.top }
                spacing: 6
                Label { width: parent.width; text: page.selectedAlbum ? page.selectedAlbum.title : ""; font { pixelSize: Theme.sizeSection; weight: Font.DemiBold } }
                Label { width: parent.width; text: page.selectedAlbum ? page.selectedAlbum.artist + (page.selectedAlbum.year > 0 ? " · " + page.selectedAlbum.year : "") : ""; color: Theme.textSecondary }
                Label {
                    width: parent.width
                    visible: page.selectedAlbum !== null && page.selectedAlbum.instruments.length > 0
                    text: page.selectedAlbum ? "Instruments: " + page.selectedAlbum.instruments : ""
                    color: Theme.accent
                    font.pixelSize: Theme.sizeMeta
                }
                Pill {
                    text: "Play album"
                    primary: true
                    onClicked: Player.playTracks(page.albumTrackList, 0, page.selectedAlbum.title)
                }
            }

            ListView {
                id: albumTracksView
                anchors { left: parent.left; right: parent.right; top: albumHeader.bottom; bottom: parent.bottom; topMargin: 12 }
                clip: true
                model: page.albumTrackList
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: modelData.workTitle.length > 0 && (index === 0 || page.albumTrackList[index - 1].workTitle !== modelData.workTitle) ? 62 : 34
                    color: rowMouse.containsMouse ? Theme.surfaceInput : (marked ? Theme.surfaceCard : "transparent")
                    readonly property bool firstOfWork: height > 40
                    readonly property bool marked: page.markedWorkId !== 0 && modelData.workId === page.markedWorkId
                    Rectangle {
                        visible: parent.marked
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                        width: 3
                        color: Theme.accent
                    }
                    Label {
                        visible: parent.firstOfWork
                        x: 8; y: 6
                        width: parent.width - 16
                        text: modelData.workTitle
                        color: Theme.accent
                        font.weight: Font.DemiBold
                    }
                    Label {
                        anchors { left: parent.left; leftMargin: modelData.workTitle.length > 0 ? 24 : 8; right: timeLabel.left; rightMargin: 8; bottom: parent.bottom; bottomMargin: 7 }
                        text: modelData.workTitle.length > 0
                              ? modelData.movementNo + ". " + modelData.movementName
                              : modelData.title
                        color: modelData.workTitle.length > 0 ? Theme.textPrimary : Theme.textSecondary
                    }
                    Label {
                        id: timeLabel
                        anchors { right: parent.right; rightMargin: 8; bottom: parent.bottom; bottomMargin: 7 }
                        text: page.formatTime(modelData.durationMs) + "  " + modelData.codec
                        color: Theme.textTertiary
                        font.pixelSize: Theme.sizeMeta
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onDoubleClicked: Player.playTracks(page.albumTrackList, index, page.selectedAlbum.title)
                    }
                }
            }
            VScrollBar { flickable: albumTracksView; anchors { left: albumTracksView.right; leftMargin: 4; top: albumTracksView.top; bottom: albumTracksView.bottom } }
        }
        Label {
            anchors { left: grid.right; leftMargin: 16; top: parent.top; topMargin: 8 }
            visible: page.selectedAlbum === null
            text: "Click an album. Double-click a track to play from it."
            color: Theme.textTertiary
        }
    }

    // ---------------------------------------------------------------- works

    Item {
        visible: page.section === "works"
        anchors { left: parent.left; right: parent.right; top: sections.bottom; bottom: parent.bottom; topMargin: 12 }

        Row {
            id: worksModes
            anchors { left: parent.left; top: parent.top; leftMargin: page.pad }
            spacing: 8
            Repeater {
                model: [{ key: "composer", label: "By composer" }, { key: "az", label: "A–Z" }]
                delegate: Pill {
                    required property var modelData
                    text: modelData.label
                    primary: page.worksMode === modelData.key
                    onClicked: page.worksMode = modelData.key
                }
            }
        }

        ListView {
            id: workListView
            anchors { left: parent.left; top: worksModes.bottom; bottom: parent.bottom; leftMargin: page.pad; topMargin: 10 }
            width: parent.width * 0.45 - page.pad
            clip: true
            model: page.workRows
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool isWork: modelData.kind === "work"
                readonly property bool selected: isWork && page.selectedWork !== null && page.selectedWork.id === modelData.work.id
                width: ListView.view.width
                height: modelData.kind === "composer" ? 44 : (isWork ? 50 : 40)
                radius: 8
                color: selected ? Theme.surfaceInput : (workRowMouse.containsMouse ? Theme.surfaceCard : "transparent")

                // Composer or family: chevron, name, count
                Item {
                    visible: !row.isWork
                    anchors { fill: parent; leftMargin: 8 + row.modelData.depth * 22; rightMargin: 10 }
                    Text {
                        id: chevron
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        width: 16
                        text: row.modelData.open ? "▾" : "▸"
                        color: Theme.textSecondary
                        font { family: Theme.fontFamily; pixelSize: Theme.sizeBody }
                    }
                    Label {
                        id: groupName
                        anchors { left: chevron.right; leftMargin: 4; verticalCenter: parent.verticalCenter }
                        width: Math.min(implicitWidth, countLabel.x - x - 8)
                        text: row.isWork ? "" : row.modelData.label
                        color: row.modelData.kind === "family" ? Theme.accent : Theme.textPrimary
                        font { weight: Font.DemiBold; pixelSize: row.modelData.kind === "composer" ? Theme.sizeBody + 1 : Theme.sizeBody }
                    }
                    // Family: the catalog numbers it spans, in gray beside the name ("BWV 1007–1012")
                    Label {
                        anchors { left: groupName.right; leftMargin: 10; right: countLabel.left; rightMargin: 8; baseline: groupName.baseline }
                        visible: row.modelData.kind === "family"
                        text: row.modelData.kind === "family" ? row.modelData.range : ""
                        color: Theme.textTertiary
                        font.pixelSize: Theme.sizeMeta
                    }
                    Label {
                        id: countLabel
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: row.isWork ? "" : row.modelData.count + (row.modelData.count === 1 ? " work" : " works")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.sizeMeta
                    }
                }

                // Work: title, then catalog, recordings, tracks
                Column {
                    visible: row.isWork
                    anchors { left: parent.left; leftMargin: 10 + row.modelData.depth * 22 + (row.modelData.depth > 0 ? 20 : 0); right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    Label { width: parent.width; text: row.isWork ? row.modelData.work.title : ""; font.weight: Font.DemiBold }
                    Label {
                        width: parent.width
                        text: {
                            if (!row.isWork) return "";
                            const w = row.modelData.work;
                            const parts = [];
                            if (row.modelData.showComposer) parts.push(w.composer.length > 0 ? w.composer : "no composer");
                            if (w.catalog.length > 0) parts.push(w.catalog);
                            parts.push(w.recordings + (w.recordings === 1 ? " recording" : " recordings"));
                            parts.push(w.tracks + " tracks");
                            return parts.join(" · ");
                        }
                        color: Theme.textSecondary
                        font.pixelSize: Theme.sizeMeta
                    }
                }

                MouseArea {
                    id: workRowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (row.isWork) page.selectedWork = row.modelData.work;
                        else page.toggle(row.modelData.key);
                    }
                }
            }
        }
        VScrollBar { flickable: workListView; anchors { left: workListView.right; leftMargin: 2; top: workListView.top; bottom: workListView.bottom } }

        // The selected work: its recordings (albums), each with its movements
        Column {
            id: workHeader
            visible: page.selectedWork !== null
            anchors { left: workListView.right; leftMargin: 20; right: parent.right; rightMargin: page.pad; top: parent.top }
            spacing: 4
            Label {
                width: parent.width
                text: page.selectedWork ? page.selectedWork.title : ""
                font { pixelSize: Theme.sizeSection; weight: Font.DemiBold }
            }
            Label {
                width: parent.width
                text: page.selectedWork
                      ? [page.selectedWork.composer, page.selectedWork.catalog].filter(x => x.length > 0).join(" · ") +
                        " · " + page.selectedWork.recordings + (page.selectedWork.recordings === 1 ? " recording" : " recordings")
                      : ""
                color: Theme.textSecondary
            }
        }

        ListView {
            id: movementsView
            anchors { left: workListView.right; leftMargin: 20; right: parent.right; rightMargin: page.pad; top: workHeader.bottom; bottom: parent.bottom; topMargin: 8 }
            clip: true
            visible: page.selectedWork !== null
            model: page.workMovementList
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                id: mv
                required property var modelData
                required property int index
                readonly property bool firstOfRecording: index === 0 || page.workMovementList[index - 1].albumId !== modelData.albumId
                width: ListView.view.width
                height: firstOfRecording ? 98 : 30

                Item {
                    visible: mv.firstOfRecording
                    width: parent.width
                    height: 66
                    Rectangle {
                        id: recCover
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        width: 52; height: 52
                        radius: Theme.radiusCover
                        color: Theme.surfaceCard
                        clip: true
                        Image {
                            anchors.fill: parent
                            visible: mv.modelData.albumArtHash.length > 0
                            source: mv.modelData.albumArtHash.length > 0 ? "image://art/" + mv.modelData.albumArtHash + "/256" : ""
                            asynchronous: true
                            fillMode: Image.PreserveAspectCrop
                            sourceSize: Qt.size(256, 256)
                        }
                    }
                    Column {
                        anchors { left: recCover.right; leftMargin: 12; right: albumPill.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        spacing: 2
                        Label { width: parent.width; text: mv.modelData.albumTitle; color: Theme.accent; font.weight: Font.DemiBold }
                        Label {
                            width: parent.width
                            text: mv.modelData.albumArtist + (mv.modelData.albumYear > 0 ? " · " + mv.modelData.albumYear : "")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.sizeMeta
                        }
                    }
                    Pill {
                        id: albumPill
                        anchors { right: playPill.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: "Album"
                        onClicked: page.showInAlbum(mv.modelData.albumId, page.selectedWork.id)
                    }
                    Pill {
                        id: playPill
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: "Play work"
                        primary: true
                        onClicked: Player.playTracks(Library.workTracks(page.selectedWork.id, mv.modelData.albumId), 0, page.selectedWork.title)
                    }
                }
                Label {
                    anchors { left: parent.left; leftMargin: 64; right: parent.right; bottom: parent.bottom; bottomMargin: 6 }
                    text: mv.modelData.movementNo + ". " + mv.modelData.movementName + "   (" + mv.modelData.title + ")"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.sizeMeta + 1
                }
            }
        }
        VScrollBar { flickable: movementsView; anchors { left: movementsView.right; leftMargin: 4; top: movementsView.top; bottom: movementsView.bottom } }
        Label {
            anchors { left: workListView.right; leftMargin: 20; top: parent.top; topMargin: 8 }
            visible: page.selectedWork === null
            text: Library.worksSummary.length > 0 ? "Open a composer and click a work to see its recordings." : ""
            color: Theme.textTertiary
        }
    }

    // ---------------------------------------------------------------- not detected

    ListView {
        id: missedView
        visible: page.section === "missed"
        anchors { left: parent.left; right: parent.right; top: sections.bottom; bottom: parent.bottom; leftMargin: page.pad; rightMargin: page.pad; topMargin: 12 }
        clip: true
        model: page.missedList
        boundsBehavior: Flickable.StopAtBounds
        header: Label {
            width: ListView.view.width
            height: 32
            text: page.missedList.length === 0 ? "Nothing here: every composer-tagged track belongs to a work." :
                  "Albums with composer-tagged tracks that were not grouped into a work (up to 500):"
            color: Theme.textSecondary
        }
        delegate: Rectangle {
            required property var modelData
            width: ListView.view.width
            height: 52
            color: "transparent"
            Column {
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                Label { width: parent.width; text: modelData.albumTitle + " · " + modelData.albumArtist; font.weight: Font.DemiBold }
                Label {
                    width: parent.width
                    text: modelData.tracks + " tracks · composer " + modelData.composer + " · e.g. “" + modelData.sampleTitle + "”"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.sizeMeta
                }
            }
        }
    }
    VScrollBar {
        visible: page.section === "missed" && ratio < 0.999
        flickable: missedView
        anchors { left: missedView.right; leftMargin: 4; top: missedView.top; bottom: missedView.bottom }
    }
}
