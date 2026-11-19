import QtQuick
import MusicPlayeah

// 36 px search field with a "Ctrl K" hint. Focused: the border turns accent.
Item {
    id: field
    property alias text: input.text
    property alias placeholder: hint.text
    signal accepted()
    function focusField() { input.forceActiveFocus(); input.selectAll(); }
    // Focus with the cursor after the text (typing carries on), e.g. when the Search page opens with the first typed letter.
    function focusEnd() { input.forceActiveFocus(); input.cursorPosition = input.length; }

    implicitWidth: 280
    implicitHeight: Theme.hitTarget

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 36
        radius: Theme.radiusInput
        color: Theme.surfaceSidebar
        border { color: input.activeFocus ? Theme.accent : Theme.borderSubtle; width: 1 }

        Icon { id: glass; name: "search"; size: 16; color: Theme.textSecondary; x: 12; anchors.verticalCenter: parent.verticalCenter }
        Text {
            id: hint
            anchors { left: glass.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
            visible: input.text.length === 0
            text: "Search your library"
            color: Theme.textTertiary
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
        }
        TextInput {
            id: input
            anchors { left: glass.right; leftMargin: 8; right: kbd.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
            color: Theme.textPrimary
            selectionColor: Theme.accent
            selectedTextColor: Theme.textOnAccent
            font { family: Theme.fontUi; pixelSize: Theme.sizeBody }
            clip: true
            onAccepted: field.accepted()
        }
        Text {
            id: kbd
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            text: "Ctrl K"
            color: Theme.textTertiary
            font { family: Theme.fontUi; pixelSize: Theme.sizeCaps }
        }
    }
}
