import QtQuick
import QtQuick.Effects
import MusicPlayeah
import "WorkTitles.js" as WorkTitles

// Now Playing (M4-NowPlaying), over the whole window: blurred cover backdrop under a dark scrim, big cover,
// title + artist + album · year + format, progress bar (waveform comes in M10), big controls, signal path, up next.
// Close: the chevron top-left or Esc.
Rectangle {
    id: np
    signal close()
    signal albumRequested(var albumId)

    color: Theme.bg
    readonly property var cur: Player.current
    readonly property string artHash: cur.albumArtHash || ""

    readonly property var mv: WorkTitles.movement(cur, Player.items)
    readonly property string performer: WorkTitles.performer(cur)
    readonly property string composer: (cur.composer || "").length > 0 && cur.composer !== performer ? cur.composer : ""

    function esc(t) { return String(t).replace(/&/g, "&amp;").replace(/</g, "&lt;"); }

    function time(ms) {
        const s = Math.max(0, Math.floor(ms / 1000));
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60;
        return (h > 0 ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(ss).padStart(2, "0");
    }

    // Swallow clicks so nothing underneath reacts.
    MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }

    // ---- backdrop: a tiny decode of the cover, scaled up to fill (already soft), then blurred.
    // The image sits in a layer so the blur gets exactly what is painted (cropped to the window's shape); handing
    // the Image itself to MultiEffect left part of the window uncovered (black band at the bottom).
    Item {
        anchors { fill: parent; margins: -160 }
        visible: np.artHash.length > 0
        Item {
            id: backdropSource
            anchors.fill: parent
            visible: false
            layer.enabled: true
            layer.smooth: true
            Image {
                anchors.fill: parent
                source: np.artHash.length > 0 ? "image://art/" + np.artHash + "/256" : ""
                sourceSize: Qt.size(64, 64)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }
        }
        MultiEffect {
            anchors.fill: parent
            source: backdropSource
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1.0
            blurMax: 64
            saturation: 0.3
            opacity: 0.7
        }
    }
    Rectangle { anchors.fill: parent; color: Qt.rgba(14 / 255, 14 / 255, 16 / 255, 0.55) }

    // ---- top bar
    component GlassButton: Item {
        id: gb
        property string icon
        property int size: 44
        property color tint: Theme.textPrimary
        property bool enabledAction: true
        signal clicked()
        width: size; height: size
        Rectangle { anchors.fill: parent; radius: width / 2; color: gbMouse.containsMouse && gb.enabledAction ? "#24FFFFFF" : "#14FFFFFF" }
        Icon { anchors.centerIn: parent; name: gb.icon; size: 18; color: gb.tint; opacity: gb.enabledAction ? 1 : 0.4 }
        MouseArea { id: gbMouse; anchors.fill: parent; hoverEnabled: true; enabled: gb.enabledAction; cursorShape: Qt.PointingHandCursor; onClicked: gb.clicked() }
    }

    component BigButton: Item {
        id: bb
        property string icon
        property int size: 56
        property int iconSize: 24
        property bool primary: false
        property bool enabledAction: true
        property color tint: Theme.textPrimary
        signal clicked()
        width: size; height: size
        Rectangle { anchors.fill: parent; radius: width / 2; color: bb.primary ? Theme.textPrimary : (bbMouse.containsMouse && bb.enabledAction ? "#14FFFFFF" : "transparent") }
        Icon {
            anchors.centerIn: parent
            name: bb.icon; size: bb.iconSize
            color: bb.primary ? Theme.bg : bb.tint
            filled: ["play", "pause", "prev", "next"].indexOf(bb.icon) >= 0
            opacity: bb.enabledAction ? 1 : 0.4
        }
        MouseArea { id: bbMouse; anchors.fill: parent; hoverEnabled: true; enabled: bb.enabledAction; cursorShape: Qt.PointingHandCursor; onClicked: bb.clicked() }
    }

    Item {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 32; rightMargin: 32; topMargin: 20 }
        height: 44
        GlassButton { anchors.left: parent.left; icon: "chevronDown"; onClicked: np.close() }
        Text {
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 140)
            elide: Text.ElideRight
            text: Player.folder.length > 0 ? "Playing from album · " + Player.folder : ""
            color: Theme.textMuted
            font { family: Theme.fontUi; pixelSize: Theme.sizeMeta; weight: Font.DemiBold; letterSpacing: 1; capitalization: Font.AllUppercase }
        }
        GlassButton { anchors.right: parent.right; icon: "queue"; enabledAction: false }   // queue panel: M5
    }

    // ---- body: cover | details, centered; stacks when the window is narrow
    Flickable {
        id: body
        anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: parent.bottom; leftMargin: 32; rightMargin: 32; bottomMargin: 32 }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: Math.max(height, layout.height + 48)

        // Cover follows the window (≈460 px in a normal window, up to 760 full screen); text and controls scale
        // with it (`k`, 1 to 1.5) so a big screen is filled instead of leaving a small island in the middle.
        readonly property bool wide: width >= 280 + 56 + 420
        readonly property int coverSize: wide ? Math.round(Math.max(280, Math.min(760, height * 0.62, width * 0.36)))
                                              : Math.min(460, width)
        readonly property real k: Math.max(1, Math.min(1.5, coverSize / 460))
        readonly property int gap: Math.round(56 * k)
        readonly property int detailsWidth: wide ? Math.min(Math.round(520 * k), width - coverSize - gap) : Math.min(520, width)

        Grid {
            id: layout
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(24, (body.height - height) / 2)
            columns: body.wide ? 2 : 1
            columnSpacing: body.gap
            rowSpacing: 32
            verticalItemAlignment: Grid.AlignVCenter
            horizontalItemAlignment: Grid.AlignHCenter

            // cover
            Item {
                width: body.coverSize; height: body.coverSize
                RectangularShadow { anchors.fill: coverFrame; offset.y: 30; blur: 80; radius: 12; color: "#99000000" }
                Rectangle {
                    id: coverFrame
                    anchors.fill: parent
                    radius: 12
                    color: Theme.surfaceCard
                    Image {   // corners rounded with a mask: the backdrop here is not a solid color
                        id: bigCover
                        anchors.fill: parent
                        // 1200 px thumbnail: sharp at 760 px with display scaling (the 600 px one looked soft, D46).
                        source: np.artHash.length > 0 ? "image://art/" + np.artHash + "/1200" : ""
                        sourceSize: Qt.size(1200, 1200)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: false
                        layer.enabled: true
                    }
                    Rectangle { id: coverMask; anchors.fill: parent; radius: 12; visible: false; layer.enabled: true; layer.smooth: true; antialiasing: true }
                    MultiEffect {
                        anchors.fill: parent
                        source: bigCover
                        maskEnabled: true
                        maskSource: coverMask
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                        autoPaddingEnabled: false
                        visible: bigCover.status === Image.Ready
                    }
                    Icon { anchors.centerIn: parent; visible: np.artHash.length === 0; name: "albums"; size: 96; color: Theme.heartIdle }
                }
            }

            // details
            Column {
                width: body.detailsWidth
                spacing: Math.round(22 * body.k)

                Column {
                    width: parent.width
                    spacing: 6
                    Text {   // work overline, only for movements
                        width: parent.width - 60
                        visible: np.mv !== null
                        text: np.mv ? np.mv.work + (np.mv.catalog ? "  <font color='" + Theme.textTertiary + "'>· " + np.esc(np.mv.catalog) + "</font>" : "") : ""
                        textFormat: Text.StyledText
                        elide: Text.ElideRight; maximumLineCount: 1
                        color: Theme.textMuted
                        font { family: Theme.fontUi; pixelSize: Math.round(Theme.sizeMeta * body.k); weight: Font.DemiBold; letterSpacing: 1; capitalization: Font.AllUppercase }
                    }
                    Row {
                        width: parent.width
                        spacing: 12
                        Text {
                            id: bigTitle
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 60
                            text: {
                                if (np.mv) {
                                    const num = np.mv.number ? "<font color='" + Theme.textMuted + "'>" + np.mv.number + "</font> " : "";   // same color as the work line
                                    const of = "<span style='font-size:" + Math.round(Theme.sizeBody * body.k) + "px; font-weight:500'>"
                                             + "<font color='" + Theme.textTertiary + "'>&nbsp;&nbsp;" + np.mv.position + " of " + np.mv.total + "</font></span>";
                                    return num + np.esc(np.mv.name) + of;
                                }
                                return Player.title.length > 0 ? Player.title : "Nothing playing";
                            }
                            // Rich text for the smaller "1 of 6" inside the title; plain text keeps eliding long titles.
                            textFormat: np.mv ? Text.RichText : Text.PlainText
                            color: Theme.textPrimary
                            wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                            lineHeight: 1.05
                            font { family: Theme.fontDisplay; pixelSize: Math.round(Theme.sizeAlbumTitle * body.k); weight: Font.Bold; letterSpacing: -1.1 * body.k }
                        }
                        GlassButton { anchors.verticalCenter: parent.verticalCenter; size: 48; icon: "heart"; tint: Theme.textSecondary; enabledAction: false }   // likes: 4b
                    }
                    LinkText {
                        maxWidth: parent.width
                        visible: text.length > 0
                        text: np.performer
                        size: Math.round(18 * body.k)
                        // Artist page: 4b
                    }
                    Text {
                        width: parent.width
                        visible: np.composer.length > 0
                        text: "composed by " + np.composer      // Composer page link: 4b
                        elide: Text.ElideRight
                        color: Theme.textMuted
                        font { family: Theme.fontUi; pixelSize: Math.round(15 * body.k) }
                    }
                    Row {
                        width: parent.width
                        visible: (np.cur.albumTitle || "").length > 0
                        LinkText {
                            maxWidth: parent.width - yearText.width
                            text: np.cur.albumTitle || ""
                            size: Math.round(15 * body.k)
                            color: Theme.textMuted
                            font.weight: Font.Normal
                            onClicked: { np.albumRequested(np.cur.albumId); }
                        }
                        Text {
                            id: yearText
                            text: np.cur.albumYear ? " · " + np.cur.albumYear : ""
                            color: Theme.textMuted
                            font { family: Theme.fontUi; pixelSize: Math.round(15 * body.k) }
                        }
                    }
                }

                // progress: 6 px bar, 14 px knob; elapsed and remaining under it; the controls right below
                Column {
                    width: parent.width
                    spacing: 8
                    Item {
                        width: parent.width; height: 14
                        Rectangle {
                            id: bigTrack
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                            height: 6; radius: 3
                            color: "#2EFFFFFF"
                            Rectangle { width: parent.width * bigSeek.shown; height: parent.height; radius: 3; color: Theme.accent }
                        }
                        Rectangle {
                            width: 14; height: 14; radius: 7; color: Theme.knob
                            anchors.verticalCenter: parent.verticalCenter
                            x: parent.width * bigSeek.shown - 7
                        }
                        MouseArea {
                            id: bigSeek
                            property real dragValue: 0
                            readonly property real value: Player.durationMs > 0 ? Player.positionMs / Player.durationMs : 0
                            readonly property real shown: pressed ? dragValue : value
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                            height: Theme.hitTarget
                            cursorShape: Qt.PointingHandCursor
                            function at(x) { return Math.max(0, Math.min(1, x / width)); }
                            onPressed: (m) => dragValue = at(m.x)
                            onPositionChanged: (m) => { if (pressed) dragValue = at(m.x); }
                            onReleased: (m) => Player.seek(Math.round(at(m.x) * Player.durationMs))
                        }
                    }
                    Item {
                        width: parent.width; height: 16
                        Text {
                            text: bigSeek.pressed ? np.time(Player.durationMs * bigSeek.dragValue) : Player.positionText
                            color: Theme.textMuted
                            font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1; features: ({ "tnum": 1 }) }
                        }
                        Text {
                            anchors.right: parent.right
                            text: "−" + np.time(Player.durationMs - (bigSeek.pressed ? Player.durationMs * bigSeek.dragValue : Player.positionMs))
                            color: Theme.textMuted
                            font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1; features: ({ "tnum": 1 }) }
                        }
                    }
                    // controls: 44 · 48 · 60 · 48 · 44 over 360 px, fixed size: they don't grow full screen (felt too big there).
                    // Part of the progress block, so they sit 8 + 2 px under the times instead of the details' 22 px gap.
                    Item { width: 1; height: 2 }
                    Item {
                        x: (parent.width - width) / 2
                        width: Math.min(360, parent.width); height: 60
                        Row {
                            anchors.fill: parent
                            spacing: (width - (44 + 48 + 60 + 48 + 44)) / 4
                            BigButton { anchors.verticalCenter: parent.verticalCenter; size: 44; iconSize: 18; icon: "shuffle"; tint: Theme.textMuted; enabledAction: false }   // M5
                            BigButton { anchors.verticalCenter: parent.verticalCenter; size: 48; iconSize: 22; icon: "prev"; onClicked: Player.previous() }
                            BigButton { anchors.verticalCenter: parent.verticalCenter; size: 60; iconSize: 22; primary: true; icon: Player.playing ? "pause" : "play"; onClicked: Player.playPause() }
                            BigButton { anchors.verticalCenter: parent.verticalCenter; size: 48; iconSize: 22; icon: "next"; onClicked: Player.next() }
                            BigButton { anchors.verticalCenter: parent.verticalCenter; size: 44; iconSize: 18; icon: "repeat"; tint: Theme.textMuted; enabledAction: false }    // M5
                        }
                    }
                }

                // signal path
                Rectangle {
                    width: parent.width
                    visible: Player.signalPath.length > 0
                    height: pathCol.height + 32
                    radius: 12
                    color: Qt.rgba(14 / 255, 14 / 255, 16 / 255, 0.55)
                    border { width: 1; color: "#14FFFFFF" }
                    Column {
                        id: pathCol
                        x: 18; y: 16
                        width: parent.width - 36
                        spacing: 10
                        Text {
                            text: "Signal path"
                            color: Theme.textSecondary
                            font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
                        }
                        Column {
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: Player.signalPath
                                delegate: Row {
                                    required property var modelData
                                    width: pathCol.width
                                    Text { width: 96; text: modelData.label; color: Theme.textSecondary; font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 } }
                                    Text { width: parent.width - 96; text: modelData.value; elide: Text.ElideRight; color: Theme.textPrimary; font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 } }
                                }
                            }
                        }
                    }
                }

                // up next (the rest of the current list; the real queue comes in M5)
                Column {
                    id: upNext
                    width: parent.width
                    spacing: 4
                    readonly property var next: Player.items.slice(Player.currentIndex + 1, Player.currentIndex + 1 + (body.k > 1.3 ? 5 : 3))
                    visible: next.length > 0
                    Text {
                        bottomPadding: 4
                        text: "Up next"
                        color: Theme.textSecondary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
                    }
                    Repeater {
                        model: upNext.next
                        delegate: Item {
                            id: nextRow
                            required property var modelData
                            required property int index
                            width: upNext.width; height: 40
                            Rectangle { anchors { fill: parent; leftMargin: -8; rightMargin: -8 } radius: 8; color: nextMouse.containsMouse ? "#14FFFFFF" : "transparent" }
                            Text {
                                id: nextNo
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20
                                text: nextRow.index + 1
                                color: Theme.textSecondary
                                font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
                            }
                            Text {
                                anchors { left: nextNo.right; leftMargin: 12; right: nextTime.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                                text: {
                                    return WorkTitles.shortTitle(nextRow.modelData, Player.items);
                                }
                                elide: Text.ElideRight
                                color: Theme.textPrimary
                                font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
                            }
                            Text {
                                id: nextTime
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: nextRow.modelData.durationMs ? np.time(nextRow.modelData.durationMs) : ""
                                color: Theme.textSecondary
                                font { family: Theme.fontUi; pixelSize: Theme.sizeBody; features: ({ "tnum": 1 }) }
                            }
                            MouseArea {
                                id: nextMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onDoubleClicked: Player.playIndex(Player.currentIndex + 1 + nextRow.index)
                            }
                        }
                    }
                }
            }
        }
    }
}
