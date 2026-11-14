pragma Singleton
import QtQuick

// Every color, size and font in the UI comes from here (D14). No literals in components.
QtObject {
    // Surfaces
    readonly property color bg: "#0E0E10"
    readonly property color surfaceSidebar: "#121215"
    readonly property color surfaceCard: "#16161A"
    readonly property color surfaceInput: "#1A1A1E"

    // Borders
    readonly property color borderSubtle: "#232328"
    readonly property color borderButton: "#3A3A42"

    // Text
    readonly property color textPrimary: "#F4F4F5"
    readonly property color textSecondary: "#A1A1AA"
    readonly property color textTertiary: "#8B8B94"

    // Accent: placeholder default, becomes a user setting in milestone 10
    property color accent: "#E9A25F"
    property color accentText: "#1A1206"

    // Radii
    readonly property int radiusCover: 8
    readonly property int radiusCard: 12

    // Type
    readonly property string fontFamily: "Inter"
    readonly property int sizeTitle: 28
    readonly property int sizeSection: 20
    readonly property int sizeBody: 14
    readonly property int sizeMeta: 12
    readonly property int sizeCaps: 11

    // Layout
    readonly property int hitTarget: 44
    readonly property int sidebarWidth: 240
    readonly property int transportHeight: 88
}
