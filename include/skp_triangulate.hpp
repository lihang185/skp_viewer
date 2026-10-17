#pragma once
#include "skp_model.hpp"
#include <cstdint>
namespace skp {
struct TriangleMesh {
    // Coordinates remain in the face's component-local coordinate system.
    std::vector<Point> vertices;
    // Three indices per triangle; winding agrees with Face::plane normal.
    std::vector<uint32_t> indices;
    bool used_fan = false;
};
TriangleMesh triangulate_face(const Face &face);
} // namespace skp
