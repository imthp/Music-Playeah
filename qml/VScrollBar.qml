import QtQuick
import MusicPlayeah

// Thin vertical scroll bar for a ListView, GridView or other Flickable (custom: no Qt Quick Controls).
// Place it beside the view: VScrollBar { flickable: myList; anchors { left: myList.right; top: myList.top; bottom: myList.bottom } }
// Shows where the view is; drag the thumb, or click the track to jump there. Hidden when everything fits.
Item {
    id: bar

    property Flickable flickable: null

    readonly property real ratio: flickable ? Math.max(0, Math.min(1, flickable.visibleArea.heightRatio)) : 1
    // 0 = top, 1 = bottom
    readonly property real position: (flickable && ratio < 1)
        ? Math.max(0, Math.min(1, flickable.visibleArea.yPosition / (1 - ratio))) : 0
    readonly property bool active: mouse.containsMouse || mouse.pressed || (flickable !== null && flickable.moving)

    width: 10
    visible: flickable !== null && ratio < 0.999

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceCard
        opacity: bar.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    Rectangle {
        id: thumb
        x: 2
        width: parent.width - 4
        height: Math.max(28, bar.height * bar.ratio)
        y: (bar.height - height) * bar.position
        radius: width / 2
        color: mouse.pressed ? Theme.textSecondary : Theme.textTertiary
        opacity: bar.active ? 1 : 0.6
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        property real grab: 0  // where in the thumb it was taken

        onPressed: (event) => {
            if (event.y >= thumb.y && event.y <= thumb.y + thumb.height) {
                grab = event.y - thumb.y;
            } else {
                grab = thumb.height / 2;
                bar.scrollTo(event.y - grab);
            }
        }
        onPositionChanged: (event) => {
            if (pressed) {
                bar.scrollTo(event.y - grab);
            }
        }
        // The bar sits beside the view, so the wheel over it scrolls the view here.
        onWheel: (wheel) => {
            const top = bar.flickable.originY;
            const bottom = top + Math.max(0, bar.flickable.contentHeight - bar.flickable.height);
            bar.flickable.contentY = Math.max(top, Math.min(bottom, bar.flickable.contentY - wheel.angleDelta.y));
        }
    }

    function scrollTo(thumbY) {
        const range = bar.height - thumb.height;
        const p = range > 0 ? Math.max(0, Math.min(1, thumbY / range)) : 0;
        flickable.contentY = flickable.originY + p * Math.max(0, flickable.contentHeight - flickable.height);
    }
}
