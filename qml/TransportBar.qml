import QtQuick
import MusicPlayeah

// Bottom player bar (D43, D44), measured from design/mockups/M4-*.dc.html:
//   107 px tall, padding 0 28, three columns. Left: 72 px cover, 16 gap, text lines 3 px apart.
//   Center (max 600): buttons 44 px (play 48) with 16 px gaps; 12 px below, the progress row (time · 10 · line · 10 · time).
//   Right: heart, queue, volume icon, 120 px volume line, 12 px gaps.
// Same sizes at every window size (ui = 1). Growing with the window looked too big full screen; px() stays so a factor can come back.
// Every row centers its items vertically (a plain Row aligns tops, which put small buttons higher than the play button).
Rectangle {
    id: bar
    signal nowPlayingRequested()
    signal albumRequested(var albumId)

    readonly property var cur: Player.current
    readonly property string artHash: cur.albumArtHash || ""
    readonly property real ui: 1
    function px(v) { return Math.round(v * ui); }

    height: px(Theme.transportHeight)
    color: Theme.bg
    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: Theme.borderSubtle }

    // "FLAC 16/44.1" (Player.badge) -> "FLAC · 16-bit / 44.1 kHz". Anything else is shown as it is.
    function formatText(badge) {
        const m = /^(\S+)\s+(\d+)\/([\d.]+)$/.exec(badge);
        return m ? m[1] + " · " + m[2] + "-bit / " + m[3] + " kHz" : badge;
    }
    function formatMs(ms) {
        const s = Math.floor(ms / 1000), m = Math.floor(s / 60), h = Math.floor(m / 60);
        const ss = String(s % 60).padStart(2, "0");
        return h > 0 ? h + ":" + String(m % 60).padStart(2, "0") + ":" + ss : m + ":" + ss;
    }

    // Square cell `size` px; a circle of the same size when `primary` (white play button) or hovered.
    component RoundButton: Item {
        id: rb
        property string icon
        property int size: bar.px(44)
        property int iconSize: bar.px(18)
        property bool primary: false
        property bool enabledAction: true
        property color tint: Theme.textPrimary
        signal clicked()
        width: size; height: size
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: rb.primary ? Theme.textPrimary : (rbMouse.containsMouse && rb.enabledAction ? Theme.rowHover : "transparent")
        }
        Icon {
            anchors.centerIn: parent
            name: rb.icon
            size: rb.iconSize
            color: rb.primary ? Theme.bg : rb.tint
            filled: ["play", "pause", "prev", "next"].indexOf(rb.icon) >= 0
        }
        MouseArea {
            id: rbMouse
            anchors.centerIn: parent
            width: Math.max(parent.width, Theme.hitTarget); height: width
            hoverEnabled: true
            enabled: rb.enabledAction
            cursorShape: Qt.PointingHandCursor
            onClicked: rb.clicked()
        }
    }

    component TimeText: Text {
        color: Theme.textSecondary
        font { family: Theme.fontUi; pixelSize: Theme.sizeMeta; features: ({ "tnum": 1 }) }
    }

    // ---- left: cover and text (flex 1)
    Item {
        id: left
        anchors { left: parent.left; leftMargin: bar.px(28); right: centerBlock.left; rightMargin: bar.px(32); top: parent.top; bottom: parent.bottom }
        Rectangle {
            id: cover
            anchors.verticalCenter: parent.verticalCenter
            width: bar.px(72); height: width
            radius: 6
            color: Theme.surfaceCard
            RoundedImage {
                anchors.fill: parent
                radius: 6
                visible: bar.artHash.length > 0
                source: bar.artHash.length > 0 ? "image://art/" + bar.artHash + "/256" : ""
                sourceSize: Qt.size(192, 192)
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: bar.nowPlayingRequested() }
        }
        Column {
            anchors { left: cover.right; leftMargin: bar.px(16); right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 3
            Text {
                width: parent.width
                text: Player.title.length > 0 ? Player.title : "Nothing playing"
                color: Theme.textPrimary; elide: Text.ElideRight; maximumLineCount: 1
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody; weight: Font.DemiBold }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: bar.nowPlayingRequested() }
            }
            Text {
                width: parent.width; visible: text.length > 0
                text: bar.cur.albumTitle || ""
                color: Theme.textMuted; elide: Text.ElideRight; maximumLineCount: 1
                font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: bar.albumRequested(bar.cur.albumId) }
            }
            Text {
                width: parent.width; visible: text.length > 0
                text: (bar.cur.artist || "").replace(/\s*[\r\n]+\s*/g, " · ")
                color: Theme.textTertiary; elide: Text.ElideRight; maximumLineCount: 1
                font { family: Theme.fontUi; pixelSize: Theme.sizeMeta }
            }
            Item {
                visible: Player.badge.length > 0
                width: parent.width; height: formatLabel.height + 4   // 2 px extra above, as in the mockup
                Icon { id: fileIcon; anchors { left: parent.left; verticalCenter: formatLabel.verticalCenter } name: "file"; size: 12; strokeWidth: 2; color: Theme.textTertiary }
                Text {
                    id: formatLabel
                    anchors { left: fileIcon.right; leftMargin: 5; right: parent.right; bottom: parent.bottom }
                    text: bar.cur.format || bar.formatText(Player.badge)
                    color: Theme.textTertiary; elide: Text.ElideRight
                    font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; features: ({ "tnum": 1 }) }
                }
            }
        }
    }

    // ---- center (flex 0 1 600)
    Item {
        id: centerBlock
        anchors.centerIn: parent
        // Icons keep their size; only the progress line gets longer on wide windows (600 px up to 1500, then 40% of the bar).
        width: Math.min(bar.width - 2 * 360, Math.max(600, bar.width * 0.4))
        height: buttons.height + bar.px(12) + progressRow.height

        Row {
            id: buttons
            anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
            height: bar.px(48)
            spacing: bar.px(16)
            RoundButton { anchors.verticalCenter: parent.verticalCenter; icon: "shuffle"; tint: Theme.textSecondary; enabledAction: false }   // M5
            RoundButton { anchors.verticalCenter: parent.verticalCenter; icon: "prev"; iconSize: bar.px(20); onClicked: Player.previous() }
            RoundButton {
                anchors.verticalCenter: parent.verticalCenter
                size: bar.px(48); primary: true
                icon: Player.playing ? "pause" : "play"
                onClicked: Player.playPause()
            }
            RoundButton { anchors.verticalCenter: parent.verticalCenter; icon: "next"; iconSize: bar.px(20); onClicked: Player.next() }
            RoundButton { anchors.verticalCenter: parent.verticalCenter; icon: "repeat"; tint: Theme.textSecondary; enabledAction: false }    // M5
        }

        Item {
            id: progressRow
            anchors { top: buttons.bottom; topMargin: bar.px(12); left: parent.left; right: parent.right }
            height: 12   // the knob; the slider's hit area reaches beyond it
            TimeText {
                id: pos
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: dur.implicitWidth; horizontalAlignment: Text.AlignRight
                text: progress.dragging ? bar.formatMs(Player.durationMs * progress.dragValue) : Player.positionText
            }
            LineSlider {
                id: progress
                anchors { left: pos.right; leftMargin: 10; right: dur.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                height: parent.height
                trackHeight: bar.px(4)
                value: Player.durationMs > 0 ? Player.positionMs / Player.durationMs : 0
                onReleased: (v) => Player.seek(Math.round(v * Player.durationMs))
            }
            TimeText {
                id: dur
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: Player.durationText
            }
        }
    }

    // ---- right (flex 1, right aligned)
    Row {
        anchors { right: parent.right; rightMargin: bar.px(28); verticalCenter: parent.verticalCenter }
        height: bar.px(44)
        spacing: bar.px(12)
        RoundButton { anchors.verticalCenter: parent.verticalCenter; icon: "heart"; iconSize: bar.px(17); tint: Theme.heartIdle; enabledAction: false }   // likes: slice 2
        RoundButton { anchors.verticalCenter: parent.verticalCenter; icon: "queue"; tint: Theme.textSecondary; enabledAction: false }                    // M5
        Icon { anchors.verticalCenter: parent.verticalCenter; name: "volume"; size: bar.px(18); color: Theme.textSecondary }
        LineSlider {
            anchors.verticalCenter: parent.verticalCenter
            width: bar.px(120); height: 12
            trackHeight: bar.px(4)
            value: Player.volume
            onMoved: (v) => Player.volume = v
        }
    }
}
