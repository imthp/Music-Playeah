import QtQuick
import MusicPlayeah

// Milestone 1 test window: proves Qt, QML, the Theme singleton, the bundled Inter font
// and the QML -> app -> core path all work. The real layout arrives in milestone 4.
Window {
    id: root
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: AppInfo.name
    color: Theme.bg

    Rectangle {
        id: sidebar
        anchors { left: parent.left; top: parent.top; bottom: transport.top }
        width: Theme.sidebarWidth
        color: Theme.surfaceSidebar

        Rectangle {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: 1
            color: Theme.borderSubtle
        }

        Text {
            anchors { left: parent.left; top: parent.top; margins: 24 }
            text: "SIDEBAR"
            color: Theme.textTertiary
            font { family: Theme.fontFamily; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 1 }
        }
    }

    Column {
        anchors { left: sidebar.right; right: parent.right; top: parent.top; bottom: transport.top; margins: 40 }
        spacing: 12

        Text {
            text: AppInfo.name
            color: Theme.textPrimary
            font { family: Theme.fontFamily; pixelSize: Theme.sizeTitle; weight: Font.Bold }
        }
        Text {
            text: "Skeleton · version " + AppInfo.version
            color: Theme.textSecondary
            font { family: Theme.fontFamily; pixelSize: Theme.sizeBody }
        }
        Text {
            text: "Core check: 3723 s formats as " + AppInfo.sampleDuration
            color: Theme.textTertiary
            font { family: Theme.fontFamily; pixelSize: Theme.sizeMeta }
        }
        Rectangle {
            width: 120; height: Theme.hitTarget
            radius: height / 2
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: "Accent"
                color: Theme.accentText
                font { family: Theme.fontFamily; pixelSize: Theme.sizeBody; weight: Font.DemiBold }
            }
        }
    }

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
        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 24 }
            text: "TRANSPORT BAR"
            color: Theme.textTertiary
            font { family: Theme.fontFamily; pixelSize: Theme.sizeCaps; weight: Font.DemiBold; letterSpacing: 1 }
        }
    }
}
