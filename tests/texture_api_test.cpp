#include "skp_mesh_api.h"
#include "skp_model.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool value) {
    if (!value)
        throw std::runtime_error("Texture API test failed");
}
static void near(double a, double b) {
    check(std::abs(a - b) < 1e-10);
}
int main() {
    try {
        skp::Face face;
        face.plane[2] = 1;
        face.has_plane = true;
        skp::Material material;
        material.texture.reset(new skp::Texture);
        material.texture->width = 2;
        material.texture->height = 4;
        face.front_material = &material;
        SUFaceRef f = {&face};
        SUMaterialRef m = {&material};
        SUPoint3D p = {6, 8, 0}, uv = {};
        check(SKPFaceGetUVQ(f, false, m, 1, &p, &uv) == SU_ERROR_NONE);
        near(uv.x, 3);
        near(uv.y, 2);
        near(uv.z, 1);
        auto &map = face.front_mapping;
        map.flags = 1;
        map.matrix[0] = 2;
        map.matrix[4] = 4;
        map.matrix[6] = 1;
        map.matrix[7] = 2;
        p = {5, 10, 0};
        check(SKPFaceGetUVQ(f, false, m, 1, &p, &uv) == SU_ERROR_NONE);
        near(uv.x, 1);
        near(uv.y, .5);
        for (double &v : map.matrix)
            v = 0;
        check(SKPFaceGetUVQ(f, false, m, 1, &p, &uv) == SU_ERROR_PARTIAL_SUCCESS);
        near(uv.x, 2.5);
        near(uv.y, 2.5);
        near(uv.z, 1);
        map.matrix[0] = map.matrix[4] = map.matrix[8] = 1;
        map.matrix[2] = .1;
        p = {2, 4, 0};
        check(SKPFaceGetUVQ(f, false, m, 1, &p, &uv) == SU_ERROR_NONE);
        near(uv.x, 1);
        near(uv.y, 1);
        near(uv.z, .8);
        face.back_mapping.flags = 2;
        face.back_mapping.projection[1] = 1;
        p = {6, 8, 12};
        check(SKPFaceGetUVQ(f, true, m, 1, &p, &uv) == SU_ERROR_NONE);
        near(uv.x, -3);
        near(uv.y, 3);
        material.texture->image_data = {1, 2, 3};
        SUTextureRef texture = {material.texture.get()};
        size_t size = 0;
        check(SKPTextureGetDataSize(texture, &size) == SU_ERROR_NONE && size == 3);
        unsigned char bytes[3] = {9, 9, 9};
        check(SKPTextureGetData(texture, 2, bytes) == SU_ERROR_INSUFFICIENT_SIZE && bytes[0] == 9);
        check(SKPTextureGetData(texture, 3, bytes) == SU_ERROR_NONE && bytes[2] == 3);
        check(SKPTextureGetData(texture, 3, nullptr) == SU_ERROR_NULL_POINTER_OUTPUT);
        check(SKPFaceGetUVQ(f, false, m, 1, nullptr, &uv) == SU_ERROR_NULL_POINTER_INPUT);
        bool shift = false;
        check(SKPMaterialGetColorizeShift(m, &shift) == SU_ERROR_NONE && shift);
        skp::Group group;
        group.material = &material;
        SUMaterialRef out = {};
        check(SKPGroupGetMaterial(SUGroupRef{&group}, &out) == SU_ERROR_NONE &&
              out.ptr == &material);
        std::cout
            << "Default, transformed, projected, projective, singular UV and image API passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
