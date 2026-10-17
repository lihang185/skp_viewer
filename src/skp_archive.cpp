#include "skp_archive.hpp"
#include <stdexcept>
#include <set>

namespace skp {
namespace {
class Reader {
    const std::vector<std::uint8_t> &b;

  public:
    std::size_t pos = 0;
    explicit Reader(const std::vector<std::uint8_t> &input) : b(input) {}
    void need(std::size_t n) const {
        if (pos > b.size() || n > b.size() - pos)
            throw std::runtime_error("Truncated SKP archive");
    }
    std::uint8_t u8() {
        need(1);
        return b[pos++];
    }
    std::uint16_t u16() {
        auto a = u8();
        auto c = u8();
        return a | (std::uint16_t(c) << 8);
    }
    std::uint32_t u32() {
        auto a = u16();
        auto c = u16();
        return a | (std::uint32_t(c) << 16);
    }
    void skip(std::size_t n) {
        need(n);
        pos += n;
    }
    std::string ascii(std::size_t n) {
        need(n);
        std::string s;
        while (n--) {
            auto c = u8();
            if (c < 32 || c > 126)
                throw std::runtime_error("Unsupported archive string");
            s += char(c);
        }
        return s;
    }
    std::string short_unicode_ascii() {
        if (u8() != 0xff || u16() != 0xfffe)
            throw std::runtime_error("Unsupported archive string encoding");
        auto n = u8();
        if (n == 0xff)
            throw std::runtime_error("Long archive string not implemented");
        need(std::size_t(n) * 2);
        std::string s;
        while (n--) {
            auto c = u16();
            if (c < 32 || c > 126)
                throw std::runtime_error("Non-ASCII metadata not implemented");
            s += char(c);
        }
        return s;
    }
};
} // namespace
ArchiveHeader read_legacy_header(const std::vector<std::uint8_t> &bytes) {
    Reader r(bytes);
    if (r.short_unicode_ascii() != "SketchUp Model")
        throw std::runtime_error("Not a SketchUp model archive");
    ArchiveHeader h;
    h.version = r.short_unicode_ascii();
    if (h.version != "{15.0.1}" && h.version != "{15.0.9351}" && h.version != "{15.3.331}" &&
        h.version != "{17.0.18899}")
        throw std::runtime_error("Unverified archive version: " + h.version);
    r.skip(16); // Observed opaque 16-byte field; meaning not established.
    if (!r.short_unicode_ascii().empty())
        throw std::runtime_error("Unverified nonempty header field");
    r.skip(4); // Observed opaque 32-bit field.
    if (r.u16() != 0xffff || r.u16() != 0)
        throw std::runtime_error("Unverified class declaration");
    auto n = r.u16();
    if (r.ascii(n) != "CVersionMap")
        throw std::runtime_error("Missing version map");
    std::set<std::string> names;
    for (std::size_t i = 0; i < 1024; ++i) {
        auto name = r.short_unicode_ascii();
        auto version = r.u32();
        if (name == "End-Of-Version-Map") {
            if (version != 0)
                throw std::runtime_error("Invalid version-map terminator");
            h.payload_offset = r.pos;
            return h;
        }
        if (name.empty() || !names.insert(name).second)
            throw std::runtime_error("Invalid class version entry");
        h.classes.push_back(ClassVersion{name, version});
    }
    throw std::runtime_error("Version map exceeds supported bounds");
}
} // namespace skp
