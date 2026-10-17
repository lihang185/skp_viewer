#include "skp_triangulate.hpp"
#include "tesselator.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace skp {
namespace {
struct P2 {
    double x, y;
};
Point sub(Point a, Point b) {
    Point p;
    p.x = a.x - b.x;
    p.y = a.y - b.y;
    p.z = a.z - b.z;
    return p;
}
Point cross(Point a, Point b) {
    Point p;
    p.x = a.y * b.z - a.z * b.y;
    p.y = a.z * b.x - a.x * b.z;
    p.z = a.x * b.y - a.y * b.x;
    return p;
}
double dot(Point a, Point b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Point unit(Point a) {
    double d = std::sqrt(dot(a, a));
    if (!(d > 0) || !std::isfinite(d))
        throw std::runtime_error("Invalid face normal");
    a.x /= d;
    a.y /= d;
    a.z /= d;
    return a;
}
double turn(P2 a, P2 b, P2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
} // namespace
TriangleMesh triangulate_face(const Face &face) {
    if (!face.outer || face.outer->vertices.size() < 3)
        throw std::runtime_error("Missing outer loop");
    Point origin = face.outer->vertices[0]->position, normal;
    normal.x = face.plane[0];
    normal.y = face.plane[1];
    normal.z = face.plane[2];
    normal = unit(normal);
    Point axis;
    if (std::abs(normal.x) < 0.8)
        axis.x = 1;
    else
        axis.y = 1;
    Point u = unit(cross(axis, normal)), v = cross(normal, u);
    std::vector<const Loop *> loops{face.outer};
    for (auto l : face.inner)
        loops.push_back(l);
    double scale = 0;
    for (auto l : loops) {
        if (!l)
            throw std::runtime_error("Null loop");
        for (auto p : l->vertices) {
            auto d = sub(p->position, origin);
            scale = std::max(scale, std::sqrt(dot(d, d)));
        }
    }
    if (!(scale > 0) || !std::isfinite(scale))
        throw std::runtime_error("Degenerate face");
    std::vector<std::vector<P2>> contours;
    std::vector<Point> original;
    for (auto l : loops) {
        std::vector<P2> ring;
        std::vector<Point> points;
        for (auto vert : l->vertices) {
            auto d = sub(vert->position, origin);
            P2 p{dot(d, u) / scale, dot(d, v) / scale};
            if (!std::isfinite(p.x) || !std::isfinite(p.y))
                throw std::runtime_error("Invalid face coordinate");
            if (!ring.empty() && std::hypot(p.x - ring.back().x, p.y - ring.back().y) <= 1e-12)
                continue;
            ring.push_back(p);
            points.push_back(vert->position);
        }
        if (ring.size() > 1 &&
            std::hypot(ring.front().x - ring.back().x, ring.front().y - ring.back().y) <= 1e-12) {
            ring.pop_back();
            points.pop_back();
        }
        if (ring.size() < 3)
            throw std::runtime_error("Degenerate contour");
        contours.push_back(ring);
        original.insert(original.end(), points.begin(), points.end());
    }
    TriangleMesh mesh;
    auto &r = contours[0];
    double area = 0;
    for (size_t i = 0; i < r.size(); ++i)
        area += r[i].x * r[(i + 1) % r.size()].y - r[i].y * r[(i + 1) % r.size()].x;
    double sign = area >= 0 ? 1 : -1;
    // A supporting half-plane test also excludes self-intersecting stars.
    bool convex = contours.size() == 1 && std::abs(area) > 1e-14;
    for (size_t i = 0; convex && i < r.size(); ++i)
        for (size_t j = 0; j < r.size(); ++j)
            if (sign * turn(r[i], r[(i + 1) % r.size()], r[j]) < -1e-13) {
                convex = false;
                break;
            }
    std::vector<P2> output;
    if (convex) {
        mesh.used_fan = true;
        mesh.vertices = original;
        output = r;
        for (uint32_t i = 1; i + 1 < r.size(); ++i)
            mesh.indices.insert(mesh.indices.end(), {0, i, i + 1});
    } else {
        std::unique_ptr<TESStesselator, decltype(&tessDeleteTess)> tess(tessNewTess(nullptr),
                                                                        tessDeleteTess);
        if (!tess)
            throw std::bad_alloc();
        for (auto &ring : contours)
            tessAddContour(tess.get(), 2, ring.data(), sizeof(P2), int(ring.size()));
        TESSreal n[3] = {0, 0, 1};
        if (!tessTesselate(tess.get(), TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, n))
            throw std::runtime_error("libtess2 failed");
        auto p = tessGetVertices(tess.get());
        auto source = tessGetVertexIndices(tess.get());
        for (int i = 0; i < tessGetVertexCount(tess.get()); ++i) {
            P2 q{p[2 * i], p[2 * i + 1]};
            output.push_back(q);
            Point world;
            if (source[i] != TESS_UNDEF)
                world = original.at(source[i]);
            else {
                world.x = origin.x + scale * (u.x * q.x + v.x * q.y);
                world.y = origin.y + scale * (u.y * q.x + v.y * q.y);
                world.z = origin.z + scale * (u.z * q.x + v.z * q.y);
            }
            mesh.vertices.push_back(world);
        }
        auto indices = tessGetElements(tess.get());
        for (int i = 0; i < tessGetElementCount(tess.get()) * 3; ++i) {
            if (indices[i] == TESS_UNDEF)
                throw std::runtime_error("Incomplete triangle");
            mesh.indices.push_back(uint32_t(indices[i]));
        }
    }
    std::vector<uint32_t> valid;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        double s = turn(output[a], output[b], output[c]);
        if (std::abs(s) <= 1e-15)
            continue;
        if (s < 0)
            std::swap(b, c);
        valid.insert(valid.end(), {a, b, c});
    }
    mesh.indices.swap(valid);
    if (mesh.indices.empty())
        throw std::runtime_error("Face has no nondegenerate triangles");
    return mesh;
}
} // namespace skp
