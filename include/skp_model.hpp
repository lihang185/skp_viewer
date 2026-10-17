#pragma once
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include "skp_metadata.hpp"
namespace skp {
struct Point {
    double x = 0, y = 0, z = 0;
};
struct Vertex {
    Point position;
};
struct Edge {
    bool hidden = false;
    bool soft = false;
    Vertex *start = nullptr;
    Vertex *end = nullptr;
    bool standalone = false;
};
// vertices[i] starts the oriented edge edges[i].
struct Loop {
    std::vector<Vertex *> vertices;
    std::vector<Edge *> edges;
};
struct Face {
    struct TextureMapping {
        double matrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // column-major texture-to-plane map
        double projection[3] = {0, 0, 0};
        unsigned flags = 0;
    };
    Loop *outer = nullptr;
    std::vector<Loop *> inner;
    double plane[4] = {0, 0, 0, 0};
    bool has_plane = false;
    Material *front_material = nullptr;
    Material *back_material = nullptr;
    TextureMapping front_mapping, back_mapping;
};
struct Group;
struct Instance;
struct Entities {
    std::vector<Face *> faces;
    std::vector<Edge *> edges;
    std::vector<Group *> groups;
    std::vector<Instance *> instances;
};
struct Transform {
    double values[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
struct Definition {
    std::string name;
    Entities entities;
};
struct Group {
    std::string name;
    Entities entities;
    Transform transform;
    Material *material = nullptr;
};
struct Instance {
    std::string name;
    Definition *definition = nullptr;
    Transform transform;
    Material *material = nullptr;
};
// Separate allocations keep handles stable when ownership vectors grow.
struct Model {
    std::string name;
    Entities entities;
    Metadata metadata;
    // Materials referenced by a nested definition but absent from the root's material list.
    std::vector<std::unique_ptr<Material>> extra_materials;
    std::vector<std::unique_ptr<Vertex>> vertices;
    std::vector<std::unique_ptr<Edge>> edges;
    std::vector<std::unique_ptr<Loop>> loops;
    std::vector<std::unique_ptr<Face>> faces;
    std::vector<std::unique_ptr<Group>> groups;
    std::vector<std::unique_ptr<Instance>> instances;
    std::vector<std::unique_ptr<Definition>> definitions;
};
struct LoadError : std::runtime_error {
    int result;
    LoadError(int code, const std::string &text) : std::runtime_error(text), result(code) {}
};
std::unique_ptr<Model> load_model_file(const char *path);
} // namespace skp
