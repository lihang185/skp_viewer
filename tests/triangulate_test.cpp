#include "skp_triangulate.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace skp;
static void check(bool b, const char *msg) {
    if (!b)
        throw std::runtime_error(msg);
}
static double loop_area(const Loop &l, const Face &f) {
    auto o = l.vertices[0]->position;
    double sum = 0, n = std::sqrt(f.plane[0] * f.plane[0] + f.plane[1] * f.plane[1] +
                                  f.plane[2] * f.plane[2]);
    for (size_t i = 1; i + 1 < l.vertices.size(); ++i) {
        auto a = l.vertices[i]->position, b = l.vertices[i + 1]->position;
        a.x -= o.x;
        a.y -= o.y;
        a.z -= o.z;
        b.x -= o.x;
        b.y -= o.y;
        b.z -= o.z;
        sum += ((a.y * b.z - a.z * b.y) * f.plane[0] + (a.z * b.x - a.x * b.z) * f.plane[1] +
                (a.x * b.y - a.y * b.x) * f.plane[2]) /
               n / 2;
    }
    return std::abs(sum);
}
static void validate(const Face &f, const TriangleMesh &m) {
    double expected = loop_area(*f.outer, f);
    for (auto l : f.inner)
        expected -= loop_area(*l, f);
    double actual = 0;
    for (size_t i = 0; i < m.indices.size(); i += 3) {
        auto a = m.vertices.at(m.indices[i]), b = m.vertices.at(m.indices[i + 1]),
             c = m.vertices.at(m.indices[i + 2]);
        b.x -= a.x;
        b.y -= a.y;
        b.z -= a.z;
        c.x -= a.x;
        c.y -= a.y;
        c.z -= a.z;
        double x = b.y * c.z - b.z * c.y, y = b.z * c.x - b.x * c.z, z = b.x * c.y - b.y * c.x;
        check(x * f.plane[0] + y * f.plane[1] + z * f.plane[2] > 0, "Triangle winding/degeneracy");
        actual += (x * f.plane[0] + y * f.plane[1] + z * f.plane[2]) /
                  (2 * std::sqrt(f.plane[0] * f.plane[0] + f.plane[1] * f.plane[1] +
                                 f.plane[2] * f.plane[2]));
    }
    if (std::abs(actual - expected) > std::max(1e-8, expected * 1e-7))
        throw std::runtime_error("Triangle area mismatch: " + std::to_string(actual) +
                                 " expected " + std::to_string(expected));
}
int main(int argc, char **argv) {
    try {
        if (argc == 1) {
            Vertex p[8];
            double xy[8][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {1, 1}, {3, 1}, {3, 3}, {1, 3}};
            for (int i = 0; i < 8; ++i) {
                p[i].position.x = xy[i][0];
                p[i].position.y = xy[i][1];
            }
            Loop outer, hole;
            for (int i = 0; i < 4; ++i)
                outer.vertices.push_back(p + i);
            for (int i = 4; i < 8; ++i)
                hole.vertices.push_back(p + i);
            Face f;
            f.outer = &outer;
            f.plane[2] = 1;
            f.has_plane = true;
            auto m = triangulate_face(f);
            check(m.used_fan && m.indices.size() == 6, "Convex fan");
            validate(f, m);
            f.inner.push_back(&hole);
            m = triangulate_face(f);
            check(!m.used_fan, "Hole routed to fan");
            validate(f, m);
            for (size_t i = 0; i < m.indices.size(); i += 3) {
                double x = 0, y = 0;
                for (int j = 0; j < 3; ++j) {
                    x += m.vertices[m.indices[i + j]].x / 3;
                    y += m.vertices[m.indices[i + j]].y / 3;
                }
                check(!(x > 1 && x < 3 && y > 1 && y < 3), "Hole filled");
            }
            f.inner.clear();
            p[2].position.x = 1;
            p[2].position.y = 1;
            m = triangulate_face(f);
            check(!m.used_fan, "Concave fan");
            validate(f, m);
            std::reverse(outer.vertices.begin(), outer.vertices.end());
            validate(f, triangulate_face(f));
            // Closing and consecutive duplicate vertices must not make zero-area triangles.
            outer.vertices.push_back(outer.vertices.front());
            outer.vertices.insert(outer.vertices.begin()+1, outer.vertices.front());
            validate(f, triangulate_face(f));
            // Rotate the same contour into the YZ plane.
            for (auto &vertex : p) {
                vertex.position.z = vertex.position.x;
                vertex.position.x = 0;
            }
            f.plane[2] = 0;
            f.plane[0] = -1;
            validate(f, triangulate_face(f));
            for (auto &v : p) {
                v.position.x += 1e9;
                v.position.y += 1e9;
            }
            validate(f, triangulate_face(f));
            std::cout
                << "Synthetic convex, concave, hole, reversed and large-coordinate tests passed\n";
        } else {
            auto model = load_model_file(argv[1]);
            size_t fan = 0, tess = 0, triangles = 0;
            for (size_t i = 0; i < model->faces.size(); ++i) {
                try {
                    auto m = triangulate_face(*model->faces[i]);
                    validate(*model->faces[i], m);
                    fan += m.used_fan;
                    tess += !m.used_fan;
                    triangles += m.indices.size() / 3;
                } catch (const std::exception &e) {
                    throw std::runtime_error("face " + std::to_string(i) + ": " + e.what());
                }
            }
            std::cout << "faces=" << model->faces.size() << " fan=" << fan << " tess=" << tess
                      << " triangles=" << triangles << "\n";
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
