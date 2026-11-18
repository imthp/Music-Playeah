import QtQuick
import MusicPlayeah

// Cover image with rounded corners, cheap enough for a fast-scrolling grid.
// A Rectangle can't clip its children to a radius, and a mask effect (MultiEffect) costs three off-screen layers per
// image, which made the Albums grid lag when scrolling fast. Instead a ring in the background color is drawn over the
// image's corners: a rounded rectangle with a `backdrop`-colored border whose inner edge has the cover's radius.
// Native scene-graph rectangle, no layers, no shaders. Needs a solid backdrop behind the image (true for the grid,
// the player bar and the album header). Over a blurred backdrop (Now Playing) pass `masked: true` for the effect.
Item {
    id: root
    property alias source: img.source
    property alias sourceSize: img.sourceSize
    property alias status: img.status
    property real radius: Theme.radiusCover
    property color backdrop: Theme.bg

    Image {
        id: img
        anchors.fill: parent
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: false
    }
    // Ring: outer edge `w` px outside the image, inner edge exactly on the image with radius `root.radius`.
    // w >= 0.42 * radius is enough to cover each corner fully (the corner sits radius·√2 from the arc's center).
    Rectangle {
        readonly property real w: Math.ceil(root.radius)
        anchors { fill: parent; margins: -w }
        radius: root.radius + w
        color: "transparent"
        border { width: w; color: root.backdrop }
        antialiasing: true
        visible: root.radius > 0
    }
}
