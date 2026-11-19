import QtQuick
import MusicPlayeah

// The like state of one thing (D42): `liked` follows the library's likes, `toggle()` flips it (optimistic, written through).
// Used by HeartButton and by header buttons that draw their own heart:
//   LikeState { id: like; kind: "album"; refId: page.albumId }   ...   onClicked: like.toggle()
QtObject {
    property string kind: "track"      // track | album | artist | composer | work
    property var refId: 0              // the row id; 0 = nothing to like yet

    // Reading likesRevision makes the binding re-run whenever any like changes.
    readonly property bool liked: {
        Library.likesRevision;
        return refId ? Library.isLiked(kind, refId) : false;
    }
    function toggle() { if (refId) Library.toggleLike(kind, refId); }
}
