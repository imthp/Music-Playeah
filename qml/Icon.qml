import QtQuick
import QtQuick.Shapes
import MusicPlayeah

// Line icon drawn from an SVG path on a 24 x 24 grid (no icon font, no image files). `filled` icons (transport) also fill.
Item {
    id: icon
    property string name: ""
    property int size: 18
    property color color: Theme.textSecondary
    property bool filled: false
    // Transport glyphs are solid shapes; hearts are filled and outlined.
    readonly property bool solid: ["play", "pause", "prev", "next"].indexOf(name) >= 0
    property real strokeWidth: 1.8

    readonly property var paths: ({
        "albums": "M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z",
        "artists": "M16 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2M12 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z",
        "songs": "M9 18V5l12-2v13M6 21a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM18 19a3 3 0 1 0 0-6 3 3 0 0 0 0 6z",
        "composers": "M4 19.5A2.5 2.5 0 0 1 6.5 17H20M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z",
        "heart": "M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.7l-1.1-1.1a5.5 5.5 0 0 0-7.8 7.8l1.1 1.1L12 21.2l7.7-7.7 1.1-1.1a5.5 5.5 0 0 0 0-7.8z",
        "foryou": "M12 2l3.1 6.3 6.9 1-5 4.9 1.2 6.8L12 17.8 5.8 21l1.2-6.8-5-4.9 6.9-1z",
        "stats": "M18 20V10M12 20V4M6 20v-6",
        "playlist": "M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01",
        "plus": "M12 5v14M5 12h14",
        "settings": "M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6",
        "search": "M11 19a8 8 0 1 0 0-16 8 8 0 0 0 0 16zM21 21l-4.3-4.3",
        "queue": "M3 6h18M3 12h18M3 18h12",
        "volume": "M11 5L6 9H2v6h4l5 4zM15.5 8.5a5 5 0 0 1 0 7M19 5a10 10 0 0 1 0 14",
        "shuffle": "M16 3h5v5M4 20L21 3M21 16v5h-5M15 15l6 6M4 4l5 5",
        "repeat": "M17 2l4 4-4 4M3 11V9a3 3 0 0 1 3-3h15M7 22l-4-4 4-4M21 13v2a3 3 0 0 1-3 3H3",
        "play": "M8 5v14l11-7z",
        "pause": "M6 4h4v16H6zM14 4h4v16h-4z",
        "prev": "M6 5h2v14H6zM20 5v14L9 12z",
        "next": "M16 5h2v14h-2zM4 5v14l11-7z",
        "chevronLeft": "M15 18l-6-6 6-6",
        "chevronDown": "M6 9l6 6 6-6",
        "grid": "M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3zM14 14h7v7h-7z",
        "square": "M4 4h16v16H4z",
        "file": "M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8zM14 3v5h5",
        "more": "M12 12h.01M19 12h.01M5 12h.01"
    })

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    Shape {
        width: 24; height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.filled && icon.solid ? "transparent" : icon.color
            strokeWidth: icon.strokeWidth
            fillColor: icon.filled ? icon.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
