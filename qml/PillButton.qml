import QtQuick
import MusicPlayeah

// 44 px pill button with an optional icon. `primary`: accent fill, dark text (Play). Otherwise a 1 px outline.
// With no `text` it is a 44 px circle (heart, queue, more). `enabledAction: false` draws it dimmed (coming later).
Item {
    id: btn
    property string text
    property string icon
    property bool primary: false
    property bool enabledAction: true
    property color iconColor: primary ? Theme.textOnAccent : Theme.textPrimary
    property bool iconFilled: false     // a liked heart is filled
    signal clicked()

    implicitHeight: Theme.hitTarget
    implicitWidth: text.length > 0 ? row.implicitWidth + (primary ? 44 : 40) : Theme.hitTarget
    opacity: enabledAction ? 1 : 0.4

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: btn.primary ? (mouse.containsMouse ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
                           : (mouse.containsMouse && btn.enabledAction ? Theme.rowHover : "transparent")
        border { width: btn.primary ? 0 : 1; color: Theme.borderButton }
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: btn.icon.length > 0
            name: btn.icon; size: 18
            color: btn.iconColor
            filled: btn.iconFilled || btn.icon === "play"
            strokeWidth: btn.icon === "more" ? 3 : 1.8
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: btn.text.length > 0
            text: btn.text
            color: btn.primary ? Theme.textOnAccent : Theme.textPrimary
            font { family: Theme.fontUi; pixelSize: 15; weight: Font.DemiBold }
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: btn.enabledAction
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }
}
