import QtQuick
import MusicPlayeah
import "Format.js" as Format

// Round placeholder for an artist or composer: initials on a dark disc (photos arrive with M9).
Rectangle {
    id: avatar
    property string name
    property int diameter: 88

    width: diameter; height: diameter
    radius: diameter / 2
    color: Theme.avatarBg
    border { width: 1; color: Theme.avatarBorder }

    Text {
        anchors.centerIn: parent
        text: Format.initials(avatar.name)
        color: Theme.textSecondary
        font { family: Theme.fontDisplay; pixelSize: Math.round(avatar.diameter * 0.33); weight: Font.Bold; letterSpacing: -0.02 * Math.round(avatar.diameter * 0.33) }
    }
}
