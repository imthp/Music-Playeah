.pragma library

// Small pure helpers shared by the pages (M4b). No access to the UI: everything comes in as arguments.

function plural(n, word) { return n + " " + word + (n === 1 ? "" : "s"); }

// "suite" -> "Suites", "symphony" -> "Symphonies", "mass" -> "Masses", "variations" -> "Variations".
function pluralTitle(word) {
    if (!word) return "";
    let w = word;
    if (/[^aeiou]y$/.test(w)) w = w.slice(0, -1) + "ies";
    else if (/ss$/.test(w)) w = w + "es";
    else if (!/s$/.test(w)) w = w + "s";
    return w.charAt(0).toUpperCase() + w.slice(1);
}
function capitalize(word) { return word ? word.charAt(0).toUpperCase() + word.slice(1) : ""; }

// 234000 -> "3:54", 3723000 -> "1:02:03"
function time(ms) {
    const s = Math.round(ms / 1000);
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60;
    return (h > 0 ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(ss).padStart(2, "0");
}

// "Johann Sebastian Bach" -> "JSB", "Bach, Johann Sebastian" -> "JSB", "Marlo Vance" -> "MV"; at most three letters.
function initials(name) {
    let n = name || "";
    const comma = n.indexOf(",");
    if (comma > 0) n = n.slice(comma + 1) + " " + n.slice(0, comma);      // "Surname, Given" -> "Given Surname"
    let parts = n.split(/[\s.\-]+/).filter(p => p.length > 0 && p[0].toUpperCase() !== p[0].toLowerCase());
    if (parts.length === 0) return "?";
    if (parts.length > 3) parts = [parts[0], parts[1], parts[parts.length - 1]];
    return parts.map(p => p[0]).join("").toUpperCase();
}

// When something was liked: "today", "yesterday", "3 days ago", "last week", "3 weeks ago", then the date.
function likedAgo(unixSeconds) {
    if (!unixSeconds) return "";
    const then = new Date(unixSeconds * 1000);
    const a = new Date(); a.setHours(0, 0, 0, 0);
    const b = new Date(then.getTime()); b.setHours(0, 0, 0, 0);
    const days = Math.round((a.getTime() - b.getTime()) / 86400000);
    if (days <= 0) return "today";
    if (days === 1) return "yesterday";
    if (days < 7) return days + " days ago";
    if (days < 14) return "last week";
    if (days < 30) return Math.floor(days / 7) + " weeks ago";
    return then.toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
}

// [1,2,3,4,5], 2 -> [[1,2],[3,4],[5]]  (rows of an album grid inside a ListView)
function chunk(list, n) {
    const out = [];
    const size = Math.max(1, n);
    for (let i = 0; i < list.length; i += size) out.push(list.slice(i, i + size));
    return out;
}

// A shuffled copy (Fisher-Yates).
function shuffled(list) {
    const out = list.slice();
    for (let i = out.length - 1; i > 0; --i) {
        const j = Math.floor(Math.random() * (i + 1));
        const t = out[i]; out[i] = out[j]; out[j] = t;
    }
    return out;
}

// 4 -> "IV" (movement numbers; above 39 stays a plain number)
function roman(n) {
    if (!(n > 0) || n >= 40) return String(n);
    const t = [[10, "X"], [9, "IX"], [5, "V"], [4, "IV"], [1, "I"]];
    let out = "";
    for (const [v, r] of t) while (n >= v) { out += r; n -= v; }
    return out;
}
