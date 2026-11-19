import QtQuick
import MusicPlayeah

// A work in a list (Search, Liked): round play button · title + catalog number · "Lute Suite · 2 recordings" · heart.
// 52 px tall (M4-Search). Click the row for the composer's page; the play button plays the work.
Item {
    id: row
    property var work                   // {id, title, catalog, subtitle (composer), ...}
    property string meta                // second line
    property bool showHeart: true
    signal play()
    signal clicked()

    // Rich text needs &, < escaped.
    function esc(t) { return t.replace(/&/g, "&amp;").replace(/</g, "&lt;"); }

    height: 52

    Rectangle { anchors.fill: parent; radius: Theme.radiusInput; color: mouse.containsMouse ? Theme.rowHover : "transparent" }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: row.clicked() }

    Rectangle {
        id: playButton
        x: 12; anchors.verticalCenter: parent.verticalCenter
        width: 32; height: 32; radius: 16
        color: playMouse.containsMouse ? Theme.rowHover : "transparent"
        border { width: 1; color: Theme.borderButton }
        Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 1; name: "play"; size: 12; filled: true; color: Theme.textPrimary }
        MouseArea {
            id: playMouse
            anchors { fill: parent; margins: -6 }
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: row.play()
        }
    }
    Column {
        anchors { left: playButton.right; leftMargin: 14; right: heart.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
        spacing: 1
        Text {
            width: parent.width
            textFormat: Text.StyledText
            text: row.work ? (row.work.catalog && row.work.catalog.length > 0
                  ? esc(row.work.title) + "  <font color='" + Theme.textTertiary + "'>" + esc(row.work.catalog) + "</font>" : esc(row.work.title)) : ""
            elide: Text.ElideRight; maximumLineCount: 1
            color: Theme.textPrimary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody; weight: Font.Medium }
        }
        Text {
            width: parent.width
            text: row.meta
            elide: Text.ElideRight; maximumLineCount: 1
            color: Theme.textSecondary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1 }
        }
    }
    HeartButton {
        id: heart
        visible: row.showHeart
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        kind: "work"
        refId: row.work ? row.work.id : 0
        label: row.work ? row.work.title : ""
    }
}
