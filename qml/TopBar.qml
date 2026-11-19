import QtQuick
import MusicPlayeah

// Top line of a detail page (M4-Artist, M4-Album...): "‹ Back label" on the left, the search field on the right.
// Typing in the field opens the Search page with what was typed (the field itself empties again).
Item {
    id: bar
    property string backLabel: "Back"
    property bool showBack: true
    property string placeholder: "Search your library"
    signal back()
    signal searchRequested(string text)

    height: Theme.hitTarget

    Row {
        id: backLink
        visible: bar.showBack
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevronLeft"; size: 16; color: backMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: bar.backLabel
            color: backMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody - 1; weight: Font.Medium }
        }
    }
    MouseArea { id: backMouse; enabled: bar.showBack; anchors { fill: backLink; margins: -10 } hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: bar.back() }

    SearchField {
        id: search
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: 300
        placeholder: bar.placeholder
        onTextChanged: {
            if (text.length > 0) {
                const typed = text;
                text = "";
                bar.searchRequested(typed);
            }
        }
    }
}
