.pragma library

// How a playing track is titled (SPEC §7b), shared by Now Playing and the player bar.
// A track that is a movement of a work with ≥ 2 movements in the playing list is titled by its movement
// ("I. Praeludio") under the library's work title ("Lute Suite No. 1 in E minor", without per-file wording such as
// "(Arr. for Guitar)"). Other tracks keep their own title. `t` and `items` are maps from Library.albumTracks().

function roman(n) {
    if (n <= 0) return "";
    if (n >= 40) return String(n);
    const table = [[10, "X"], [9, "IX"], [5, "V"], [4, "IV"], [1, "I"]];
    let out = "";
    for (const [v, r] of table) while (n >= v) { out += r; n -= v; }
    return out;
}

// "Suite in E minor, BWV 996" + "BWV 996" -> "Suite in E minor" (the catalog number is shown on its own).
function stripCatalog(title, catalog) {
    if (!catalog) return title;
    const i = title.lastIndexOf(catalog);
    if (i <= 0) return title;
    return (title.slice(0, i) + title.slice(i + catalog.length)).replace(/[\s,;:–-]+$/, "").trim();
}

// {work, catalog, number ("I."), name, position, total} or null when `t` is not a movement.
function movement(t, items) {
    if (!t || !t.workId || !items) return null;
    const all = [];
    for (let i = 0; i < items.length; ++i) {
        const x = items[i];
        if (x.workId === t.workId && x.albumId === t.albumId) all.push(x);
    }
    if (all.length < 2) return null;
    let position = 0;
    for (let i = 0; i < all.length; ++i) if (all[i].id === t.id) position = i + 1;
    return {
        work: stripCatalog(t.workTitle || "", t.catalog || ""),
        catalog: t.catalog || "",
        number: t.movementNo > 0 ? roman(t.movementNo) + "." : "",
        name: (t.movementName || "").length > 0 ? t.movementName : t.title,
        position: position,
        total: all.length
    };
}

// "I. Praeludio" for a movement, else the track's own title.
function shortTitle(t, items) {
    const m = movement(t, items);
    return m ? (m.number ? m.number + " " : "") + m.name : (t && t.title) || "";
}

// The performer: the album artist when the track also names a composer (raw artist tags on classical files read
// "Bach, Composer · Shibe, Artist, MainArtist"); else the artist tag, line breaks joined with " · ".
function performer(t) {
    if (!t) return "";
    if ((t.composer || "").length > 0 && (t.albumArtist || "").length > 0) return t.albumArtist;
    return (t.artist || "").replace(/\s*[\r\n]+\s*/g, " · ");
}
