#include "core/library/tag_reader.hpp"

// fileref.h first: some TagLib 1.x headers (aiffproperties.h) don't include what they use.
#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

#include <taglib/aiffproperties.h>
#include <taglib/audioproperties.h>
#include <taglib/flacproperties.h>
#include <taglib/mp4file.h>
#include <taglib/mp4item.h>
#include <taglib/mp4properties.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegproperties.h>
#include <taglib/opusproperties.h>
#include <taglib/vorbisproperties.h>
#include <taglib/wavproperties.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <string_view>

#include "core/text.hpp"

namespace mp::core::library {

namespace {

namespace tl = TagLib;

std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string extensionOf(const std::filesystem::path& file) { return lowerAscii(pathToUtf8(file.extension())); }

std::string utf8(const tl::String& s) { return trimText(s.to8Bit(true)); }

// All values of the first property key that has any, joined with "; ".
std::string property(const tl::PropertyMap& props, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const auto it = props.find(key);
        if (it == props.end()) {
            continue;
        }
        std::string joined;
        for (const tl::String& value : it->second) {
            const std::string v = utf8(value);
            if (v.empty()) {
                continue;
            }
            joined += joined.empty() ? "" : "; ";
            joined += v;
        }
        if (!joined.empty()) {
            return joined;
        }
    }
    return {};
}

// MP4 atoms TagLib does not map to properties (checked against 1.13): ©wrk ©mvn ©mvi ©mvc.
// "\xC2\xA9" is © in UTF-8.
std::string mp4Text(const tl::MP4::Tag& tag, const char* atom) {
    const tl::String key(atom, tl::String::UTF8);
    if (!tag.contains(key)) {
        return {};
    }
    const tl::MP4::Item item = tag.item(key);
    const tl::StringList list = item.toStringList();
    return list.isEmpty() ? std::string() : utf8(list.front());
}

int mp4Int(const tl::MP4::Tag& tag, const char* atom) {
    const tl::String key(atom, tl::String::UTF8);
    if (!tag.contains(key)) {
        return 0;
    }
    const tl::MP4::Item item = tag.item(key);
    if (item.toInt() > 0) {
        return item.toInt();
    }
    if (item.toByte() > 0) {
        return item.toByte();
    }
    return parseLeadingInt(mp4Text(tag, atom));
}

void readAudioProperties(const tl::AudioProperties& props, const std::string& ext, FileTags& out) {
    out.sampleRate = props.sampleRate();
    out.channels = props.channels();
    out.bitrateKbps = props.bitrate();
    out.durationMs = props.lengthInMilliseconds();

    if (const auto* flac = dynamic_cast<const tl::FLAC::Properties*>(&props)) {
        out.codec = "FLAC";
        out.bitDepth = flac->bitsPerSample();
    } else if (const auto* mp4 = dynamic_cast<const tl::MP4::Properties*>(&props)) {
        const bool alac = mp4->codec() == tl::MP4::Properties::ALAC;
        out.codec = alac ? "ALAC" : "AAC";
        out.bitDepth = alac ? mp4->bitsPerSample() : 0;
    } else if (const auto* wav = dynamic_cast<const tl::RIFF::WAV::Properties*>(&props)) {
        out.codec = "WAV";
        out.bitDepth = wav->bitsPerSample();
    } else if (const auto* aiff = dynamic_cast<const tl::RIFF::AIFF::Properties*>(&props)) {
        out.codec = "AIFF";
        out.bitDepth = aiff->bitsPerSample();
    } else if (const auto* mpeg = dynamic_cast<const tl::MPEG::Properties*>(&props)) {
        out.codec = mpeg->layer() == 3 ? "MP3" : (mpeg->layer() == 2 ? "MP2" : "MP1");
    } else if (dynamic_cast<const tl::Ogg::Opus::Properties*>(&props) != nullptr) {
        out.codec = "Opus";
    } else if (dynamic_cast<const tl::Vorbis::Properties*>(&props) != nullptr) {
        out.codec = "Vorbis";
    } else if (ext == ".dsf" || ext == ".dff") {
        // TagLib 2's DSF/DSDIFF properties: sampleRate() is the DSD rate, 1 bit per sample.
        out.codec = "DSD";
        out.bitDepth = 1;
    } else {
        out.codec = ext.empty() ? "" : lowerAscii(ext.substr(1));
    }
    // Lossy formats report no bit depth even when TagLib guesses one.
    if (out.codec == "MP3" || out.codec == "MP2" || out.codec == "MP1" || out.codec == "AAC" ||
        out.codec == "Opus" || out.codec == "Vorbis") {
        out.bitDepth = 0;
    }
}

}  // namespace

int parseLeadingInt(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
        ++i;
    }
    long value = 0;
    std::size_t digits = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0 && digits < 9) {
        value = value * 10 + (text[i] - '0');
        ++i;
        ++digits;
    }
    return static_cast<int>(value);
}

int parseTotalAfterSlash(const std::string& text) {
    const std::size_t slash = text.find('/');
    return slash == std::string::npos ? 0 : parseLeadingInt(text.substr(slash + 1));
}

std::optional<double> parseGainDb(const std::string& text) {
    const std::string t = trimText(text);
    if (t.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(t.c_str(), &end);  // stops at " dB"
    if (end == t.c_str()) {
        return std::nullopt;
    }
    return value;
}

int parseYear(const std::string& text) {
    // First run of four digits: "2019", "2019-05-03", "May 2019".
    for (std::size_t i = 0; i + 4 <= text.size(); ++i) {
        bool all = true;
        for (std::size_t k = 0; k < 4; ++k) {
            all = all && std::isdigit(static_cast<unsigned char>(text[i + k])) != 0;
        }
        if (all) {
            const bool digitBefore = i > 0 && std::isdigit(static_cast<unsigned char>(text[i - 1])) != 0;
            const bool digitAfter = i + 4 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 4])) != 0;
            if (!digitBefore && !digitAfter) {
                return std::atoi(text.substr(i, 4).c_str());
            }
        }
    }
    return 0;
}

bool isLibraryFile(const std::filesystem::path& file) {
    static constexpr std::array<std::string_view, 11> kExtensions = {
        ".flac", ".mp3", ".m4a", ".dsf", ".dff", ".wav", ".aif", ".aiff", ".ogg", ".oga", ".opus"};
    const std::string ext = extensionOf(file);
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}

bool readTags(const std::filesystem::path& file, FileTags& out, std::string& error) {
    out = FileTags{};
    try {
        // path::c_str() is wchar_t* on Windows and char* elsewhere; TagLib's FileName takes both.
        // Average (not Fast): VBR MP3s without a Xing header get a measured duration, not a guess.
        // TagLib opens files with the plain Windows API: paths over 260 characters need the long form.
        const std::filesystem::path openable = longPathSafe(file);
        const tl::FileRef ref(openable.c_str(), true, tl::AudioProperties::Average);
        if (ref.isNull() || ref.file() == nullptr || !ref.file()->isValid()) {
            error = "unreadable or unsupported file";
            return false;
        }
        const tl::AudioProperties* audio = ref.audioProperties();
        if (audio == nullptr) {
            error = "no audio properties";
            return false;
        }
        readAudioProperties(*audio, extensionOf(file), out);

        const tl::PropertyMap props = ref.file()->properties();
        out.title = property(props, {"TITLE"});
        out.artist = property(props, {"ARTIST"});
        out.albumArtist = property(props, {"ALBUMARTIST", "ALBUM ARTIST"});
        out.album = property(props, {"ALBUM"});
        out.composer = property(props, {"COMPOSER"});
        out.genre = property(props, {"GENRE"});
        out.workTag = property(props, {"WORK"});
        out.movementName = property(props, {"MOVEMENTNAME"});

        const std::string track = property(props, {"TRACKNUMBER"});
        const std::string disc = property(props, {"DISCNUMBER"});
        out.trackNo = parseLeadingInt(track);
        out.discNo = parseLeadingInt(disc);

        const std::string movement = property(props, {"MOVEMENTNUMBER", "MOVEMENT"});
        out.movementNo = parseLeadingInt(movement);
        out.movementTotal = parseLeadingInt(property(props, {"MOVEMENTTOTAL", "MOVEMENTCOUNT"}));
        if (out.movementTotal == 0) {
            out.movementTotal = parseTotalAfterSlash(movement);  // ID3 MVIN can be "2/4"
        }

        out.year = parseYear(property(props, {"DATE", "YEAR", "ORIGINALDATE", "ORIGINALYEAR"}));
        out.rgTrackGain = parseGainDb(property(props, {"REPLAYGAIN_TRACK_GAIN"}));
        out.rgAlbumGain = parseGainDb(property(props, {"REPLAYGAIN_ALBUM_GAIN"}));

        if (const auto* mp4 = dynamic_cast<tl::MP4::File*>(ref.file()); mp4 != nullptr && mp4->tag() != nullptr) {
            const tl::MP4::Tag& tag = *mp4->tag();
            if (out.workTag.empty()) {
                out.workTag = mp4Text(tag, "\xC2\xA9wrk");
            }
            if (out.movementName.empty()) {
                out.movementName = mp4Text(tag, "\xC2\xA9mvn");
            }
            if (out.movementNo == 0) {
                out.movementNo = mp4Int(tag, "\xC2\xA9mvi");
            }
            if (out.movementTotal == 0) {
                out.movementTotal = mp4Int(tag, "\xC2\xA9mvc");
            }
        }

        if (out.title.empty()) {
            out.title = pathToUtf8(file.stem());
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "unknown error while reading tags";
    }
    return false;
}

}  // namespace mp::core::library
