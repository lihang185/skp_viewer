#include "skp_model.hpp"
#include "legacy_reader.hpp"
#include "slapi_compat.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <unordered_map>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace skp {
namespace {
using legacy::Node;
using legacy::Reader;
void schema(Reader &a, const char *name, unsigned expected) {
    if (a.version(name) != expected)
        throw std::runtime_error(std::string("Unsupported geometry schema: ") + name);
}
Node *inline_object(Reader &a, const char *name) {
    Node *n = a.make_node(name);
    a.handlers.at(name)(a, *n);
    return n;
}
void register_geometry(Reader &a) {
    a.handlers["CCurve"] = [](Reader &a, Node &n) {
        schema(a, "CCurve", 4);
        a.entity(n);
        n.numbers["closed"] = a.u8();
        n.numbers["segment_count"] = a.u32();
    };
    a.handlers["CArcCurve"] = [](Reader &a, Node &n) {
        schema(a, "CArcCurve", 3);
        a.handlers.at("CCurve")(a, n);
        // 0x102B4B80: center, normal, first axis, angle range, second axis.
        for (int i = 0; i < 14; ++i)
            n.values.push_back(a.f64());
    };
    a.handlers["CVertex"] = [](Reader &a, Node &n) {
        schema(a, "CVertex", 0);
        a.entity(n);
        for (int i = 0; i < 3; ++i)
            n.values.push_back(a.f64());
    };
    a.handlers["CEdge"] = [](Reader &a, Node &n) {
        schema(a, "CEdge", 2);
        a.drawing(n);
        n.refs["start"] = a.obj();
        n.refs["finish"] = a.obj();
        n.refs["curve"] = a.obj();
    };
    a.handlers["CEdgeUse"] = [](Reader &a, Node &n) {
        schema(a, "CEdgeUse", 1);
        a.entity(n);
        n.refs["edge"] = a.obj();
        n.numbers["reversed"] = a.u8() != 0;
        n.refs["loop"] = a.obj();
    };
    a.handlers["CLoop"] = [](Reader &a, Node &n) {
        schema(a, "CLoop", 1);
        a.entity(n);
        n.numbers["flag0"] = a.u8();
        n.numbers["flag1"] = a.u8();
        while (Node *use = a.obj())
            n.lists["uses"].push_back(use);
    };
    a.handlers["CFace"] = [](Reader &a, Node &n) {
        schema(a, "CFace", 3);
        a.drawing(n);
        for (int i = 0; i < 4; ++i)
            n.values.push_back(a.f64());
        auto count = a.count();
        for (unsigned i = 0; i < count; ++i)
            n.lists["loops"].push_back(a.obj());
        n.refs["back_material"] = a.obj();
    };
    a.handlers["CDefinitionList"] = [](Reader &a, Node &n) {
        schema(a, "CDefinitionList", 0);
        auto count = a.count();
        for (unsigned i = 0; i < count; ++i)
            n.lists["definitions"].push_back(a.obj());
    };
    a.handlers["CRelationshipMap"] = [](Reader &a, Node &) {
        schema(a, "CRelationshipMap", 0);
        if (a.count() != 0)
            throw std::runtime_error("Nonempty relationship maps are not yet supported");
    };
    a.handlers["CComponent"] = [](Reader &a, Node &n) {
        schema(a, "CComponent", 11);
        a.drawing(n);
        n.refs["materials"] = inline_object(a, "CMaterialManager");
        n.refs["layers"] = inline_object(a, "CLayerManager");
        n.refs["definitions"] = inline_object(a, "CDefinitionList");
        auto count = a.count();
        for (unsigned i = 0; i < count; ++i)
            n.lists["entities"].push_back(a.obj());
        n.refs["relationships"] = inline_object(a, "CRelationshipMap");
        n.refs["extra"] = a.obj();
    };
    a.handlers["CComponentDefinition"] = [](Reader &a, Node &n) {
        schema(a, "CComponentDefinition", 10);
        a.handlers.at("CComponent")(a, n);
        a.skip(16);
        n.texts["name"] = a.string();
        n.texts["description"] = a.string();
        n.texts["path"] = a.string();
        n.numbers["time"] = a.u32();
        n.numbers["definition_flag"] = a.u8();
        for (int i = 0; i < 3; ++i)
            n.values.push_back(a.f64());
        n.refs["behavior"] = inline_object(a, "CComponentBehavior");
        n.numbers["definition_type"] = a.u32();
        n.refs["extension2"] = a.obj();
    };
    auto instance = [](Reader &a, Node &n) {
        schema(a, "CComponentInstance", 5);
        a.drawing(n);
        n.refs["definition"] = a.obj();
        for (int i = 0; i < 13; ++i)
            n.values.push_back(a.f64());
        n.texts["name"] = a.string();
        a.skip(16);
    };
    a.handlers["CComponentInstance"] = instance;
    a.handlers["CGroup"] = instance;
}
template <class T> T *allocate(std::vector<std::unique_ptr<T>> &owner) {
    owner.emplace_back(new T);
    return owner.back().get();
}
template <class T>
T *lookup(const std::unordered_map<Node *, T *> &map, Node *node, const char *expected) {
    auto it = map.find(node);
    if (it == map.end())
        throw std::runtime_error(std::string("Invalid archive reference to ") + expected);
    return it->second;
}
Node *reference(Node &n, const char *key) {
    auto it = n.refs.find(key);
    if (it == n.refs.end())
        throw std::runtime_error(std::string("Missing archive field ") + key);
    return it->second;
}
void finite(double value) {
    if (!std::isfinite(value))
        throw std::runtime_error("Non-finite geometry");
}
void texture_dimensions(const Reader &a, Node *dib, Texture &texture) {
    if (!dib)
        return;
    auto offset = dib->numbers.find("data_offset"), length = dib->numbers.find("data_size");
    if (offset == dib->numbers.end() || length == dib->numbers.end())
        return;
    size_t p = static_cast<size_t>(offset->second), size = static_cast<size_t>(length->second);
    if (p > a.data.size() || size > a.data.size() - p)
        throw std::runtime_error("Invalid image extent");
    const auto *b = a.data.data() + p;
    texture.image_data.assign(b, b + size);
    texture.format = static_cast<int>(dib->numbers.at("format"));
    if (size >= 24 && b[0] == 137 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        texture.pixel_width =
            (size_t(b[16]) << 24) | (size_t(b[17]) << 16) | (size_t(b[18]) << 8) | b[19];
        texture.pixel_height =
            (size_t(b[20]) << 24) | (size_t(b[21]) << 16) | (size_t(b[22]) << 8) | b[23];
    } else if (size >= 4 && b[0] == 255 && b[1] == 216) {
        size_t i = 2;
        while (i + 3 < size) {
            if (b[i++] != 255)
                break;
            while (i < size && b[i] == 255)
                ++i;
            if (i >= size)
                break;
            unsigned marker = b[i++];
            if (marker == 0xd9 || marker == 0xda)
                break;
            if (marker == 1 || (marker >= 0xd0 && marker <= 0xd7))
                continue;
            if (i + 2 > size)
                break;
            size_t len = size_t(b[i]) * 256 + b[i + 1];
            if (len < 2 || len > size - i)
                break;
            if (len >= 7 && marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 &&
                marker != 0xcc) {
                texture.pixel_height = size_t(b[i + 3]) * 256 + b[i + 4];
                texture.pixel_width = size_t(b[i + 5]) * 256 + b[i + 6];
                break;
            }
            i += len;
        }
    }
}
void material(Reader &a, Node *n, Material &out) {
    if (!n || n->class_name != "CMaterial")
        throw std::runtime_error("Invalid material reference");
    out.name = n->texts["name"];
    out.description = n->texts["description"];
    if (n->values.size() != 4)
        throw std::runtime_error("Invalid material color");
    for (int i = 0; i < 4; ++i)
        out.rgba[i] = static_cast<unsigned char>(n->values[i]);
    out.transparency = n->numbers["opacity"];
    out.use_opacity = n->numbers["use_opacity"] != 0;
    out.type = static_cast<int>(n->numbers["type"]);
    out.colorize_shift = n->numbers["unknown"] == 0;
    finite(out.transparency);
    auto it = n->refs.find("texture");
    if (it != n->refs.end() && it->second) {
        Node *t = it->second;
        out.texture.reset(new Texture);
        out.texture->filename = t->texts["filename"];
        out.texture->width = t->numbers["width"];
        out.texture->height = t->numbers["height"];
        texture_dimensions(a, reference(*t, "dib"), *out.texture);
    }
    if (out.texture) {
        finite(out.texture->width);
        finite(out.texture->height);
    }
    if (!out.texture)
        out.type = 0;
    else if (out.type == 1) {
        uint32_t color = static_cast<uint32_t>(it->second->numbers["flags"]);
        for (int i = 0; i < 4; ++i)
            out.rgba[i] = static_cast<unsigned char>(color >> (8 * i));
    }
    if (!(out.rgba[0] | out.rgba[1] | out.rgba[2] | out.rgba[3])) {
        out.rgba[0] = out.rgba[1] = out.rgba[2] = 64;
        out.rgba[3] = 255;
    }
}
std::unique_ptr<Model> build_model(Reader &a, Node *root, Node *component) {
    std::unique_ptr<Model> m(new Model);
    std::unordered_map<Node *, Vertex *> vertices;
    std::unordered_map<Node *, Edge *> edges;
    std::unordered_map<Node *, Loop *> loops;
    std::unordered_map<Node *, Face *> faces;
    std::unordered_map<Node *, Definition *> definitions;
    std::unordered_map<Node *, Group *> groups;
    std::unordered_map<Node *, Instance *> instances;
    std::vector<std::unique_ptr<Definition>> group_definitions;
    std::set<Node *> group_templates;
    std::set<Node *> declared_definitions;
    for (const auto &n : a.nodes)
        if (n->class_name == "CDefinitionList")
            for (Node *d : n->lists["definitions"]) {
                if (!d || d->class_name != "CComponentDefinition")
                    throw std::runtime_error("Invalid definition list");
                declared_definitions.insert(d);
            }
    for (const auto &n : a.nodes)
        if (n->class_name == "CComponentDefinition" && !declared_definitions.count(n.get()))
            throw std::runtime_error("Undeclared component definition");
    std::unordered_map<Node *, unsigned> visit;
    std::function<void(Node *, unsigned)> validate_tree = [&](Node *n, unsigned depth) {
        if (depth > 128)
            throw std::runtime_error("Component hierarchy exceeds depth limit");
        if (visit[n] == 1)
            throw std::runtime_error("Cyclic component hierarchy");
        if (visit[n] == 2)
            return;
        visit[n] = 1;
        for (Node *child : n->lists["entities"])
            if (child &&
                (child->class_name == "CGroup" || child->class_name == "CComponentInstance")) {
                if (child->class_name == "CGroup")
                    schema(a, "CGroup", 1);
                Node *d = reference(*child, "definition");
                if (!declared_definitions.count(d))
                    throw std::runtime_error("Undeclared instance definition");
                validate_tree(d, depth + 1);
            }
        visit[n] = 2;
    };
    validate_tree(component, 0);
    for (Node *d : declared_definitions)
        validate_tree(d, 0);
    std::unordered_map<Node *, Material *> materials;
    Node *manager = reference(*component, "materials");
    for (Node *n : manager->lists["materials"]) {
        auto out = allocate(m->metadata.materials);
        material(a, n, *out);
        materials[n] = out;
    }
    Node *layer_manager = reference(*component, "layers");
    for (Node *n : layer_manager->lists["layers"]) {
        if (!n || n->class_name != "CLayer")
            throw std::runtime_error("Invalid layer reference");
        auto out = allocate(m->metadata.layers);
        out->name = n->texts["name"];
        out->hidden = n->numbers["hidden"] != 0;
        material(a, reference(*n, "material"), out->material);
    }
    m->metadata.description = root->texts["description"];
    Node *camera = reference(*root, "camera");
    if (camera) {
        auto &out = m->metadata.camera;
        if (camera->class_name != "CCamera" || camera->values.size() < 9)
            throw std::runtime_error("Invalid camera");
        for (double d : camera->values)
            finite(d);
        for (const auto &d : camera->numbers)
            finite(d.second);
        std::copy(camera->values.begin(), camera->values.begin() + 3, out.eye);
        std::copy(camera->values.begin() + 3, camera->values.begin() + 6, out.target);
        std::copy(camera->values.begin() + 6, camera->values.begin() + 9, out.up);
        out.perspective = camera->numbers["perspective"] != 0;
        out.fov = camera->numbers["fov"];
        out.orthographic_height = camera->numbers["unknown1"];
        out.aspect_ratio = camera->numbers["unknown2"];
    }
    for (const auto &n : a.nodes)
        if (n->class_name == "CGroup")
            group_templates.insert(reference(*n, "definition"));
    for (const auto &node : a.nodes) {
        Node *n = node.get();
        if (n->class_name == "CVertex") {
            auto v = allocate(m->vertices);
            vertices[n] = v;
            if (n->values.size() != 3)
                throw std::runtime_error("Invalid vertex");
            for (double x : n->values)
                finite(x);
            v->position.x = n->values[0];
            v->position.y = n->values[1];
            v->position.z = n->values[2];
        } else if (n->class_name == "CEdge") {
            edges[n] = allocate(m->edges);
        } else if (n->class_name == "CLoop") {
            loops[n] = allocate(m->loops);
        } else if (n->class_name == "CFace") {
            faces[n] = allocate(m->faces);
        } else if (n->class_name == "CComponentDefinition") {
            auto d = allocate(group_templates.count(n) ? group_definitions : m->definitions);
            d->name = n->texts["name"];
            definitions[n] = d;
        } else if (n->class_name == "CGroup") {
            auto g = allocate(m->groups);
            g->name = n->texts["name"];
            groups[n] = g;
        } else if (n->class_name == "CComponentInstance") {
            auto in = allocate(m->instances);
            in->name = n->texts["name"];
            instances[n] = in;
        }
    }
    for (const auto &pair : edges) {
        Node &n = *pair.first;
        Edge &e = *pair.second;
        // CDrawingElement schema 9: first flag -> bit 0 (GetHidden at 0x10027DC0).
        e.hidden = n.numbers.at("flag0") != 0;
        // SUEdgeGetSoft (0x1000e2d0): drawing flags bit 3.
        e.soft = n.numbers.at("flag3") != 0;
        e.start = lookup(vertices, reference(n, "start"), "vertex");
        e.end = lookup(vertices, reference(n, "finish"), "vertex");
        e.standalone = reference(n, "curve") == nullptr;
    }
    for (const auto &pair : loops) {
        Node &n = *pair.first;
        Loop &loop = *pair.second;
        std::set<Node *> seen;
        for (Node *use : n.lists["uses"]) {
            if (!use || use->class_name != "CEdgeUse" || !seen.insert(use).second)
                throw std::runtime_error("Invalid edge-use list");
            if (reference(*use, "loop") != &n)
                throw std::runtime_error("Edge use belongs to another loop");
            Edge *e = lookup(edges, reference(*use, "edge"), "edge");
            bool reversed = use->numbers.at("reversed") != 0;
            loop.edges.push_back(e);
            loop.vertices.push_back(reversed ? e->end : e->start);
            e->standalone = false;
        }
        if (loop.vertices.size() < 3)
            throw std::runtime_error("Face loop has fewer than three vertices");
        for (size_t i = 0; i < loop.edges.size(); ++i) {
            auto e = loop.edges[i];
            auto start = loop.vertices[i];
            auto end = start == e->start ? e->end : e->start;
            if (end != loop.vertices[(i + 1) % loop.vertices.size()])
                throw std::runtime_error("Disconnected face loop");
        }
    }
    auto resolve_material = [&](Node *n) -> Material * {
        if (!n)
            return nullptr;
        auto it = materials.find(n);
        if (it != materials.end())
            return it->second;
        auto out = allocate(m->extra_materials);
        material(a, n, *out);
        materials[n] = out;
        return out;
    };
    for (const auto &pair : faces) {
        Node &n = *pair.first;
        Face &f = *pair.second;
        if (n.values.size() != 4)
            throw std::runtime_error("Invalid face plane");
        for (double x : n.values)
            finite(x);
        std::copy(n.values.begin(), n.values.end(), f.plane);
        f.has_plane = true;
        for (Node *loop : n.lists["loops"]) {
            auto l = lookup(loops, loop, "loop");
            if (loop->numbers.at("flag0") != 0) {
                if (f.outer)
                    throw std::runtime_error("Multiple outer loops");
                f.outer = l;
            } else
                f.inner.push_back(l);
        }
        if (!f.outer)
            throw std::runtime_error("Face has no outer loop");
        f.front_material = resolve_material(reference(n, "material"));
        f.back_material = resolve_material(reference(n, "back_material"));
        std::set<Node *> visited_attributes;
        std::function<void(Node *)> read_mapping = [&](Node *attribute) {
            if (!attribute || !visited_attributes.insert(attribute).second)
                return;
            if (attribute->class_name == "CFaceTextureCoords") {
                if (attribute->values.size() < 24)
                    throw std::runtime_error("Incomplete face texture mapping");
                for (int side = 0; side < 2; ++side) {
                    auto &mapping = side ? f.back_mapping : f.front_mapping;
                    std::copy_n(attribute->values.begin() + side * 12, 9, mapping.matrix);
                    std::copy_n(attribute->values.begin() + side * 12 + 9, 3, mapping.projection);
                    mapping.flags = static_cast<unsigned>(
                        attribute->numbers[side ? "back_flags" : "front_flags"]);
                    for (double x : mapping.matrix)
                        finite(x);
                    for (double x : mapping.projection)
                        finite(x);
                }
            } else if (attribute->class_name == "CAttributeContainer") {
                for (auto item : attribute->lists["items"])
                    read_mapping(item);
            }
        };
        read_mapping(reference(n, "attributes"));
    }
    auto fill_entities = [&](Node *source, Entities &out) {
        for (Node *n : source->lists["entities"]) {
            if (!n)
                throw std::runtime_error("Null model entity");
            if (n->class_name == "CFace")
                out.faces.push_back(lookup(faces, n, "face"));
            else if (n->class_name == "CEdge")
                out.edges.push_back(lookup(edges, n, "edge"));
            else if (n->class_name == "CGroup")
                out.groups.push_back(lookup(groups, n, "group"));
            else if (n->class_name == "CComponentInstance")
                out.instances.push_back(lookup(instances, n, "instance"));
            else
                throw std::runtime_error("Unsupported model entity: " + n->class_name);
        }
    };
    fill_entities(component, m->entities);
    for (const auto &p : definitions)
        fill_entities(p.first, p.second->entities);
    auto transform = [](Node *n, Transform &t) {
        if (n->values.size() != 13)
            throw std::runtime_error("Invalid transform");
        for (double d : n->values)
            finite(d);
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 3; ++row)
                t.values[4 * col + row] = n->values[3 * row + col];
        for (int i = 0; i < 3; ++i)
            t.values[12 + i] = n->values[9 + i];
        t.values[15] = n->values[12];
    };
    for (const auto &p : groups) {
        p.second->material = resolve_material(reference(*p.first, "material"));
        auto d = lookup(definitions, reference(*p.first, "definition"), "group definition");
        p.second->entities = d->entities;
        transform(p.first, p.second->transform);
    }
    for (const auto &p : instances) {
        p.second->material = resolve_material(reference(*p.first, "material"));
        p.second->definition =
            lookup(definitions, reference(*p.first, "definition"), "component definition");
        if (group_templates.count(reference(*p.first, "definition")))
            throw std::runtime_error("Group definition used by component instance");
        transform(p.first, p.second->transform);
    }
    return m;
}
} // namespace
std::unique_ptr<Model> load_model_file(const char *path) {
    if (!path)
        throw LoadError(SU_ERROR_NULL_POINTER_INPUT, "Null model path");
#ifdef _WIN32
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (!length)
        throw LoadError(SU_ERROR_SERIALIZATION, "Model path is not valid UTF-8");
    std::vector<wchar_t> wide_path(static_cast<size_t>(length));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path.data(), length);
    std::ifstream file(wide_path.data(), std::ios::binary | std::ios::ate);
#else
    std::ifstream file(path, std::ios::binary | std::ios::ate);
#endif
    if (!file)
        throw LoadError(SU_ERROR_SERIALIZATION, "Cannot open model file");
    auto size = file.tellg();
    if (size < 0 || size > 512 * 1024 * 1024)
        throw LoadError(SU_ERROR_SERIALIZATION, "Invalid or oversized archive");
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!bytes.empty() && !file.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
        throw LoadError(SU_ERROR_SERIALIZATION, "Cannot read model file");
    try {
        Reader reader(std::move(bytes));
        register_geometry(reader);
        Node *root = reader.root_prefix();
        Node *component = inline_object(reader, "CComponent");
        reader.root_suffix(*root);
        return build_model(reader, root, component);
    } catch (const LoadError &) {
        throw;
    } catch (const std::exception &e) {
        throw LoadError(SU_ERROR_SERIALIZATION, e.what());
    }
}
} // namespace skp
