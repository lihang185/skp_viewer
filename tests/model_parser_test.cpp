#include "skp_model.hpp"
#include "legacy_reader.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

using Bytes = std::vector<uint8_t>;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> static void rejects(F f, const char *message) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, message);
}
static void put16(Bytes &b, unsigned n) {
    b.push_back(uint8_t(n));
    b.push_back(uint8_t(n >> 8));
}
static void put32(Bytes &b, unsigned n) {
    put16(b, n);
    put16(b, n >> 16);
}
static size_t find_bytes(const Bytes &b, const Bytes &needle) {
    auto p = std::search(b.begin(), b.end(), needle.begin(), needle.end());
    check(p != b.end(), "Test mutation marker not found");
    return size_t(p - b.begin());
}

static void reader_tests() {
    // Sparse masks must not be interpreted as typed-value tags or lengths.
    legacy::Reader ids(Bytes{0, 0, 0x81, 0x34, 0x12});
    ids.versions["CEntity"] = 5;
    legacy::Node entity;
    ids.entity(entity);
    check(entity.texts["persistent_id"] == "1297036692682702900" && ids.pos == 5,
          "Sparse 64-bit persistent ID decoding");
    legacy::Reader short_id(Bytes{0, 0, 0x81, 0x34});
    short_id.versions["CEntity"] = 5;
    rejects([&] { short_id.entity(entity); }, "Truncated persistent ID accepted");
    legacy::Reader ascii(Bytes{3, 'a', 'b', 'c'});
    check(ascii.string() == "abc", "ASCII CString");
    // UTF-16: Chinese U+4E2D and a supplementary Unicode character U+1F600.
    legacy::Reader unicode(Bytes{255, 254, 255, 3, 0x2d, 0x4e, 0x3d, 0xd8, 0, 0xde});
    check(unicode.string() == "\xe4\xb8\xad\xf0\x9f\x98\x80", "UTF-16 surrogate decoding");
    legacy::Reader truncated(Bytes{255, 254, 255, 2, 0x2d, 0x4e});
    rejects([&] { truncated.string(); }, "Truncated UTF-16 accepted");
    legacy::Reader bad_reference(Bytes{1, 0});
    rejects([&] { bad_reference.obj(); }, "Forward object reference accepted");
    Bytes b;
    put16(b, 65535);
    put16(b, 0);
    put16(b, 1);
    b.push_back('T');
    put16(b, 2); // Newly allocated object refers to itself before its serializer returns.
    put16(b, 2); // Repeated object reference must preserve identity.
    put16(b, 0x8001);
    put16(b, 3); // Reuse class slot 1 for object slot 3.
    put16(b, 0x7fff);
    put32(b, 3); // Extended object reference.
    legacy::Reader missing_version(b);
    missing_version.handlers["T"] = [](legacy::Reader &, legacy::Node &) {};
    rejects([&] { missing_version.obj(); }, "Missing class version accepted");
    legacy::Reader mismatched_schema(b);
    mismatched_schema.versions["T"] = 1;
    mismatched_schema.handlers["T"] = [](legacy::Reader &, legacy::Node &) {};
    rejects([&] { mismatched_schema.obj(); }, "Class descriptor/version-map mismatch accepted");
    legacy::Reader r(b);
    r.versions["T"] = 0;
    r.handlers["T"] = [](legacy::Reader &a, legacy::Node &n) { n.refs["self"] = a.obj(); };
    auto first = r.obj();
    check(first->refs.at("self") == first, "Recursive object reference lost");
    check(r.obj() == first, "Shared object identity lost");
    auto second = r.obj();
    check(second != first && second->refs.at("self") == second,
          "Class reuse allocated wrong object");
    check(r.obj() == second && r.pos == b.size(), "Extended object reference failed");
    legacy::Reader class_as_object(Bytes{255, 255, 0, 0, 1, 0, 'T', 1, 0});
    class_as_object.versions["T"] = 0;
    class_as_object.handlers["T"] = [](legacy::Reader &a, legacy::Node &) { a.obj(); };
    rejects([&] { class_as_object.obj(); }, "Class slot accepted as object");
}

static void topology_tests(const skp::Model &m) {
    check(m.vertices.size() == 16 && m.edges.size() == 24 && m.faces.size() == 11 &&
              m.loops.size() == 12,
          "Sample geometry counts");
    check(m.entities.faces.size() == 11 && m.entities.edges.size() == 24, "Root geometry counts");
    std::set<const skp::Vertex *> vertices;
    for (const auto &v : m.vertices)
        vertices.insert(v.get());
    std::set<const skp::Edge *> edges;
    for (const auto &e : m.edges)
        edges.insert(e.get());
    std::map<const skp::Edge *, unsigned> incidence;
    size_t inner_count = 0, uses = 0;
    for (const auto &face : m.faces) {
        check(face->outer != nullptr, "Missing outer loop");
        inner_count += face->inner.size();
        std::vector<skp::Loop *> loops(1, face->outer);
        loops.insert(loops.end(), face->inner.begin(), face->inner.end());
        for (auto loop : loops) {
            check(loop->edges.size() == 4 && loop->vertices.size() == 4, "Sample loop size");
            std::set<const skp::Vertex *> distinct;
            for (size_t i = 0; i < 4; ++i) {
                auto e = loop->edges[i];
                auto v = loop->vertices[i];
                auto next = loop->vertices[(i + 1) % 4];
                check(edges.count(e) && vertices.count(v), "Topology references non-owned object");
                check((e->start == v && e->end == next) || (e->end == v && e->start == next),
                      "Loop edge endpoints disconnected");
                check(!e->standalone, "Face edge marked standalone");
                ++incidence[e];
                ++uses;
                distinct.insert(v);
            }
            check(distinct.size() == 4, "Repeated loop vertex");
        }
    }
    check(inner_count == 1 && uses == 48, "Inner-loop or edge-use count");
    for (const auto &e : m.edges)
        check(incidence[e.get()] == 2, "Shared edge does not have two incident faces");
}

int main(int argc, char **argv) {
    const std::string temporary = argc > 2 ? argv[2] : "model_parser_negative.tmp.skp";
    try {
        check(argc >= 2, "Usage: model_parser_test sample.skp [temporary.skp]");
        reader_tests();
        auto model = skp::load_model_file(argv[1]);
        topology_tests(*model);
        std::ifstream input(argv[1], std::ios::binary);
        check(bool(input), "Cannot read fixture");
        Bytes bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        auto reject_file = [&](const Bytes &data, const char *reason) {
            {
                std::ofstream out(temporary.c_str(), std::ios::binary | std::ios::trunc);
                check(bool(out), "Cannot create temporary fixture");
                out.write(reinterpret_cast<const char *>(data.data()), data.size());
            }
            rejects([&] { skp::load_model_file(temporary.c_str()); }, reason);
        };
        legacy::Reader header(bytes);
        header.base_header();
        for (size_t n = 0; n < header.pos; ++n) {
            legacy::Reader prefix(Bytes(bytes.begin(), bytes.begin() + n));
            rejects([&] { prefix.base_header(); }, "Truncated header accepted");
        }
        // Discover the first actual CVertex class declaration, not a production offset.
        Bytes marker{255, 255, 0, 0, 7, 0, 'C', 'V', 'e', 'r', 't', 'e', 'x'};
        size_t vertex = find_bytes(bytes, marker), coordinate = vertex + marker.size() + 2;
        for (size_t n :
             std::vector<size_t>{0, 1, 30, 75, header.pos - 1, header.pos, vertex, coordinate,
                                 coordinate + 7, coordinate + 23, bytes.size() / 2})
            reject_file(Bytes(bytes.begin(), bytes.begin() + n), "Truncated payload accepted");
        Bytes version_name{255, 254, 255, 7};
        for (char c : std::string("CVertex")) {
            version_name.push_back(uint8_t(c));
            version_name.push_back(0);
        }
        auto changed = bytes;
        size_t version = find_bytes(bytes, version_name) + version_name.size();
        changed[version] = 255;
        reject_file(changed, "Unknown geometry class version accepted");
        changed = bytes;
        for (size_t i = 0; i < 8; ++i)
            changed[coordinate + i] = 0;
        changed[coordinate + 6] = 0xf0;
        changed[coordinate + 7] = 0x7f;
        reject_file(changed, "Infinite vertex coordinate accepted");
        changed = bytes;
        changed[4] = 'X';
        reject_file(changed, "Invalid magic accepted");
        reject_file(Bytes(bytes.begin(), bytes.end() - 1), "Truncated model suffix accepted");
        std::remove(temporary.c_str());
        std::cout << "Reader, topology, truncation and malformed-input checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::remove(temporary.c_str());
        std::cerr << e.what() << '\n';
        return 1;
    }
}
