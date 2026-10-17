#include "skp_mesh_api.h"
#include "skp_triangulate.hpp"
#include <algorithm>
#include <cmath>
SUResult SU_CALL SKPEdgeGetHidden(SUEdgeRef edge, bool *hidden) {
    if (!edge.ptr) return SU_ERROR_INVALID_INPUT;
    if (!hidden) return SU_ERROR_NULL_POINTER_OUTPUT;
    *hidden = static_cast<skp::Edge *>(edge.ptr)->hidden;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SKPEdgeGetSoft(SUEdgeRef edge, bool *soft) {
    if (!edge.ptr) return SU_ERROR_INVALID_INPUT;
    if (!soft) return SU_ERROR_NULL_POINTER_OUTPUT;
    *soft = static_cast<skp::Edge *>(edge.ptr)->soft;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SKPMaterialGetColorizeShift(SUMaterialRef material, bool *shift) {
    if (!material.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!shift)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *shift = static_cast<skp::Material *>(material.ptr)->colorize_shift;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SKPFaceCreateMesh(SUFaceRef face, SKPMeshRef *mesh) {
    if (!face.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!mesh)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    if (mesh->ptr)
        return SU_ERROR_INVALID_OUTPUT;
    try {
        mesh->ptr =
            new skp::TriangleMesh(skp::triangulate_face(*static_cast<skp::Face *>(face.ptr)));
        return SU_ERROR_NONE;
    } catch (...) {
        return SU_ERROR_GENERIC;
    }
}
SUResult SU_CALL SKPMeshRelease(SKPMeshRef *mesh) {
    if (!mesh)
        return SU_ERROR_NULL_POINTER_INPUT;
    if (!mesh->ptr)
        return SU_ERROR_INVALID_INPUT;
    delete static_cast<skp::TriangleMesh *>(mesh->ptr);
    mesh->ptr = nullptr;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SKPMeshGetSizes(SKPMeshRef mesh, size_t *vertices, size_t *indices) {
    if (!mesh.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!vertices || !indices)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto m = static_cast<skp::TriangleMesh *>(mesh.ptr);
    *vertices = m->vertices.size();
    *indices = m->indices.size();
    return SU_ERROR_NONE;
}
SUResult SU_CALL SKPMeshGetData(SKPMeshRef mesh, size_t vc, SUPoint3D *vertices, size_t ic,
                                unsigned int *indices) {
    if (!mesh.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!vertices || !indices)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto m = static_cast<skp::TriangleMesh *>(mesh.ptr);
    if (vc < m->vertices.size() || ic < m->indices.size())
        return SU_ERROR_INSUFFICIENT_SIZE;
    for (size_t i = 0; i < m->vertices.size(); ++i) {
        vertices[i].x = m->vertices[i].x;
        vertices[i].y = m->vertices[i].y;
        vertices[i].z = m->vertices[i].z;
    }
    std::copy(m->indices.begin(), m->indices.end(), indices);
    return SU_ERROR_NONE;
}

SUResult SU_CALL SKPTextureGetDataSize(SUTextureRef texture, size_t *size) {
    if (!texture.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!size)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *size = static_cast<skp::Texture *>(texture.ptr)->image_data.size();
    return SU_ERROR_NONE;
}
SUResult SU_CALL SKPTextureGetData(SUTextureRef texture, size_t capacity, unsigned char *data) {
    if (!texture.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!data)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    const auto &image = static_cast<skp::Texture *>(texture.ptr)->image_data;
    if (capacity < image.size())
        return SU_ERROR_INSUFFICIENT_SIZE;
    std::copy(image.begin(), image.end(), data);
    return SU_ERROR_NONE;
}
SUResult SU_CALL SKPGroupGetMaterial(SUGroupRef group, SUMaterialRef *material) {
    if (!group.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!material)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    material->ptr = static_cast<skp::Group *>(group.ptr)->material;
    return material->ptr ? SU_ERROR_NONE : SU_ERROR_NO_DATA;
}
SUResult SU_CALL SKPInstanceGetMaterial(SUComponentInstanceRef instance, SUMaterialRef *material) {
    if (!instance.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!material)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    material->ptr = static_cast<skp::Instance *>(instance.ptr)->material;
    return material->ptr ? SU_ERROR_NONE : SU_ERROR_NO_DATA;
}

// Validated against SUUVHelper on the original DLL; see analysis/uv_original_validation.json.
SUResult SU_CALL SKPFaceGetUVQ(SUFaceRef face, bool back, SUMaterialRef material, size_t count,
                               const SUPoint3D *points, SUPoint3D *uvq) {
    if (!face.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!points)
        return SU_ERROR_NULL_POINTER_INPUT;
    if (!uvq)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto &f = *static_cast<skp::Face *>(face.ptr);
    auto &map = back ? f.back_mapping : f.front_mapping;
    auto mat = static_cast<skp::Material *>(material.ptr);
    if (!mat)
        mat = back ? f.back_material : f.front_material;
    unsigned flags = mat && mat->texture ? map.flags : 0;
    double nx = f.plane[0], ny = f.plane[1], nz = f.plane[2];
    if ((flags & 2) && (map.projection[0] || map.projection[1] || map.projection[2])) {
        nx = map.projection[0];
        ny = map.projection[1];
        nz = map.projection[2];
    }
    double length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!(length > 0) || !std::isfinite(length))
        return SU_ERROR_GENERIC;
    nx /= length;
    ny /= length;
    nz /= length;
    double ux = -ny, uy = nx, uz = 0;
    double ul = std::sqrt(ux * ux + uy * uy);
    if (std::abs(nx) <= 0.001 && std::abs(ny) <= 0.001 && std::abs(std::abs(nz) - 1) <= 0.001) {
        ux = nz;
        uy = 0;
        uz = -nx;
        ul = std::sqrt(ux * ux + uz * uz);
    }
    ux /= ul;
    uy /= ul;
    uz /= ul;
    double vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux;
    double inv[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool fallback = false;
    if (flags & 1) {
        const auto &m = map.matrix;
        double a = m[0], b = m[3], c = m[6], d = m[1], e = m[4], g = m[2], h = m[5], j = m[8],
               ff = m[7];
        double det = a * (e * j - ff * h) - b * (d * j - ff * g) + c * (d * h - e * g);
        if (!std::isfinite(det))
            return SU_ERROR_GENERIC;
        double cof[9] = {e * j - ff * h, c * h - b * j, b * ff - c * e,
                         ff * g - d * j, a * j - c * g, c * d - a * ff,
                         d * h - e * g,  b * g - a * h, a * e - b * d};
        // Original inverse retains the adjugate when abs(det) < 1e-6.
        for (int k = 0; k < 9; ++k)
            inv[k] = std::abs(det) >= 1e-6 ? cof[k] / det : cof[k];
        if (std::abs(inv[6]) < 1e-6 && std::abs(inv[7]) < 1e-6 && std::abs(inv[8]) >= 1e-6) {
            double q = inv[8];
            for (double &x : inv)
                x /= q;
        }
        for (size_t i = 0; i < count; ++i) {
            auto p = points[i];
            double x = p.x * ux + p.y * uy + p.z * uz, y = p.x * vx + p.y * vy + p.z * vz;
            if (std::abs(inv[6] * x + inv[7] * y + inv[8]) < 1e-15) {
                fallback = true;
                break;
            }
        }
        if (fallback) {
            for (double &x : inv)
                x = 0;
            inv[0] = inv[4] = inv[8] = 1;
        }
    }
    double w = 1, h = 1;
    if (mat && mat->texture) {
        w = mat->texture->width;
        h = mat->texture->height;
    }
    if (w == 0 || h == 0 || !std::isfinite(w) || !std::isfinite(h))
        return SU_ERROR_GENERIC;
    for (size_t i = 0; i < count; ++i) {
        auto p = points[i];
        double x = p.x * ux + p.y * uy + p.z * uz, y = p.x * vx + p.y * vy + p.z * vz;
        uvq[i].x = (inv[0] * x + inv[1] * y + inv[2]) / w;
        uvq[i].y = (inv[3] * x + inv[4] * y + inv[5]) / h;
        uvq[i].z = inv[6] * x + inv[7] * y + inv[8];
        if (!std::isfinite(uvq[i].x) || !std::isfinite(uvq[i].y) || !std::isfinite(uvq[i].z))
            return SU_ERROR_GENERIC;
    }
    return fallback ? SU_ERROR_PARTIAL_SUCCESS : SU_ERROR_NONE;
}
