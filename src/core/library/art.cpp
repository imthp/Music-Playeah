#include "core/library/art.hpp"

// fileref.h first: some TagLib 1.x headers don't include what they use.
#include <taglib/fileref.h>
#include <taglib/taglib.h>
#include <taglib/tpropertymap.h>

#include <taglib/aifffile.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/opusfile.h>
#include <taglib/vorbisfile.h>
#include <taglib/wavfile.h>
#include <taglib/xiphcomment.h>
#if TAGLIB_MAJOR_VERSION >= 2
#include <taglib/tvariant.h>  // complexProperties(): used for DSF/DSDIFF, which only TagLib 2 can open
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>

#include "core/text.hpp"

namespace mp::core::library {

namespace fs = std::filesystem;
namespace tl = TagLib;

namespace {

std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

constexpr std::string_view kEmbedded = "embedded:";
constexpr std::string_view kFile = "file:";

}  // namespace

namespace {

// The best picture seen so far: a front cover beats anything else, the first one wins among equals.
struct PictureChoice {
    Bytes data;
    bool front = false;

    void offer(const tl::ByteVector& bytes, bool isFront) {
        if (bytes.isEmpty() || (!data.empty() && (front || !isFront))) {
            return;
        }
        data.assign(bytes.begin(), bytes.end());
        front = isFront;
    }
};

void fromId3v2(tl::ID3v2::Tag* tag, PictureChoice& out) {
    if (tag == nullptr) {
        return;
    }
    const tl::ID3v2::FrameList frames = tag->frameList("APIC");
    for (const tl::ID3v2::Frame* frame : frames) {
        if (const auto* pic = dynamic_cast<const tl::ID3v2::AttachedPictureFrame*>(frame)) {
            out.offer(pic->picture(), pic->type() == tl::ID3v2::AttachedPictureFrame::FrontCover);
        }
    }
}

void fromXiph(tl::Ogg::XiphComment* tag, PictureChoice& out) {
    if (tag == nullptr) {
        return;
    }
    for (const tl::FLAC::Picture* pic : tag->pictureList()) {
        out.offer(pic->data(), pic->type() == tl::FLAC::Picture::FrontCover);
    }
}

}  // namespace

Bytes readEmbeddedPicture(const fs::path& file) {
    try {
        const fs::path openable = longPathSafe(file);
        // No audio properties: only the tags are needed.
        const tl::FileRef ref(openable.c_str(), false);
        if (ref.isNull() || ref.file() == nullptr || !ref.file()->isValid()) {
            return {};
        }
        tl::File* f = ref.file();
        PictureChoice best;
        if (auto* flac = dynamic_cast<tl::FLAC::File*>(f)) {
            for (const tl::FLAC::Picture* pic : flac->pictureList()) {
                best.offer(pic->data(), pic->type() == tl::FLAC::Picture::FrontCover);
            }
        } else if (auto* mpeg = dynamic_cast<tl::MPEG::File*>(f)) {
            fromId3v2(mpeg->ID3v2Tag(), best);
        } else if (auto* wav = dynamic_cast<tl::RIFF::WAV::File*>(f)) {
            fromId3v2(wav->ID3v2Tag(), best);
        } else if (auto* aiff = dynamic_cast<tl::RIFF::AIFF::File*>(f)) {
            fromId3v2(aiff->tag(), best);
        } else if (auto* mp4 = dynamic_cast<tl::MP4::File*>(f)) {
            if (mp4->tag() != nullptr && mp4->tag()->contains("covr")) {
                bool first = true;
                for (const tl::MP4::CoverArt& art : mp4->tag()->item("covr").toCoverArtList()) {
                    best.offer(art.data(), first);  // no picture types in MP4: the first one is the cover
                    first = false;
                }
            }
        } else if (auto* vorbis = dynamic_cast<tl::Ogg::Vorbis::File*>(f)) {
            fromXiph(vorbis->tag(), best);
        } else if (auto* opus = dynamic_cast<tl::Ogg::Opus::File*>(f)) {
            fromXiph(opus->tag(), best);
        }
#if TAGLIB_MAJOR_VERSION >= 2
        else {
            // DSF / DSDIFF (ID3v2 inside) and anything else TagLib 2 knows.
            for (const tl::VariantMap& picture : f->complexProperties("PICTURE")) {
                const auto data = picture.find("data");
                const auto type = picture.find("pictureType");
                if (data != picture.end()) {
                    best.offer(data->second.toByteVector(),
                               type != picture.end() && type->second.toString() == "Front Cover");
                }
            }
        }
#endif
        return best.data;
    } catch (...) {
        return {};
    }
}

std::optional<fs::path> findCoverFile(const fs::path& folder) {
    static constexpr std::string_view kBases[] = {"cover", "folder", "front"};
    static constexpr std::string_view kExtensions[] = {".jpg", ".jpeg", ".png", ".bmp", ".webp"};
    std::error_code ec;
    std::optional<fs::path> best;
    std::size_t bestRank = 99;
    for (fs::directory_iterator it(longPathSafe(folder), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code typeEc;
        if (!it->is_regular_file(typeEc)) {
            continue;
        }
        const std::string stem = lowerAscii(pathToUtf8(it->path().stem()));
        const std::string ext = lowerAscii(pathToUtf8(it->path().extension()));
        if (std::find(std::begin(kExtensions), std::end(kExtensions), ext) == std::end(kExtensions)) {
            continue;
        }
        for (std::size_t rank = 0; rank < std::size(kBases); ++rank) {
            if (stem == kBases[rank] && rank < bestRank) {
                bestRank = rank;
                best = folder / it->path().filename();
            }
        }
    }
    return best;
}

std::string hashImage(const Bytes& bytes) {
    std::uint64_t h = 14695981039346656037ULL;
    for (const std::uint8_t b : bytes) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[h & 0xF];
        h >>= 4;
    }
    return out;
}

std::string imageExtension(const Bytes& b) {
    auto starts = [&](std::initializer_list<int> magic) {
        if (b.size() < magic.size()) {
            return false;
        }
        std::size_t i = 0;
        for (const int m : magic) {
            if (b[i++] != static_cast<std::uint8_t>(m)) {
                return false;
            }
        }
        return true;
    };
    if (starts({0xFF, 0xD8, 0xFF})) {
        return "jpg";
    }
    if (starts({0x89, 'P', 'N', 'G'})) {
        return "png";
    }
    if (starts({'G', 'I', 'F', '8'})) {
        return "gif";
    }
    if (starts({'B', 'M'})) {
        return "bmp";
    }
    if (b.size() > 12 && starts({'R', 'I', 'F', 'F'}) && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P') {
        return "webp";
    }
    return "";
}

namespace {

int be16(const Bytes& b, std::size_t i) { return (b[i] << 8) | b[i + 1]; }
int le16(const Bytes& b, std::size_t i) { return b[i] | (b[i + 1] << 8); }
int le24(const Bytes& b, std::size_t i) { return b[i] | (b[i + 1] << 8) | (b[i + 2] << 16); }
std::int32_t le32(const Bytes& b, std::size_t i) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(b[i]) | (static_cast<std::uint32_t>(b[i + 1]) << 8) |
                                     (static_cast<std::uint32_t>(b[i + 2]) << 16) | (static_cast<std::uint32_t>(b[i + 3]) << 24));
}

ImageSize jpegSize(const Bytes& b) {
    std::size_t i = 2;
    while (i + 9 <= b.size()) {
        if (b[i] != 0xFF) {
            ++i;  // not at a marker (shouldn't happen between segments): resync
            continue;
        }
        const int marker = b[i + 1];
        if (marker == 0xFF) {  // fill byte
            ++i;
            continue;
        }
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {  // no length field
            i += 2;
            continue;
        }
        // SOF0..SOF15 except DHT (C4), JPG (C8), DAC (CC): precision, height, width.
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            return {be16(b, i + 7), be16(b, i + 5)};
        }
        if (marker == 0xDA || marker == 0xD9) {  // start of scan / end: no SOF before it
            break;
        }
        i += 2 + static_cast<std::size_t>(be16(b, i + 2));
    }
    return {};
}

}  // namespace

ImageSize imageSize(const Bytes& b) {
    const std::string kind = imageExtension(b);
    if (kind == "jpg") {
        return jpegSize(b);
    }
    if (kind == "png" && b.size() >= 24) {
        // IHDR, big endian. Unsigned so a hostile header can't overflow `int`; the PNG spec caps sides at 2^31-1.
        auto be32 = [&](std::size_t i) {
            const std::uint32_t v = (static_cast<std::uint32_t>(be16(b, i)) << 16) | static_cast<std::uint32_t>(be16(b, i + 2));
            return static_cast<int>(std::min<std::uint32_t>(v, 0x7FFFFFFFu));
        };
        return {be32(16), be32(20)};
    }
    if (kind == "gif" && b.size() >= 10) {
        return {le16(b, 6), le16(b, 8)};
    }
    if (kind == "bmp" && b.size() >= 26) {
        // Height is negative for top-down bitmaps. Widen before abs(): abs(INT_MIN) is undefined.
        auto side = [&](std::size_t i) {
            return static_cast<int>(std::min<std::int64_t>(std::abs(static_cast<std::int64_t>(le32(b, i))), 0x7FFFFFFF));
        };
        return {side(18), side(22)};
    }
    if (kind == "webp" && b.size() >= 30) {
        if (b[12] == 'V' && b[13] == 'P' && b[14] == '8' && b[15] == ' ') {
            return {le16(b, 26) & 0x3FFF, le16(b, 28) & 0x3FFF};
        }
        if (b[12] == 'V' && b[13] == 'P' && b[14] == '8' && b[15] == 'L' && b.size() >= 25) {
            const int w = 1 + (b[21] | ((b[22] & 0x3F) << 8));
            const int h = 1 + ((b[22] >> 6) | (b[23] << 2) | ((b[24] & 0x0F) << 10));
            return {w, h};
        }
        if (b[12] == 'V' && b[13] == 'P' && b[14] == '8' && b[15] == 'X') {
            return {1 + le24(b, 24), 1 + le24(b, 27)};
        }
    }
    return {};
}

namespace {

// The first `limit` bytes of a file (enough for any image header).
Bytes readHead(const fs::path& path, std::size_t limit) {
    std::ifstream in(longPathSafe(path), std::ios::binary);
    if (!in) {
        return {};
    }
    Bytes out(limit);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(limit));
    out.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
    return out;
}

constexpr std::size_t kHeaderBytes = 256 * 1024;

}  // namespace

Bytes loadArtSource(const std::string& source) {
    if (source.compare(0, kEmbedded.size(), kEmbedded) == 0) {
        return readEmbeddedPicture(pathFromUtf8(std::string_view(source).substr(kEmbedded.size())));
    }
    if (source.compare(0, kFile.size(), kFile) == 0) {
        const fs::path path = longPathSafe(pathFromUtf8(std::string_view(source).substr(kFile.size())));
        // A cover file is chosen by name only, so a huge file called "cover.jpg" must not be read whole into memory.
        constexpr std::uintmax_t kMaxCoverFileBytes = 64ull * 1024 * 1024;
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);
        if (ec || size == 0 || size > kMaxCoverFileBytes) {
            return {};
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return {};
        }
        Bytes out(static_cast<std::size_t>(size));
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        out.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
        return out;
    }
    return {};
}

ArtStats resolveArt(Database& db, const std::atomic<bool>* cancel) {
    ArtStats stats;

    struct Todo {
        std::int64_t albumId = 0;
        std::vector<std::string> paths;  // first tracks of the album, disc and track order
    };
    std::vector<Todo> todo;
    {
        Statement albums = db.prepare("SELECT id FROM albums WHERE art_checked = 0 ORDER BY id");
        std::vector<std::int64_t> ids;
        while (albums.step()) {
            ids.push_back(albums.getInt(0));
        }
        // Existing tracks first, so an unplugged drive does not hide a cover that is elsewhere.
        Statement tracks = db.prepare(
            "SELECT path FROM tracks WHERE album_id = ?1 ORDER BY (missing_since IS NOT NULL), disc_no, track_no, path LIMIT 3");
        for (const std::int64_t id : ids) {
            Todo t;
            t.albumId = id;
            tracks.bind(1, id);
            while (tracks.step()) {
                t.paths.push_back(tracks.getText(0));
            }
            tracks.reset();
            todo.push_back(std::move(t));
        }
    }

    Statement setArt = db.prepare("UPDATE albums SET art_hash = ?1, art_source = ?2, art_checked = 1 WHERE id = ?3");
    constexpr std::size_t kBatch = 100;
    std::size_t inBatch = 0;
    std::optional<Transaction> tx;
    for (const Todo& t : todo) {
        if (cancel != nullptr && cancel->load()) {
            break;
        }
        if (!tx) {
            tx.emplace(db);
        }
        std::string hash;
        std::string source;
        ImageSize embeddedSize;
        for (const std::string& path : t.paths) {
            const Bytes picture = readEmbeddedPicture(pathFromUtf8(path));
            if (!picture.empty()) {
                hash = hashImage(picture);
                source = std::string(kEmbedded) + path;
                embeddedSize = imageSize(picture);
                break;
            }
        }
        // A cover file beside the tracks replaces the embedded picture when it is larger (D46): embedded art is
        // often ~500 px, a scan in the folder 1200-3000 px. Only its header is read unless it wins.
        if (!t.paths.empty()) {
            const fs::path folder = pathFromUtf8(t.paths.front()).parent_path();
            if (const std::optional<fs::path> cover = findCoverFile(folder)) {
                const bool wins = source.empty() || imageSize(readHead(*cover, kHeaderBytes)).area() > embeddedSize.area();
                if (wins) {
                    const Bytes picture = loadArtSource(std::string(kFile) + genericUtf8(*cover));
                    if (!picture.empty()) {
                        hash = hashImage(picture);
                        source = std::string(kFile) + genericUtf8(*cover);
                    }
                }
            }
        }
        if (source.empty()) {
            setArt.bind(1, nullptr).bind(2, nullptr);
        } else {
            setArt.bind(1, hash).bind(2, source);
            ++stats.withArt;
        }
        setArt.bind(3, t.albumId);
        setArt.run();
        ++stats.albumsChecked;
        if (++inBatch >= kBatch) {
            tx->commit();
            tx.reset();
            inBatch = 0;
        }
    }
    if (tx) {
        tx->commit();
    }
    return stats;
}

}  // namespace mp::core::library
