import QtQuick
import MusicPlayeah

// One row of an album grid inside a ListView (Artist, Liked): the page cuts its album list into rows of `cols`
// (Format.chunk) so the whole page stays one virtualized list. `cellWidth` is the slot of one card.
Item {
    id: row
    property var albums: []
    property int cover: 180
    property real cellWidth: 200
    signal albumClicked(var album)

    height: cover + 56 + 24

    Repeater {
        model: row.albums
        delegate: Item {
            id: cell
            required property var modelData
            required property int index
            x: index * row.cellWidth
            width: row.cellWidth; height: row.height
            AlbumCard {
                album: cell.modelData
                cover: row.cover
                onClicked: row.albumClicked(cell.modelData)
                onPlayClicked: Player.playTracks(Library.albumTracks(cell.modelData.id), 0, cell.modelData.title)
            }
        }
    }
}
