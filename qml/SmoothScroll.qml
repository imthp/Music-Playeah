import QtQuick
import MusicPlayeah

// Smooth mouse-wheel scrolling for a list or grid (M4b). Qt's own wheel handling turns each notch into a short
// flick, which on Windows moves by uneven amounts and feels jumpy. Here a notch moves a fixed distance with a short
// ease-out, and notches in a row add up (scrolling fast keeps going). Touchpads (pixel deltas) scroll 1:1.
// Lay it over the view: SmoothScroll { anchors.fill: list; flickable: list }. Clicks and hover pass through.
MouseArea {
    id: wheel
    property Flickable flickable
    property real notch: 150           // px per wheel notch
    property real goal: 0

    acceptedButtons: Qt.NoButton

    function clamp(y) {
        const f = flickable;
        const minY = f.originY - f.topMargin;
        const maxY = Math.max(minY, f.originY + f.contentHeight + f.bottomMargin - f.height);
        return Math.max(minY, Math.min(maxY, y));
    }

    NumberAnimation {
        id: anim
        target: wheel.flickable
        property: "contentY"
        duration: 220
        easing.type: Easing.OutCubic
        onFinished: if (wheel.flickable) wheel.flickable.returnToBounds()
    }

    onWheel: (w) => {
        const f = flickable;
        if (!f || Math.abs(w.angleDelta.x) > Math.abs(w.angleDelta.y)) { w.accepted = false; return; }
        f.cancelFlick();
        if (w.pixelDelta.y !== 0) {             // touchpad: follow the fingers
            anim.stop();
            f.contentY = clamp(f.contentY - w.pixelDelta.y);
            goal = f.contentY;
            return;
        }
        const from = anim.running ? goal : f.contentY;
        goal = clamp(from - w.angleDelta.y / 120 * notch);
        anim.stop();
        anim.from = f.contentY;
        anim.to = goal;
        anim.start();
    }
}
