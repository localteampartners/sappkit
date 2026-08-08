#include "KitMix.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace sapp::kit {

// ------------------------------------------------------------- serialize ---

namespace {

std::string escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (uint8_t(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

std::string num(double v)
{
    // Compact but round-trip-safe for the ranges we store.
    char buf[32];
    if (v == std::floor(v) && std::fabs(v) < 1e9)
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    else
        std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

} // namespace

std::string serializeKitMix(const KitMix& mix)
{
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": " << KitMix::kVersion << ",\n";
    o << "  \"kit\": \"" << escape(mix.kitPath) << "\",\n";
    o << "  \"name\": \"" << escape(mix.kitName) << "\",\n";
    o << "  \"pads\": [";
    for (size_t i = 0; i < mix.pads.size(); ++i) {
        const auto& p = mix.pads[i];
        o << (i ? ",\n    " : "\n    ");
        o << "{\"note\": " << p.note
          << ", \"name\": \"" << escape(p.name) << "\""
          << ", \"tune\": " << num(p.mix.tuneSemis)
          << ", \"decay\": " << num(p.mix.decay)
          << ", \"pan\": " << num(p.mix.pan)
          << ", \"level\": " << num(p.mix.levelDb) << "}";
    }
    o << (mix.pads.empty() ? "]" : "\n  ]") << ",\n";
    o << "  \"bus\": {";
    for (size_t i = 0; i < mix.bus.size(); ++i) {
        o << (i ? ", " : "");
        o << "\"" << escape(mix.bus[i].first) << "\": " << num(mix.bus[i].second);
    }
    o << "}\n";
    o << "}\n";
    return o.str();
}

// ----------------------------------------------------------------- parse ---
// Minimal recursive-descent JSON, covering what serializeKitMix emits plus
// reasonable hand edits. No exceptions; malformed input fails the parse.

namespace {

struct Cursor {
    const char* p;
    const char* end;
    bool ok = true;

    void skipWs()
    {
        while (p < end && std::isspace(uint8_t(*p))) ++p;
    }
    bool eat(char c)
    {
        skipWs();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }
    bool peek(char c)
    {
        skipWs();
        return p < end && *p == c;
    }
};

bool parseString(Cursor& c, std::string& out)
{
    if (!c.eat('"')) return false;
    out.clear();
    while (c.p < c.end && *c.p != '"') {
        char ch = *c.p++;
        if (ch == '\\' && c.p < c.end) {
            char e = *c.p++;
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (c.end - c.p < 4) return false;
                    unsigned v = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = *c.p++;
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= unsigned(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= unsigned(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= unsigned(h - 'A' + 10);
                        else return false;
                    }
                    if (v < 0x80) out += char(v);
                    else if (v < 0x800) {
                        out += char(0xC0 | (v >> 6));
                        out += char(0x80 | (v & 0x3F));
                    } else {
                        out += char(0xE0 | (v >> 12));
                        out += char(0x80 | ((v >> 6) & 0x3F));
                        out += char(0x80 | (v & 0x3F));
                    }
                    break;
                }
                default: return false;
            }
        } else {
            out += ch;
        }
    }
    return c.p < c.end && *c.p++ == '"';
}

bool parseNumber(Cursor& c, double& out)
{
    c.skipWs();
    char* endp = nullptr;
    out = std::strtod(c.p, &endp);
    if (endp == c.p) return false;
    c.p = endp;
    return true;
}

bool skipValue(Cursor& c);  // forward

bool skipObject(Cursor& c)
{
    if (!c.eat('{')) return false;
    if (c.eat('}')) return true;
    do {
        std::string k;
        if (!parseString(c, k)) return false;
        if (!c.eat(':')) return false;
        if (!skipValue(c)) return false;
    } while (c.eat(','));
    return c.eat('}');
}

bool skipArray(Cursor& c)
{
    if (!c.eat('[')) return false;
    if (c.eat(']')) return true;
    do {
        if (!skipValue(c)) return false;
    } while (c.eat(','));
    return c.eat(']');
}

bool skipValue(Cursor& c)
{
    c.skipWs();
    if (c.peek('{')) return skipObject(c);
    if (c.peek('[')) return skipArray(c);
    if (c.peek('"')) { std::string s; return parseString(c, s); }
    if (c.p + 4 <= c.end && std::string(c.p, 4) == "true") { c.p += 4; return true; }
    if (c.p + 5 <= c.end && std::string(c.p, 5) == "false") { c.p += 5; return true; }
    if (c.p + 4 <= c.end && std::string(c.p, 4) == "null") { c.p += 4; return true; }
    double d;
    return parseNumber(c, d);
}

bool parsePadEntry(Cursor& c, PadMixEntry& pad)
{
    if (!c.eat('{')) return false;
    if (c.eat('}')) return true;
    do {
        std::string key;
        if (!parseString(c, key)) return false;
        if (!c.eat(':')) return false;
        if (key == "note") {
            double d;
            if (!parseNumber(c, d)) return false;
            pad.note = int(d);
        } else if (key == "name") {
            if (!parseString(c, pad.name)) return false;
        } else if (key == "tune") {
            double d;
            if (!parseNumber(c, d)) return false;
            pad.mix.tuneSemis = float(d);
        } else if (key == "decay") {
            double d;
            if (!parseNumber(c, d)) return false;
            pad.mix.decay = float(d);
        } else if (key == "pan") {
            double d;
            if (!parseNumber(c, d)) return false;
            pad.mix.pan = float(d);
        } else if (key == "level") {
            double d;
            if (!parseNumber(c, d)) return false;
            pad.mix.levelDb = float(d);
        } else {
            if (!skipValue(c)) return false;
        }
    } while (c.eat(','));
    return c.eat('}');
}

} // namespace

bool parseKitMix(const std::string& json, KitMix& out)
{
    KitMix mix;
    Cursor c{json.data(), json.data() + json.size()};
    if (!c.eat('{')) return false;
    if (!c.eat('}')) {
        do {
            std::string key;
            if (!parseString(c, key)) return false;
            if (!c.eat(':')) return false;
            if (key == "kit") {
                if (!parseString(c, mix.kitPath)) return false;
            } else if (key == "name") {
                if (!parseString(c, mix.kitName)) return false;
            } else if (key == "pads") {
                if (!c.eat('[')) return false;
                if (!c.eat(']')) {
                    do {
                        PadMixEntry pad;
                        if (!parsePadEntry(c, pad)) return false;
                        if (pad.note >= 0) mix.pads.push_back(std::move(pad));
                    } while (c.eat(','));
                    if (!c.eat(']')) return false;
                }
            } else if (key == "bus") {
                if (!c.eat('{')) return false;
                if (!c.eat('}')) {
                    do {
                        std::string id;
                        double v;
                        if (!parseString(c, id)) return false;
                        if (!c.eat(':')) return false;
                        if (!parseNumber(c, v)) return false;
                        mix.setBus(id, v);
                    } while (c.eat(','));
                    if (!c.eat('}')) return false;
                }
            } else {
                if (!skipValue(c)) return false;
            }
        } while (c.eat(','));
        if (!c.eat('}')) return false;
    }
    out = std::move(mix);
    return true;
}

// ------------------------------------------------------------- utilities ---

std::string kitMixFileName(const std::string& kitPath)
{
    if (kitPath.empty()) return "diagnostic-kit.json";

    // FNV-1a over the full path keeps same-named kits apart.
    uint32_t h = 2166136261u;
    for (char ch : kitPath) {
        h ^= uint8_t(ch);
        h *= 16777619u;
    }

    // Stem = basename without extension, sanitized for the filesystem.
    size_t slash = kitPath.find_last_of("/\\");
    std::string stem = slash == std::string::npos ? kitPath : kitPath.substr(slash + 1);
    size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    for (char& ch : stem)
        if (!(std::isalnum(uint8_t(ch)) || ch == '-' || ch == '_' || ch == ' ')) ch = '_';
    if (stem.size() > 48) stem = stem.substr(0, 48);
    if (stem.empty()) stem = "kit";

    char hex[12];
    std::snprintf(hex, sizeof(hex), "%08x", h);
    return stem + "-" + hex + ".json";
}

int applyMixToOverrides(const KitMix& mix, const KitModel& model, PadOverrides& out)
{
    int applied = 0;
    for (const auto& pad : mix.pads) {
        const int idx = model.padIndexForNote(pad.note);
        if (idx < 0) continue;
        out[size_t(idx)] = pad.mix;
        ++applied;
    }
    return applied;
}

KitMix captureMix(const std::string& kitPath, const std::string& kitName,
                  const KitModel& model, const PadOverrides& overrides)
{
    KitMix mix;
    mix.kitPath = kitPath;
    mix.kitName = kitName;
    for (int i = 0; i < model.padCount; ++i) {
        const auto& ov = overrides[size_t(i)];
        if (ov.isDefault()) continue;
        PadMixEntry pad;
        pad.note = model.pads[size_t(i)].note;
        pad.name = model.pads[size_t(i)].name;
        pad.mix = ov;
        mix.pads.push_back(std::move(pad));
    }
    return mix;
}

} // namespace sapp::kit
