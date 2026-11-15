import QtQuick
import QtQuick.Dialogs
import MusicPlayeah

// Test window with two pages (real layout arrives in milestone 4):
//  - Player (milestone 2): a folder's tracks and a minimal transport bar, enough to test the
//    playback engine (gapless, seeking, pause, volume, underruns while scrolling).
//  - Library (milestone 3a): watched folders, scanning and counts (LibraryTest.qml).
// Keys: Space play/pause · Left/Right seek 5 s · Ctrl+Left/Right previous/next · Ctrl+O open folder.
Window {
    id: root
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: Player.title.length > 0 ? Player.title + " · " + AppInfo.name : AppInfo.name
    color: Theme.bg

    // Which test page is shown: "player" (milestone 2) or "library" (milestone 3a).
    // Real navigation (sidebar) arrives with milestone 4.
    property string page: "player"

    // ---------------------------------------------------------------- small components

    component Label: Text {
        color: Theme.textPrimary
        font { family: Theme.fontFamily; pixelSize: Theme.sizeBody }
        elide: Text.ElideRight
    }

    // Round button that draws its own icon, so no icon font is needed yet.
    component IconButton: Rectangle {
        id: btn
        property string icon: "play"   // play | pause | prev | next
        property bool primary: false
        signal clicked()

        width: Theme.hitTarget; height: Theme.hitTarget
        radius: width / 2
        color: primary ? Theme.accent : (mouse.containsMouse ? Theme.surfaceInput : "transparent")

        Canvas {
            id: canvas
            anchors.centerIn: parent
            width: 16; height: 16
            property color fill: btn.primary ? Theme.accentText : Theme.textPrimary
            property string icon: btn.icon
            onIconChanged: requestPaint()
            onFillChanged: requestPaint()
            onPaint: {
                const c = getContext("2d");
                c.reset();
                c.fillStyle = fill;
                // Triangle with its flat side at x0 and its point at x1 (x1 < x0 points left).
                const tri = (x0, x1) => { c.beginPath(); c.moveTo(x0, 1); c.lineTo(x1, 8); c.lineTo(x0, 15); c.closePath(); c.fill(); };
                if (icon === "play") { tri(3, 15); }
                else if (icon === "pause") { c.fillRect(3, 1, 4, 14); c.fillRect(10, 1, 4, 14); }
                else if (icon === "next") { tri(1, 12); c.fillRect(12, 1, 3, 14); }
                else if (icon === "prev") { tri(15, 4); c.fillRect(1, 1, 3, 14); }
            }
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
        }
    }

    // Horizontal bar you can click or drag. `value` is 0..1; `moved` fires while dragging,
    // `released` once at the end (seeking waits for the release, volume follows the drag).
    component Slider: Item {
        id: slider
        property real value: 0
        readonly property bool dragging: area.pressed
        property real dragValue: 0
        readonly property real shown: dragging ? dragValue : value
        signal moved(real v)
        signal released(real v)

        height: Theme.hitTarget
        function valueAt(x) { return Math.max(0, Math.min(1, x / width)); }

        Rectangle {
            id: track
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            height: area.containsMouse || slider.dragging ? 6 : 4
            radius: height / 2
            color: Theme.borderButton
            Rectangle {
                width: parent.width * slider.shown
                height: parent.height
                radius: parent.radius
                color: Theme.accent
            }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onPressed: (m) => { slider.dragValue = slider.valueAt(m.x); slider.moved(slider.dragValue); }
            onPositionChanged: (m) => { if (pressed) { slider.dragValue = slider.valueAt(m.x); slider.moved(slider.dragValue); } }
            onReleased: (m) => slider.released(slider.valueAt(m.x))
        }
    }

    // ---------------------------------------------------------------- keys and dialog

    Shortcut { sequence: "Space"; onActivated: Player.playPause() }
    Shortcut { sequence: "Left"; onActivated: Player.seekBy(-5000) }
    Shortcut { sequence: "Right"; onActivated: Player.seekBy(5000) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: Player.previous() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: Player.next() }
    Shortcut { sequence: "Ctrl+O"; onActivated: folderDialog.open() }

    FolderDialog {
        id: folderDialog
        title: "Choose an album folder"
        onAccepted: Player.openUrl(selectedFolder)
    }

    // ---------------------------------------------------------------- page tabs (test windows)

    Row {
        id: tabs
        anchors { left: parent.left; top: parent.top; margins: 24 }
        spacing: 8
        Repeater {
            model: [{ key: "player", label: "Player" }, { key: "library", label: "Library" }]
            delegate: Rectangle {
                id: tab
                required property var modelData
                readonly property bool active: root.page === modelData.key
                width: tabLabel.implicitWidth + 32
                height: Theme.hitTarget
                radius: height / 2
                color: active ? Theme.accent : (tabMouse.containsMouse ? Theme.surfaceInput : "transparent")
                border { color: active ? "transparent" : Theme.borderButton; width: 1 }
                Label {
                    id: tabLabel
                    anchors.centerIn: parent
                    text: tab.modelData.label
                    color: tab.active ? Theme.accentText : Theme.textPrimary
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: tabMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.page = tab.modelData.key
                }
            }
        }
    }

    // ---------------------------------------------------------------- header

    Item {
        id: header
        visible: root.page === "player"
        anchors { left: parent.left; right: parent.right; top: tabs.bottom; margins: 24 }
        height: Theme.hitTarget

        Rectangle {
            id: openButton
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: openLabel.implicitWidth + 32
            height: Theme.hitTarget
            radius: height / 2
            color: openMouse.containsMouse ? Theme.surfaceInput : "transparent"
            border { color: Theme.borderButton; width: 1 }
            Label {
                id: openLabel
                anchors.centerIn: parent
                text: "Open folder…"
                font.weight: Font.DemiBold
            }
            MouseArea {
                id: openMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: folderDialog.open()
            }
        }

        Column {
            anchors { left: openButton.right; leftMargin: 16; right: status.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
            Label {
                width: parent.width
                text: Player.folder.length > 0 ? Player.folder : "No folder open. Pick an album, or pass a folder on the command line."
                color: Theme.textSecondary
            }
            Label {
                width: parent.width
                visible: Player.errorText.length > 0
                text: Player.errorText
                color: Theme.accent
                font.pixelSize: Theme.sizeMeta
            }
        }

        Column {
            id: status
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            Label {
                anchors.right: parent.right
                text: Player.deviceText
                color: Theme.textTertiary
                font.pixelSize: Theme.sizeMeta
            }
            Label {
                anchors.right: parent.right
                text: "Underruns: " + Player.underruns
                color: Player.underruns > 0 ? Theme.accent : Theme.textTertiary
                font.pixelSize: Theme.sizeMeta
            }
        }
    }

    // ---------------------------------------------------------------- track list

    ListView {
        id: list
        visible: root.page === "player"
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: transport.top; topMargin: 16 }
        clip: true
        model: Player.trackNames
        boundsBehavior: Flickable.StopAtBounds

        delegate: Rectangle {
            required property int index
            required property string modelData
            readonly property bool current: index === Player.currentIndex

            width: ListView.view.width
            height: Theme.hitTarget
            color: rowMouse.containsMouse ? Theme.surfaceCard : "transparent"

            Label {
                id: number
                anchors { left: parent.left; leftMargin: 24; verticalCenter: parent.verticalCenter }
                width: 40
                text: index + 1
                color: current ? Theme.accent : Theme.textTertiary
                font.pixelSize: Theme.sizeMeta
            }
            Label {
                anchors { left: number.right; right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
                text: modelData
                color: current ? Theme.accent : Theme.textPrimary
                font.weight: current ? Font.DemiBold : Font.Normal
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                onDoubleClicked: Player.playIndex(index)
            }
        }
    }

    // ---------------------------------------------------------------- library page (milestone 3a)

    LibraryTest {
        visible: root.page === "library"
        anchors { left: parent.left; right: parent.right; top: tabs.bottom; bottom: transport.top; topMargin: 16 }
    }

    // ---------------------------------------------------------------- transport

    Rectangle {
        id: transport
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.transportHeight
        color: Theme.surfaceCard

        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: Theme.borderSubtle
        }

        // Left: title and format badge
        Column {
            anchors { left: parent.left; leftMargin: 24; verticalCenter: parent.verticalCenter }
            width: 280
            spacing: 6
            Label {
                width: parent.width
                text: Player.title.length > 0 ? Player.title : "Nothing playing"
                font.weight: Font.DemiBold
            }
            Rectangle {
                visible: Player.badge.length > 0
                width: badgeText.implicitWidth + 12
                height: badgeText.implicitHeight + 4
                radius: 4
                color: "transparent"
                border { color: Theme.accent; width: 1 }
                Label {
                    id: badgeText
                    anchors.centerIn: parent
                    text: Player.badge
                    color: Theme.accent
                    font { pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.5 }
                }
            }
        }

        // Center: buttons and progress
        Item {
            id: center
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; bottom: parent.bottom }
            width: Math.min(640, parent.width - 2 * 340)

            Row {
                id: buttons
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 12 }
                spacing: 8
                IconButton { icon: "prev"; onClicked: Player.previous() }
                IconButton { icon: Player.playing ? "pause" : "play"; primary: true; onClicked: Player.playPause() }
                IconButton { icon: "next"; onClicked: Player.next() }
            }

            Label {
                id: posLabel
                anchors { left: parent.left; verticalCenter: progress.verticalCenter }
                width: 52
                horizontalAlignment: Text.AlignRight
                text: progress.dragging ? root.formatMs(Player.durationMs * progress.dragValue) : Player.positionText
                color: Theme.textSecondary
                font { pixelSize: Theme.sizeMeta; features: ({ "tnum": 1 }) }
            }
            Slider {
                id: progress
                anchors { left: posLabel.right; right: durLabel.left; leftMargin: 12; rightMargin: 12; top: buttons.bottom; topMargin: -8 }
                value: Player.durationMs > 0 ? Player.positionMs / Player.durationMs : 0
                onReleased: (v) => Player.seek(Math.round(v * Player.durationMs))
            }
            Label {
                id: durLabel
                anchors { right: parent.right; verticalCenter: progress.verticalCenter }
                width: 52
                text: Player.durationText
                color: Theme.textSecondary
                font { pixelSize: Theme.sizeMeta; features: ({ "tnum": 1 }) }
            }
        }

        // Right: volume
        Row {
            anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
            spacing: 12
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "Vol " + Math.round(volume.shown * 100)
                color: Theme.textTertiary
                font { pixelSize: Theme.sizeMeta; features: ({ "tnum": 1 }) }
                width: 52
            }
            Slider {
                id: volume
                width: 120
                value: Player.volume
                onMoved: (v) => Player.volume = v
            }
        }
    }

    // Time label while dragging the progress bar (the engine's own label comes from core::formatDuration).
    function formatMs(ms) {
        const s = Math.floor(ms / 1000);
        const m = Math.floor(s / 60);
        const h = Math.floor(m / 60);
        const ss = String(s % 60).padStart(2, "0");
        return h > 0 ? h + ":" + String(m % 60).padStart(2, "0") + ":" + ss : m + ":" + ss;
    }
}
