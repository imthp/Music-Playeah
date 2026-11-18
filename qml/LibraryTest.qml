import QtQuick
import QtQuick.Dialogs
import MusicPlayeah

// Milestone 3a test view: add/remove music folders, scan with a progress bar and cancel, and
// show the counts. Enough to check "done when": the library scans, a rescan is fast, a missing
// folder marks tracks missing, and the window never freezes. The real library UI is milestone 4.
Item {
    id: page

    readonly property int pad: 24

    // ---------------------------------------------------------------- small components

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

        implicitWidth: pillLabel.implicitWidth + 32
        height: Theme.hitTarget
        radius: height / 2
        opacity: enabled ? 1.0 : 0.4
        color: primary ? Theme.accent : (pillMouse.containsMouse ? Theme.surfaceInput : "transparent")
        border { color: primary ? "transparent" : Theme.borderButton; width: 1 }

        Text {
            id: pillLabel
            anchors.centerIn: parent
            text: pill.text
            color: pill.primary ? Theme.accentText : Theme.textPrimary
            font { family: Theme.fontFamily; pixelSize: Theme.sizeBody; weight: Font.DemiBold }
        }
        MouseArea {
            id: pillMouse
            anchors.fill: parent
            enabled: pill.enabled
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
        }
    }

    component Stat: Column {
        property string label
        property int value: 0
        property bool highlight: false
        spacing: 2
        Text {
            text: parent.value.toLocaleString(Qt.locale(), "f", 0)
            color: parent.highlight && parent.value > 0 ? Theme.accent : Theme.textPrimary
            font { family: Theme.fontFamily; pixelSize: Theme.sizeTitle; weight: Font.Bold; features: ({ "tnum": 1 }) }
        }
        CapsLabel { text: parent.label }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose a music folder"
        onAccepted: Library.addFolder(selectedFolder)
    }

    // ---------------------------------------------------------------- content

    Column {
        id: top
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: page.pad }
        spacing: 20

        Row {
            spacing: 12
            Pill {
                text: "Add music folder…"
                primary: true
                enabled: Library.available && !Library.scanning
                onClicked: folderDialog.open()
            }
            Pill {
                text: "Rescan"
                enabled: Library.available && !Library.scanning && Library.folders.length > 0
                onClicked: Library.scan()
            }
            Pill {
                text: "Cancel"
                visible: Library.scanning
                onClicked: Library.cancelScan()
            }
        }

        Label {
            width: parent.width
            visible: Library.errorText.length > 0
            text: Library.errorText
            color: Theme.accent
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        // Watched folders
        Column {
            width: parent.width
            spacing: 8
            CapsLabel { text: "Watched folders" }
            Label {
                visible: Library.folders.length === 0
                text: "No folders yet. Add the folder (or folders) that hold your music."
                color: Theme.textSecondary
            }
            Repeater {
                model: Library.folders
                delegate: Rectangle {
                    required property string modelData
                    width: parent.width
                    height: Theme.hitTarget
                    radius: Theme.radiusCard
                    color: Theme.surfaceCard
                    Label {
                        anchors { left: parent.left; leftMargin: 16; right: removePill.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: modelData
                    }
                    Pill {
                        id: removePill
                        anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                        height: Theme.hitTarget - 8
                        text: "Remove"
                        enabled: !Library.scanning
                        onClicked: Library.removeFolder(modelData)
                    }
                }
            }
        }

        // Progress
        Column {
            width: parent.width
            spacing: 8
            visible: Library.scanning

            Rectangle {
                id: bar
                width: parent.width
                height: 6
                radius: 3
                color: Theme.borderButton
                clip: true

                Rectangle {
                    visible: !Library.indeterminate
                    width: bar.width * Library.progress
                    height: bar.height
                    radius: bar.radius
                    color: Theme.accent
                }
                Rectangle {
                    id: chunk
                    visible: Library.indeterminate
                    width: bar.width * 0.25
                    height: bar.height
                    radius: bar.radius
                    color: Theme.accent
                    SequentialAnimation on x {
                        running: Library.indeterminate && Library.scanning
                        loops: Animation.Infinite
                        NumberAnimation { from: -chunk.width; to: bar.width; duration: 1200; easing.type: Easing.InOutQuad }
                    }
                }
            }
            Label {
                width: parent.width
                text: Library.progressText
                color: Theme.textSecondary
                font.pixelSize: Theme.sizeMeta
            }
        }

        // Counts
        Row {
            spacing: 48
            Stat { label: "Tracks"; value: Library.trackCount }
            Stat { label: "Albums"; value: Library.albumCount }
            Stat { label: "Artists"; value: Library.artistCount }
            Stat { label: "Missing"; value: Library.missingCount; highlight: true }
        }

        Label {
            width: parent.width
            visible: Library.summaryText.length > 0
            text: Library.summaryText
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    // Unavailable folders, unreadable files, scan errors
    ListView {
        id: problems
        anchors { left: parent.left; right: parent.right; top: top.bottom; bottom: footer.top; leftMargin: page.pad; rightMargin: page.pad; topMargin: 12; bottomMargin: 8 }
        clip: true
        spacing: 4
        model: Library.problems
        boundsBehavior: Flickable.StopAtBounds
        delegate: Label {
            required property string modelData
            width: ListView.view.width
            text: modelData
            color: Theme.textTertiary
            font.pixelSize: Theme.sizeMeta
        }
    }
    VScrollBar { flickable: problems; anchors { left: problems.right; leftMargin: 4; top: problems.top; bottom: problems.bottom } }

    Label {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: page.pad; rightMargin: page.pad; bottomMargin: 12 }
        text: "Database: " + Library.databasePath
        color: Theme.textTertiary
        font.pixelSize: Theme.sizeMeta
        elide: Text.ElideMiddle
    }
}
