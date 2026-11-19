import QtQuick
import MusicPlayeah

// Heart for rows and lists (D42, DESIGN.md HeartButton): outline when off, filled accent when on, 44 px hit area.
// `kind` + `refId` say what it likes; the state comes from the library, so every heart of the same thing agrees.
// Headers draw their own round button with LikeState instead.
Item {
    id: heart
    property string kind: "track"
    property var refId: 0
    property string label: ""          // "Like <label>" / "Unlike <label>" for screen readers
    property int iconSize: 18
    property color idleColor: Theme.heartIdle
    readonly property alias liked: likeState.liked
    signal toggled(bool liked)

    width: Theme.hitTarget
    height: Theme.hitTarget
    opacity: refId ? 1 : 0.4

    LikeState { id: likeState; kind: heart.kind; refId: heart.refId }

    Icon {
        anchors.centerIn: parent
        name: "heart"
        size: heart.iconSize
        filled: likeState.liked
        color: likeState.liked ? Theme.accent : (mouse.containsMouse ? Theme.textSecondary : heart.idleColor)
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: heart.refId ? true : false
        cursorShape: Qt.PointingHandCursor
        onClicked: { likeState.toggle(); heart.toggled(likeState.liked); }
    }

    Accessible.role: Accessible.Button
    Accessible.name: (likeState.liked ? "Unlike " : "Like ") + heart.label
}
