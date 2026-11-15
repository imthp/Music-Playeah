#include "core/text.hpp"

#include <cstdint>
#include <vector>

namespace mp::core {

namespace {

bool isSpace(std::uint32_t cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0; }

std::uint32_t toLower(std::uint32_t cp) {
    if (cp < 0x80) {
        return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
    }
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) {  // Latin-1: À..Þ except ×
        return cp + 32;
    }
    if (cp >= 0x100 && cp <= 0x17F) {  // Latin Extended-A: mostly upper/lower pairs
        if (cp <= 0x137) {
            return (cp % 2 == 0 && cp != 0x130) ? cp + 1 : cp;
        }
        if (cp >= 0x139 && cp <= 0x148) {
            return cp % 2 == 1 ? cp + 1 : cp;
        }
        if (cp >= 0x14A && cp <= 0x177) {
            return cp % 2 == 0 ? cp + 1 : cp;
        }
        if (cp == 0x178) {
            return 0xFF;
        }
        if (cp >= 0x179 && cp <= 0x17E) {
            return cp % 2 == 1 ? cp + 1 : cp;
        }
        return cp;
    }
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) {  // Greek capitals
        return cp + 32;
    }
    if (cp >= 0x410 && cp <= 0x42F) {  // Cyrillic А..Я
        return cp + 32;
    }
    if (cp >= 0x400 && cp <= 0x40F) {  // Cyrillic Ѐ..Џ
        return cp + 80;
    }
    return cp;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Decodes one code point at `i` and advances it. Invalid bytes come out unchanged as `raw`
// (so garbage tags still compare equal to themselves instead of being mangled).
struct Decoded {
    std::uint32_t cp;
    bool valid;
};

Decoded decodeOne(std::string_view s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    auto cont = [&](std::size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
    auto cb = [&](std::size_t k) { return static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + k]) & 0x3F); };
    if (b0 < 0x80) {
        ++i;
        return {b0, true};
    }
    if ((b0 & 0xE0) == 0xC0 && cont(1)) {
        const std::uint32_t cp = ((b0 & 0x1Fu) << 6) | cb(1);
        i += 2;
        return {cp, cp >= 0x80};
    }
    if ((b0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        const std::uint32_t cp = ((b0 & 0x0Fu) << 12) | (cb(1) << 6) | cb(2);
        i += 3;
        return {cp, cp >= 0x800};
    }
    if ((b0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        const std::uint32_t cp = ((b0 & 0x07u) << 18) | (cb(1) << 12) | (cb(2) << 6) | cb(3);
        i += 4;
        return {cp, cp >= 0x10000 && cp <= 0x10FFFF};
    }
    ++i;
    return {b0, false};
}

}  // namespace

std::string trimText(std::string_view s) {
    auto spaceAt = [&](std::size_t i) { return s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'; };
    std::size_t begin = 0;
    std::size_t end = s.size();
    for (;;) {
        if (begin < end && spaceAt(begin)) {
            ++begin;
        } else if (end - begin >= 2 && static_cast<unsigned char>(s[begin]) == 0xC2 &&
                   static_cast<unsigned char>(s[begin + 1]) == 0xA0) {
            begin += 2;
        } else {
            break;
        }
    }
    for (;;) {
        if (end > begin && spaceAt(end - 1)) {
            --end;
        } else if (end - begin >= 2 && static_cast<unsigned char>(s[end - 2]) == 0xC2 &&
                   static_cast<unsigned char>(s[end - 1]) == 0xA0) {
            end -= 2;
        } else {
            break;
        }
    }
    return std::string(s.substr(begin, end - begin));
}

std::string normalizeText(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool pendingSpace = false;
    std::size_t i = 0;
    while (i < s.size()) {
        const std::size_t start = i;
        const Decoded d = decodeOne(s, i);
        if (d.valid && isSpace(d.cp)) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        if (d.valid) {
            appendUtf8(out, toLower(d.cp));
        } else {
            out.append(s.substr(start, i - start));
        }
    }
    return out;
}

std::string sortName(std::string_view s) {
    std::string n = normalizeText(s);
    if (n.size() > 4 && n.compare(0, 4, "the ") == 0) {
        n.erase(0, 4);
    }
    return n;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string u = path.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::filesystem::path longPathSafe(const std::filesystem::path& path) {
#ifdef _WIN32
    constexpr std::size_t kThreshold = 240;  // MAX_PATH is 260 including the terminator; leave some room
    const std::wstring& text = path.native();
    if (text.size() < kThreshold || text.rfind(L"\\\\?\\", 0) == 0) {
        return path;
    }
    std::filesystem::path absolute = path.is_absolute() ? path : std::filesystem::absolute(path);
    std::wstring full = absolute.lexically_normal().make_preferred().native();  // "..", ".", '/' are not allowed after the prefix
    if (full.rfind(L"\\\\", 0) == 0) {
        return std::filesystem::path(L"\\\\?\\UNC\\" + full.substr(2));  // \\server\share -> \\?\UNC\server\share
    }
    if (full.size() >= 3 && full[1] == L':') {
        return std::filesystem::path(L"\\\\?\\" + full);
    }
    return path;
#else
    return path;
#endif
}

std::string genericUtf8(const std::filesystem::path& path) {
    const std::u8string u = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace mp::core
