pragma Singleton
import QtQuick

// Every color, size and font in the UI comes from here (D14). No literals in components.
// Values follow design/DESIGN.md (approved 2026-11-18, D44).
QtObject {
    // Surfaces
    readonly property color bg: "#0E0E10"
    readonly property color surfaceSidebar: "#111114"
    readonly property color surfaceCard: "#16161A"
    readonly property color surfaceRaised: "#141417"
    readonly property color surfaceLastfm: "#17171B"
    readonly property color surfaceInput: "#1A1A1E"      // milestone 1-3 test pages only

    // Borders and lines
    readonly property color borderSubtle: "#232328"
    readonly property color borderButton: "#3A3A42"
    readonly property color chipBorder: "#2E2E35"
    readonly property color track: "#2E2E35"

    // Text
    readonly property color textPrimary: "#F4F4F5"
    readonly property color textNav: "#E4E4E7"
    readonly property color textMuted: "#C9C9D0"
    readonly property color textSecondary: "#A1A1AA"
    readonly property color textTertiary: "#8B8B94"
    readonly property color heartIdle: "#5A5A63"
    readonly property color avatarBg: "#1E1E23"          // round placeholder for artist / composer photos (M9 brings photos)
    readonly property color avatarBorder: "#2A2A30"
    readonly property color knob: "#F4F4F5"

    // States
    readonly property color rowHover: "#16161A"
    readonly property color success: "#6FCF97"

    // Accent: placeholder default, becomes a user setting in milestone 10. Never hard-code it in components.
    property color accent: "#E9A25F"
    readonly property color accentTint: Qt.rgba(accent.r, accent.g, accent.b, 0.14)   // playing row, active nav item
    readonly property color textOnAccent: "#121212"
    readonly property color accentText: textOnAccent                                   // old name, used by the test pages

    // Radii
    readonly property int radiusCover: 8
    readonly property int radiusInput: 8
    readonly property int radiusCard: 12

    // Type: two font roles (D45), both Inter by default
    property string fontDisplay: "Inter"
    property string fontUi: "Inter"
    readonly property string fontFamily: fontUi                                        // old name, used by the test pages
    readonly property int sizeAlbumTitle: 44
    readonly property int sizeTitle: 28
    readonly property int sizeSection: 20
    readonly property int sizeBody: 14
    readonly property int sizeMeta: 12
    readonly property int sizeCaps: 11

    // Spacing
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 28
    readonly property int space7: 36

    // Layout
    readonly property int hitTarget: 44
    readonly property int sidebarWidth: 240
    readonly property int transportHeight: 107
    readonly property int pagePaddingTop: 24
    readonly property int pagePaddingSides: 36
    readonly property int pagePaddingBottom: 48
    readonly property int coverSizeMin: 120
    readonly property int coverSizeMax: 320
}
