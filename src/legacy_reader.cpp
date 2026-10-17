#include "legacy_reader.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace legacy {
namespace {
void utf8(std::string &s, uint32_t c) {
    if (c < 128)
        s.push_back(static_cast<char>(c));
    else if (c < 2048) {
        s.push_back(static_cast<char>(192 | (c >> 6)));
        s.push_back(static_cast<char>(128 | (c & 63)));
    } else if (c < 65536) {
        s.push_back(static_cast<char>(224 | (c >> 12)));
        s.push_back(static_cast<char>(128 | ((c >> 6) & 63)));
        s.push_back(static_cast<char>(128 | (c & 63)));
    } else {
        s.push_back(static_cast<char>(240 | (c >> 18)));
        s.push_back(static_cast<char>(128 | ((c >> 12) & 63)));
        s.push_back(static_cast<char>(128 | ((c >> 6) & 63)));
        s.push_back(static_cast<char>(128 | (c & 63)));
    }
}
void expect_class(Node *node, const char *name) {
    if (node && node->class_name != name)
        throw std::runtime_error(std::string("Invalid archive reference, expected ") + name);
}
void scalars(Reader &a, Node &n, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        n.values.push_back(a.f64());
}
void entity_base(Reader &a, Node &n) {
    uint32_t v = a.version("CEntity");
    if (v == 1)
        n.numbers["legacy_entity_field"] = a.u32();
    else if (v >= 3)
        a.entity(n);
}
void attribute_base(Reader &a, Node &n) {
    if (a.version("CAttribute") != 0)
        throw std::runtime_error("Unsupported attribute schema");
    entity_base(a, n);
    n.numbers["attribute_field"] = a.u32();
}
void bounded_version(Reader &a, const char *name, uint32_t max) {
    if (a.version(name) > max)
        throw std::runtime_error(std::string("Unsupported class version: ") + name);
}
} // namespace
Reader::Reader(std::vector<uint8_t> bytes) : data(std::move(bytes)) {
    table.push_back(Entry{false, "", nullptr});
    register_metadata();
    register_suffix();
}
// Conservative cumulative allocation accounting bounds expansion of compact
// object-reference streams and tiny typed values, independently of file size.
void Reader::account(size_t bytes) {
    // Large furniture models contain hundreds of thousands of topology nodes.
    // Scale the estimate with input size, while retaining a hard 1 GiB bound.
    const size_t limit =
        (std::min)(size_t(1024u * 1024u * 1024u),
                   (std::max)(size_t(256u * 1024u * 1024u),
                              (std::min)(data.size(), size_t(16u * 1024u * 1024u)) * 64));
    if (bytes > limit - allocation_budget)
        throw std::runtime_error("Archive allocation budget exceeded");
    allocation_budget += bytes;
}
void Reader::require(size_t n) const {
    if (pos > data.size() || n > data.size() - pos)
        throw std::runtime_error("Truncated archive at " + std::to_string(pos));
}
void Reader::skip(size_t n) {
    require(n);
    pos += n;
}
std::vector<uint8_t> Reader::take(size_t n) {
    require(n);
    std::vector<uint8_t> out(data.begin() + pos, data.begin() + pos + n);
    pos += n;
    return out;
}
uint8_t Reader::u8() {
    require(1);
    return data[pos++];
}
uint16_t Reader::u16() {
    uint16_t v = u8();
    v |= uint16_t(u8()) << 8;
    return v;
}
uint32_t Reader::u32() {
    uint32_t v = u16();
    v |= uint32_t(u16()) << 16;
    return v;
}
double Reader::f64() {
    uint64_t v = u32();
    v |= uint64_t(u32()) << 32;
    double d;
    std::memcpy(&d, &v, 8);
    return d;
}
uint32_t Reader::version(const std::string &name) const {
    auto it = versions.find(name);
    if (it == versions.end())
        throw std::runtime_error("Missing class version: " + name);
    return it->second;
}
uint32_t Reader::count(uint32_t limit) {
    uint32_t n = u32();
    if (n > limit || n > data.size() - pos)
        throw std::runtime_error("Archive count exceeds bound at " + std::to_string(pos));
    return n;
}
std::string Reader::string() {
    bool wide = false;
    uint32_t n = u8();
    if (n == 255) {
        n = u16();
        if (n == 65534) {
            wide = true;
            n = u8();
            if (n == 255)
                n = u16();
        }
        if (n == 65535)
            n = u32();
    }
    if (n > 10000000)
        throw std::runtime_error("Archive string too long");
    account(size_t(n) * 3 + 32);
    require(size_t(n) * (wide ? 2 : 1));
    std::string out;
    if (!wide) {
        out.assign(reinterpret_cast<const char *>(data.data() + pos), n);
        pos += n;
        return out;
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t c = u16();
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i >= n)
                throw std::runtime_error("Incomplete UTF16 surrogate");
            uint32_t lo = u16();
            if (lo < 0xdc00 || lo > 0xdfff)
                throw std::runtime_error("Invalid UTF16 surrogate");
            c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
        } else if (c >= 0xdc00 && c <= 0xdfff)
            throw std::runtime_error("Invalid UTF16 surrogate");
        utf8(out, c);
    }
    return out;
}
Node *Reader::make_node(const std::string &name) {
    account(1024 + name.size());
    if (nodes.size() >= 1000000)
        throw std::runtime_error("Archive node limit");
    nodes.emplace_back(new Node);
    nodes.back()->class_name = name;
    return nodes.back().get();
}
Node *Reader::obj() {
    account(32);
    if (depth >= 256)
        throw std::runtime_error("Archive nesting limit");
    ++depth;
    struct Guard {
        unsigned &v;
        ~Guard() {
            --v;
        }
    } guard{depth};
    size_t start = pos;
    uint16_t word = u16();
    uint32_t tag =
        word == 0x7fff ? u32() : (uint32_t(word & 0x7fff) | (uint32_t(word & 0x8000) << 16));
    if (!(tag & 0x80000000)) {
        if (tag >= table.size() || table[tag].is_class)
            throw std::runtime_error("Invalid object reference");
        return table[tag].object;
    }
    std::string name;
    if (word == 0xffff) {
        uint16_t schema = u16();
        uint16_t size = u16();
        if (!size || size > 256)
            throw std::runtime_error("Invalid archive class name length");
        account(size * 2 + 128);
        auto b = take(size);
        name.assign(b.begin(), b.end());
        if ((name == "CVersionMap" && schema != 0) ||
            (name != "CVersionMap" && schema != version(name)))
            throw std::runtime_error("Class descriptor/version-map mismatch: " + name);
        table.push_back(Entry{true, name, nullptr});
    } else {
        uint32_t i = tag & 0x7fffffff;
        if (i >= table.size() || !table[i].is_class)
            throw std::runtime_error("Invalid class reference");
        name = table[i].name;
    }
    Node *node = make_node(name);
    node->numbers["id"] = double(table.size());
    node->numbers["offset"] = double(start);
    table.push_back(Entry{false, "", node});
    auto it = handlers.find(name);
    if (it == handlers.end())
        throw std::runtime_error("Unsupported archive class " + name + " at " +
                                 std::to_string(start));
    it->second(*this, *node);
    node->numbers["end"] = double(pos);
    return node;
}
void Reader::entity(Node &n) {
    const auto entity_version = version("CEntity");
    if (entity_version != 3 && entity_version != 5)
        throw std::runtime_error("Unsupported entity schema");
    Node *attributes = obj();
    if (attributes && attributes->class_name != "CAttributeContainer")
        throw std::runtime_error("Invalid attribute-container reference");
    n.refs["attributes"] = attributes;
    if (entity_version == 5) {
        // SKP 2017 stores a persistent ID as a nonzero-byte mask followed
        // by the selected bytes, least significant byte first.
        uint8_t mask = u8();
        uint64_t id = 0;
        for (unsigned i = 0; i < 8; ++i)
            if (mask & (1u << i))
                id |= uint64_t(u8()) << (8 * i);
        n.texts["persistent_id"] = std::to_string(id);
    }
}
void Reader::drawing(Node &n) {
    if (version("CDrawingElement") != 9)
        throw std::runtime_error("Unsupported drawing-element schema");
    entity(n);
    n.refs["material"] = obj();
    if (n.refs["material"] && n.refs["material"]->class_name != "CMaterial")
        throw std::runtime_error("Invalid drawing material reference");
    for (int i = 0; i < 6; ++i)
        n.numbers["flag" + std::to_string(i)] = u8();
    n.refs["layer"] = obj();
    if (n.refs["layer"] && n.refs["layer"]->class_name != "CLayer")
        throw std::runtime_error("Invalid drawing layer reference");
}
Node *Reader::typed_value(unsigned nesting) {
    if (nesting >= 128)
        throw std::runtime_error("Typed value nesting limit");
    Node *n = make_node("TypedValue");
    uint8_t tag = u8();
    n->numbers["tag"] = tag;
    switch (tag) {
    case 0:
    case 1:
        break;
    case 2:
        n->numbers["value"] = u8();
        break;
    case 3:
        n->numbers["value"] = static_cast<int16_t>(u16());
        break;
    case 4:
    case 9:
        n->numbers["value"] = static_cast<int32_t>(u32());
        break;
    case 5: {
        uint32_t bits = u32();
        float f;
        std::memcpy(&f, &bits, 4);
        n->numbers["value"] = f;
        break;
    }
    case 6:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
        n->numbers["value"] = f64();
        break;
    case 7:
        n->numbers["value"] = u8() != 0;
        break;
    case 8:
        for (int i = 0; i < 4; ++i)
            n->values.push_back(u8());
        break;
    case 10:
        n->texts["value"] = string();
        break;
    case 11: {
        int32_t len = static_cast<int32_t>(u32());
        if (len > 1000000 || (len > 0 && size_t(len) > data.size() - pos))
            throw std::runtime_error("Typed value count limit");
        for (int32_t i = 0; i < len; ++i)
            n->lists["items"].push_back(typed_value(nesting + 1));
        break;
    }
    case 17:
    case 18:
    case 19:
        scalars(*this, *n, 3);
        break;
    case 20:
        scalars(*this, *n, 13);
        break;
    default:
        throw std::runtime_error("Unsupported typed value tag " + std::to_string(tag));
    }
    return n;
}
void Reader::base_header() {
    if (string() != "SketchUp Model")
        throw std::runtime_error("Not a SketchUp archive");
    std::string file_version = string();
    if (file_version != "{15.0.1}" && file_version != "{15.0.9351}" &&
        file_version != "{15.3.331}" && file_version != "{17.0.18899}")
        throw std::runtime_error("Unsupported SKP archive version");
    skip(16);
    string();
    u32();
    Node *map = obj();
    if (!map || map->class_name != "CVersionMap")
        throw std::runtime_error("Missing version map");
}
void Reader::register_metadata() {
    handlers["CVersionMap"] = [](Reader &a, Node &) {
        if (a.version_map_seen)
            throw std::runtime_error("Duplicate archive version map");
        a.version_map_seen = true;
        for (;;) {
            std::string name = a.string();
            uint32_t v = a.u32();
            if (name == "End-Of-Version-Map")
                break;
            if (name.empty() || a.versions.count(name))
                throw std::runtime_error("Invalid version map");
            a.account(128);
            a.versions[name] = v;
        }
    };
    handlers["CAttribute"] = attribute_base;
    // CFaceTextureCoords::Serialize (0x1026B5D0): attribute base,
    // front/back 3x3 UV matrices and projection vectors, pins, flags.
    handlers["CFaceTextureCoords"] = [](Reader &a, Node &n) {
        bounded_version(a, "CFaceTextureCoords", 4);
        attribute_base(a, n);
        const auto v = a.version("CFaceTextureCoords");
        scalars(a, n, 9);
        if (v >= 1)
            scalars(a, n, 3);
        if (v >= 2)
            scalars(a, n, 12);
        if (v >= 3) {
            for (const char *side : {"front_pin_count", "back_pin_count"}) {
                uint32_t count = a.count();
                n.numbers[side] = count;
                scalars(a, n, count * 4);
            }
        }
        if (v >= 4) {
            n.numbers["front_flags"] = a.u32();
            n.numbers["back_flags"] = a.u32();
        }
    };
    handlers["CAttributeNamed"] = [](Reader &a, Node &n) {
        bounded_version(a, "CAttributeNamed", 1);
        attribute_base(a, n);
        n.texts["name"] = a.string();
        for (;;) {
            std::string key = a.string();
            if (key.empty())
                break;
            n.refs[key] = a.typed_value();
        }
        if (a.version("CAttributeNamed") >= 1)
            n.numbers["attribute_tail"] = a.u32();
    };
    handlers["CAttributeContainer"] = [](Reader &a, Node &n) {
        if (a.version("CAttributeContainer") != 0)
            throw std::runtime_error("Unsupported attribute-container schema");
        entity_base(a, n);
        while (Node *item = a.obj()) {
            if (item->class_name != "CAttributeNamed" && item->class_name != "CAttribute" &&
                item->class_name != "CAttributeContainer" &&
                item->class_name != "CFaceTextureCoords")
                throw std::runtime_error("Invalid attribute item reference");
            n.lists["items"].push_back(item);
        }
    };
    handlers["CDib"] = [](Reader &a, Node &n) {
        bounded_version(a, "CDib", 3);
        uint32_t v = a.version("CDib");
        if (!v)
            return;
        uint32_t fmt = v == 1 ? 4 : a.u32();
        n.numbers["format"] = fmt;
        uint32_t len = a.u32();
        n.numbers["data_offset"] = double(a.pos);
        n.numbers["data_size"] = len;
        a.skip(len);
        if (v >= 3 && fmt == 1)
            n.numbers["extra"] = a.u32();
    };
    handlers["CCamera"] = [](Reader &a, Node &n) {
        bounded_version(a, "CCamera", 5);
        uint32_t v = a.version("CCamera");
        scalars(a, n, 9);
        n.numbers["aspect"] = a.f64();
        n.numbers["height"] = a.f64();
        n.numbers["perspective"] = a.u8();
        n.numbers["fov"] = a.f64();
        n.numbers["unknown1"] = a.f64();
        scalars(a, n, 3);
        n.numbers["unknown2"] = a.f64();
        n.numbers["flag1"] = a.u8();
        if (v > 1)
            n.numbers["flag2"] = a.u8();
        if (v > 2)
            n.texts["description"] = a.string();
        if (v > 3)
            n.numbers["unknown3"] = a.f64();
        if (v > 4) {
            n.numbers["flag3"] = a.u8();
            scalars(a, n, 3);
        }
    };
    handlers["CTexture"] = [](Reader &a, Node &n) {
        bounded_version(a, "CTexture", 6);
        uint32_t v = a.version("CTexture");
        if (v >= 5)
            a.entity(n);
        if (v >= 6 || v <= 1)
            n.refs["dib"] = a.obj();
        else if (a.u8()) {
            Node *dib = a.make_node("CDib");
            a.handlers.at("CDib")(a, *dib);
            n.refs["dib"] = dib;
        } else
            n.refs["dib"] = nullptr;
        n.numbers["width"] = a.f64();
        n.numbers["height"] = a.f64();
        n.texts["filename"] = a.string();
        if (v)
            n.numbers["flags"] = a.u32();
        if (v == 3) {
            n.values.push_back(a.u32());
            n.values.push_back(a.u32());
        }
    };
    handlers["CMaterial"] = [](Reader &a, Node &n) {
        bounded_version(a, "CMaterial", 12);
        a.entity(n);
        uint32_t v = a.version("CMaterial");
        n.texts["name"] = a.string();
        if (v < 3 && a.u8())
            a.u32();
        if (a.u8()) {
            Node *texture = a.make_node("CTexture");
            a.handlers.at("CTexture")(a, *texture);
            n.refs["texture"] = texture;
        }
        if (v >= 2 && v <= 4)
            a.u32();
        if (v >= 6)
            n.numbers["flag"] = a.u8();
        if (v >= 7)
            for (int i = 0; i < 4; ++i)
                n.values.push_back(a.u8());
        if (v >= 8)
            n.texts["description"] = a.string();
        if (v >= 9)
            n.numbers["type"] = a.u32();
        if (v >= 10)
            n.numbers["unknown"] = a.u32();
        if (v >= 11)
            n.numbers["opacity"] = a.f64();
        if (v >= 12)
            n.numbers["use_opacity"] = a.u8();
    };
    handlers["CLayer"] = [](Reader &a, Node &n) {
        bounded_version(a, "CLayer", 2);
        a.entity(n);
        uint32_t v = a.version("CLayer");
        n.texts["name"] = a.string();
        n.numbers["hidden"] = a.u8();
        if (v) {
            Node *m = a.make_node("CMaterial");
            a.handlers.at("CMaterial")(a, *m);
            n.refs["material"] = m;
        } else {
            for (int i = 0; i < 4; ++i)
                n.values.push_back(a.u8());
            n.refs["material"] = a.obj();
        }
        if (v >= 2)
            n.numbers["flags"] = a.u32();
    };
    handlers["CThumbnail"] = [](Reader &a, Node &n) {
        if (a.version("CThumbnail") != 1)
            throw std::runtime_error("Unsupported thumbnail schema");
        a.entity(n);
        n.refs["camera"] = a.obj();
        n.refs["dib"] = a.obj();
    };
    handlers["CMaterialManager"] = [](Reader &a, Node &n) {
        bounded_version(a, "CMaterialManager", 4);
        uint32_t v = a.version("CMaterialManager");
        if (v)
            a.entity(n);
        uint32_t count = a.count();
        for (uint32_t i = 0; i < count; ++i)
            n.lists["materials"].push_back(a.obj());
        if (v > 1)
            n.refs["current"] = a.obj();
        if (v == 3)
            n.refs["extra"] = a.obj();
    };
    handlers["CLayerManager"] = [](Reader &a, Node &n) {
        bounded_version(a, "CLayerManager", 4);
        uint32_t v = a.version("CLayerManager");
        if (v)
            a.entity(n);
        uint32_t count = a.count();
        for (uint32_t i = 0; i < count; ++i)
            n.lists["layers"].push_back(a.obj());
        if (v >= 3) {
            if (v == 3 || count)
                n.refs["current"] = a.obj();
        } else
            n.texts["current_name"] = a.string();
    };
    handlers["CComponentBehavior"] = [](Reader &a, Node &n) {
        bounded_version(a, "CComponentBehavior", 5);
        a.entity(n);
        uint32_t v = a.version("CComponentBehavior");
        if (v >= 3)
            n.numbers["flag1"] = a.u8();
        n.numbers["flag2"] = a.u8();
        if (v >= 2)
            n.numbers["snap"] = a.u32();
        if (v >= 4)
            n.numbers["flags"] = a.u8();
        if (v >= 5)
            n.numbers["unknown"] = a.u32();
    };
    handlers["COptionsManager"] = [](Reader &a, Node &n) {
        n.numbers["schema"] = a.u32();
        uint32_t count = a.count();
        for (uint32_t i = 0; i < count; ++i) {
            std::string name = a.string();
            Node *provider = a.make_node("OptionsProvider");
            n.refs[name] = provider;
            for (;;) {
                std::string key = a.string();
                if (key.empty())
                    break;
                provider->refs[key] = a.typed_value();
            }
        }
    };
    handlers["CRenderingOptions"] = [](Reader &a, Node &n) {
        a.entity(n);
        if (a.version("CRenderingOptions") != 36)
            throw std::runtime_error("Unsupported rendering options version");
        const char *format = "IBBBIIIIIBBBBIBIBIBIBIBBIIIddBBBIBddIIBBBBIIIBBBIIIIIIIBdBIBdBBdBd";
        for (const char *p = format; *p; ++p) {
            if (*p == 'I')
                n.values.push_back(a.u32());
            else if (*p == 'B')
                n.values.push_back(a.u8());
            else if (*p == 'd')
                n.values.push_back(a.f64());
        }
    };
}
Node *Reader::root_prefix() {
    base_header();
    if (version("CSketchUpModel") != 24 && version("CSketchUpModel") != 26)
        throw std::runtime_error("Unsupported model schema (expected 24)");
    Node *root = make_node("CSketchUpModel");
    root->values.push_back(u32());
    root->values.push_back(u32());
    root->numbers["unknown18"] = u32();
    if (version("CSketchUpModel") == 26) {
        uint64_t id = u32();
        id |= uint64_t(u32()) << 32;
        root->texts["unknown_model26_field"] = std::to_string(id);
    }
    root->refs["dib"] = obj();
    expect_class(root->refs["dib"], "CDib");
    root->numbers["dib_flag"] = u8();
    for (const char *kind : {"CComponentBehavior", "COptionsManager"}) {
        if (std::string(kind) == "COptionsManager")
            root->texts["description"] = string();
        Node *n = make_node(kind);
        handlers.at(kind)(*this, *n);
        root->refs[std::string(kind) == "COptionsManager" ? "options" : "behavior"] = n;
    }
    root->refs["attributes"] = obj();
    root->refs["dib2"] = obj();
    root->refs["camera"] = obj();
    expect_class(root->refs["attributes"], "CAttributeContainer");
    expect_class(root->refs["dib2"], "CDib");
    expect_class(root->refs["camera"], "CCamera");
    Node *rendering = make_node("CRenderingOptions");
    handlers.at("CRenderingOptions")(*this, *rendering);
    root->refs["rendering"] = rendering;
    uint32_t n = count();
    for (uint32_t i = 0; i < n; ++i) {
        Node *relationship = obj();
        expect_class(relationship, "CRelationship");
        root->lists["relationships"].push_back(relationship);
    }
    root->numbers["unknown17"] = u32();
    return root;
}
// Root suffix layout is recovered from sub_101D4160. The handlers below use
// exact class revisions from the supported SketchUp 2015 version map.
void Reader::register_suffix() {
    handlers["CShadowInfo"] = [](Reader &a, Node &n) {
        if (a.version("CShadowInfo") != 7)
            throw std::runtime_error("Unsupported shadow schema");
        a.entity(n);
        n.numbers["time"] = static_cast<int32_t>(a.u32());
        n.numbers["enabled"] = a.u8();
        n.texts["location"] = a.string();
        n.texts["country"] = a.string();
        scalars(a, n, 6);
        for (int i = 0; i < 4; ++i)
            n.numbers["flag" + std::to_string(i)] = a.u8();
        n.numbers["light"] = a.u32();
        n.numbers["dark"] = a.u32();
        n.numbers["flag4"] = a.u8();
    };
    handlers["CPageList"] = [](Reader &a, Node &n) {
        if (a.version("CPageList") != 1)
            throw std::runtime_error("Unsupported page-list schema");
        a.entity(n);
        uint32_t count = a.count();
        for (uint32_t i = 0; i < count; ++i)
            n.lists["pages"].push_back(a.obj());
        n.refs["current"] = a.obj();
    };
    handlers["CSketchCS"] = [](Reader &a, Node &n) {
        if (a.version("CSketchCS") != 0)
            throw std::runtime_error("Unsupported axes schema");
        a.drawing(n);
        scalars(a, n, 12);
    };
    handlers["CSkFont"] = [](Reader &a, Node &n) {
        if (a.version("CSkFont") != 1)
            throw std::runtime_error("Unsupported font schema");
        a.entity(n);
        n.texts["name"] = a.string();
        n.numbers["bold"] = a.u8();
        n.numbers["italic"] = a.u8();
        n.numbers["size"] = a.u32();
        n.numbers["flag"] = a.u8();
        n.numbers["scale"] = a.f64();
    };
    handlers["CDimensionStyle"] = [](Reader &a, Node &n) {
        if (a.version("CDimensionStyle") != 4)
            throw std::runtime_error("Unsupported dimension-style schema");
        a.entity(n);
        n.numbers["legacy"] = a.u32();
        n.refs["font"] = a.obj();
        n.numbers["flag0"] = a.u8();
        n.numbers["flag1"] = a.u8();
        for (int i = 0; i < 5; ++i)
            n.values.push_back(a.u32());
        n.numbers["flag2"] = a.u8();
        n.numbers["color0"] = a.u32();
        n.numbers["flag3"] = a.u8();
        n.numbers["flag4"] = a.u8();
        n.values.push_back(a.f64());
        n.numbers["flag5"] = a.u8();
        n.values.push_back(a.f64());
        for (int i = 0; i < 3; ++i)
            n.values.push_back(a.u32());
    };
    handlers["CTextStyle"] = [](Reader &a, Node &n) {
        if (a.version("CTextStyle") != 5)
            throw std::runtime_error("Unsupported text-style schema");
        a.entity(n);
        n.refs["font"] = a.obj();
        n.values.push_back(a.u32());
        n.values.push_back(a.u32());
        n.numbers["flag0"] = a.u8();
        n.values.push_back(a.u32());
        n.numbers["flag1"] = a.u8();
        n.numbers["color0"] = a.u32();
        n.numbers["color1"] = a.u32();
        n.refs["font2"] = a.obj();
    };
    handlers["CFontManager"] = [](Reader &a, Node &n) {
        if (a.version("CFontManager") != 0)
            throw std::runtime_error("Unsupported font-manager schema");
        a.entity(n);
        uint32_t count = a.count();
        for (uint32_t i = 0; i < count; ++i)
            n.lists["fonts"].push_back(a.obj());
    };
    handlers["CSkpStyleManager"] = [](Reader &a, Node &n) {
        if (a.version("CSkpStyleManager") != 2)
            throw std::runtime_error("Unsupported style-manager schema");
        a.entity(n);
        uint32_t count = a.count();
        for (uint32_t i = 0; i < count; ++i)
            n.lists["styles"].push_back(a.obj());
        if (count) {
            n.refs["current"] = a.obj();
            n.refs["other"] = a.obj();
            n.numbers["flag"] = a.u8();
        }
    };
    handlers["CWatermarkManager"] = [](Reader &a, Node &n) {
        if (a.version("CWatermarkManager") != 2)
            throw std::runtime_error("Unsupported watermark-manager schema");
        a.entity(n);
        uint32_t count = a.count();
        for (uint32_t i = 0; i < count; ++i)
            n.lists["items"].push_back(a.obj());
    };
    handlers["CSkpStyle"] = [](Reader &a, Node &n) {
        if (a.version("CSkpStyle") != 1)
            throw std::runtime_error("Unsupported style schema");
        a.entity(n);
        auto guid = a.take(16);
        n.texts["guid"].assign(guid.begin(), guid.end());
        n.texts["id"] = a.string();
        uint32_t v = a.u32();
        if (v != 3)
            throw std::runtime_error("Unsupported style-container schema");
        n.texts["name"] = a.string();
        n.texts["description"] = a.string();
        uint32_t count = a.count();
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t key = a.u32();
            Node *value = a.make_node("StyleValue");
            n.refs[std::to_string(key)] = value;
            if (key == 1013) {
                uint32_t pv = a.u32();
                if (pv != 2)
                    throw std::runtime_error("Unsupported style-pattern schema");
                scalars(a, *value, 2);
                value->numbers["field0"] = a.u32();
                value->numbers["flag"] = a.u8();
                value->numbers["field1"] = a.u32();
                value->numbers["field2"] = a.u32();
                uint32_t sizes = a.count();
                for (uint32_t j = 0; j < sizes; ++j)
                    value->values.push_back(a.u32());
                uint32_t images = a.count();
                for (uint32_t j = 0; j < images; ++j)
                    value->lists["images"].push_back(a.obj());
            } else if (key == 5001) {
                uint32_t items = a.count();
                for (uint32_t j = 0; j < items; ++j)
                    value->lists["items"].push_back(a.obj());
            } else {
                uint32_t schema = a.u32();
                if (schema != 1)
                    throw std::runtime_error("Unsupported style-value schema");
                uint32_t tag = a.u32();
                value->numbers["tag"] = tag;
                switch (tag) {
                case 0:
                case 8:
                case 13:
                    break;
                case 1:
                case 2:
                    value->numbers["value"] = a.u8();
                    break;
                case 3:
                    value->numbers["value"] = static_cast<int16_t>(a.u16());
                    break;
                case 4:
                case 5:
                case 14:
                    value->numbers["value"] = static_cast<int32_t>(a.u32());
                    break;
                case 6: {
                    uint32_t bits = a.u32();
                    float f;
                    std::memcpy(&f, &bits, 4);
                    value->numbers["value"] = f;
                    break;
                }
                case 7:
                    value->numbers["value"] = a.f64();
                    break;
                case 9:
                case 10:
                case 11:
                    scalars(a, *value, 3);
                    break;
                case 12:
                    scalars(a, *value, 13);
                    break;
                case 15:
                    value->numbers["low"] = a.u32();
                    value->numbers["high"] = a.u32();
                    break;
                default:
                    throw std::runtime_error("Unsupported style-value tag");
                }
            }
        }
    };
    handlers["CSchemaZipFile"] = [](Reader &a, Node &n) {
        if (a.version("CSchemaZipFile") != 1)
            throw std::runtime_error("Unsupported schema-zip revision");
        n.texts["path"] = a.string();
        n.texts["name"] = a.string();
        uint32_t length = a.u32();
        n.numbers["data_offset"] = double(a.pos);
        n.numbers["data_size"] = length;
        a.skip(length);
    };
}
void Reader::root_suffix(Node &root) {
    if (version("CSketchUpModel") != 24 && version("CSketchUpModel") != 26)
        throw std::runtime_error("Unsupported root suffix schema");
    auto read_inline = [this, &root](const char *cls, const char *key) {
        Node *n = make_node(cls);
        n->numbers["offset"] = double(pos);
        handlers.at(cls)(*this, *n);
        n->numbers["end"] = double(pos);
        root.refs[key] = n;
    };
    read_inline("CShadowInfo", "shadow");
    read_inline("CPageList", "pages");
    read_inline("CSketchCS", "axes");
    for (int i = 0; i < 16; ++i)
        root.values.push_back(u32());
    read_inline("CDimensionStyle", "dimensions");
    read_inline("CTextStyle", "text");
    read_inline("CFontManager", "fonts");
    root.refs["background"] = obj();
    expect_class(root.refs["background"], "CBackgroundImage");
    read_inline("CSkpStyleManager", "styles");
    read_inline("CWatermarkManager", "watermarks");
    root.numbers["tail"] = u32();
    root.numbers["tail_flag"] = u8();
    uint32_t extras = count();
    for (uint32_t i = 0; i < extras; ++i) {
        Node *extra = obj();
        expect_class(extra, "CSchemaZipFile");
        root.lists["extras"].push_back(extra);
    }
    if (pos != data.size())
        throw std::runtime_error("Trailing data after model archive at " + std::to_string(pos));
}

} // namespace legacy
