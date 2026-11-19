import QtQuick
import MusicPlayeah

// 240 px navigation (D44). `current` is the page key; `navigate(key)` is emitted on click.
Rectangle {
    id: bar
    property string current: "albums"
    signal navigate(string key)

    width: Theme.sidebarWidth
    color: Theme.surfaceSidebar
    Rectangle { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: 1; color: Theme.borderSubtle }

    component Section: Text {
        leftPadding: 10; topPadding: 12; bottomPadding: 8
        color: Theme.textSecondary
        font { family: Theme.fontUi; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 0.9; capitalization: Font.AllUppercase }
    }

    component NavItem: Item {
        id: item
        property string key
        property string label
        property string icon
        readonly property bool active: bar.current === key
        width: parent ? parent.width : 0
        height: Theme.hitTarget
        Rectangle {
            anchors { fill: parent; topMargin: 3; bottomMargin: 3 }
            radius: Theme.radiusInput
            color: item.active ? Theme.accentTint : (mouse.containsMouse ? Theme.rowHover : "transparent")
        }
        Icon {
            id: ico
            x: 10; anchors.verticalCenter: parent.verticalCenter
            name: item.icon; size: 18
            color: item.active ? Theme.accent : Theme.textSecondary
        }
        Text {
            anchors { left: ico.right; leftMargin: 12; right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            text: item.label
            elide: Text.ElideRight
            color: item.active ? Theme.textPrimary : Theme.textNav
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody; weight: item.active ? Font.DemiBold : Font.Medium }
        }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: bar.navigate(item.key) }
    }

    Column {
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 12; rightMargin: 13; topMargin: 20 }
        Row {
            spacing: 10
            leftPadding: 10; bottomPadding: 16
            Item {   // logo: ring + center dot (mockup)
                width: 22; height: 22
                anchors.verticalCenter: parent.verticalCenter
                Rectangle { x: 2; y: 2; width: 18; height: 18; radius: 9; color: "transparent"; border { width: 2; color: Theme.accent } antialiasing: true }
                Rectangle { anchors.centerIn: parent; width: 5; height: 5; radius: 2.5; color: Theme.accent; antialiasing: true }
            }
            Text {
                text: AppInfo.name
                color: Theme.textPrimary
                anchors.verticalCenter: parent.verticalCenter
                font { family: Theme.fontDisplay; pixelSize: 15; weight: Font.Bold; letterSpacing: -0.15 }
            }
        }
        Section { text: "Library" }
        NavItem { key: "albums"; label: "Albums"; icon: "albums" }
        NavItem { key: "artists"; label: "Artists"; icon: "artists" }
        NavItem { key: "songs"; label: "Songs"; icon: "songs" }
        NavItem { key: "composers"; label: "Composers"; icon: "composers" }
        NavItem { key: "liked"; label: "Liked"; icon: "heart" }
        Section { text: "Listening" }
        NavItem { key: "foryou"; label: "For You"; icon: "foryou" }
        NavItem { key: "stats"; label: "History & Stats"; icon: "stats" }
        Section { text: "Playlists" }
        NavItem { key: "newplaylist"; label: "New playlist"; icon: "plus" }
    }

    Column {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 12; rightMargin: 13; bottomMargin: 16 }
        // Last.fm status (M4-*: sidebar bottom). Placeholder until milestone 8 connects it; opens Settings.
        Item {
            width: parent.width
            height: lastfmCard.height + 4
            Rectangle {
                id: lastfmCard
                width: parent.width
                height: 52
                radius: Theme.radiusInput
                color: lastfmMouse.containsMouse ? Theme.surfaceCard : Theme.surfaceLastfm
                Rectangle {
                    id: lastfmDot
                    x: 10; anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 8; radius: 4
                    color: Theme.heartIdle      // grey: not connected (green Theme.success once M8 connects)
                }
                Column {
                    anchors { left: lastfmDot.right; leftMargin: 10; right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 1
                    Text {
                        width: parent.width
                        text: "Last.fm not connected"
                        elide: Text.ElideRight
                        color: Theme.textPrimary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1; weight: Font.DemiBold }
                    }
                    Text {
                        width: parent.width
                        text: "Scrobbling comes later"
                        elide: Text.ElideRight
                        color: Theme.textTertiary
                        font { family: Theme.fontUi; pixelSize: Theme.sizeMeta }
                    }
                }
                MouseArea { id: lastfmMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: bar.navigate("settings") }
            }
        }
        NavItem { key: "settings"; label: "Settings"; icon: "settings" }
    }
}
