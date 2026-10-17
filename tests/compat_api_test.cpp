#include "slapi_compat.h"
#include "skp_model.hpp"
#include <cstdio>
#include <cmath>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (0)
namespace skp {
std::unique_ptr<Model> load_model_file(const char *) {
    throw LoadError(SU_ERROR_SERIALIZATION, "test stub");
}
} // namespace skp
int main() {
    try {
        SUModelRef m = {};
        CHECK(SUModelCreate(nullptr) == SU_ERROR_NULL_POINTER_OUTPUT);
        CHECK(SUModelCreate(&m) == 0);
        CHECK(SUModelCreate(&m) == SU_ERROR_OVERWRITE_VALID);
        SUEntitiesRef e = {};
        CHECK(SUModelGetEntities(m, &e) == 0);
        size_t n = 99;
        CHECK(SUEntitiesGetNumFaces(e, &n) == 0 && n == 0);
        auto model = static_cast<skp::Model *>(m.ptr);
        model->definitions.emplace_back(new skp::Definition);
        model->definitions.back()->name = "component";
        CHECK(SUModelGetNumComponentDefinitions(m, &n) == 0 && n == 1);
        SUComponentDefinitionRef definitions[2] = {{nullptr}, {reinterpret_cast<void *>(1)}};
        CHECK(SUModelGetComponentDefinitions(m, 1, definitions, &n) == 0 && n == 1 &&
              definitions[0].ptr == model->definitions[0].get() &&
              definitions[1].ptr == reinterpret_cast<void *>(1));
        CHECK(SUModelGetComponentDefinitions(m, 0, definitions, &n) == 0 && n == 0);
        CHECK(SUModelGetComponentDefinitions(m, 0, nullptr, &n) == SU_ERROR_NULL_POINTER_OUTPUT);
        model->metadata.materials.emplace_back(new skp::Material);
        auto mat = model->metadata.materials.back().get();
        mat->name = "[Brick]";
        mat->rgba[0] = 100;
        mat->rgba[1] = 120;
        mat->rgba[2] = 140;
        mat->rgba[3] = 255;
        mat->transparency = 0.25;
        SUMaterialRef mr = {};
        CHECK(SUModelGetNumMaterials(m, &n) == 0 && n == 1);
        CHECK(SUModelGetMaterials(m, 1, &mr, &n) == 0 && n == 1 && mr.ptr == mat);
        double opacity = 0;
        CHECK(SUMaterialGetOpacity(mr, &opacity) == 0 && opacity == 1);
        mat->use_opacity = true;
        CHECK(SUMaterialGetOpacity(mr, &opacity) == 0 && opacity == 0.75);
        SUColor color = {};
        CHECK(SUMaterialGetColor(mr, &color) == 0 && color.red == 100 && color.alpha == 191);
        SUTextureRef tr = {reinterpret_cast<void *>(1)};
        CHECK(SUMaterialGetTexture(mr, &tr) == SU_ERROR_NO_DATA &&
              tr.ptr == reinterpret_cast<void *>(1));
        mat->texture.reset(new skp::Texture);
        mat->texture->filename = "C:\\images\\brick.jpg";
        mat->texture->width = 15;
        mat->texture->height = 20;
        mat->texture->pixel_width = 256;
        mat->texture->pixel_height = 512;
        CHECK(SUMaterialGetTexture(mr, &tr) == 0);
        size_t tw = 0, th = 0;
        double ts = 0, tt = 0;
        CHECK(SUTextureGetDimensions(tr, &tw, &th, &ts, &tt) == 0 && tw == 256 && th == 512 &&
              std::fabs(ts - 1.0 / 15) < 1e-12 && tt == 0.05);
        SUStringRef label = {};
        CHECK(SUStringCreate(&label) == 0);
        CHECK(SUMaterialGetName(mr, &label) == 0);
        CHECK(*static_cast<std::string *>(label.ptr) == "Brick");
        CHECK(SUTextureGetFileName(tr, &label) == 0 &&
              *static_cast<std::string *>(label.ptr) == "brick.jpg");
        CHECK(SUStringRelease(&label) == 0);
        model->metadata.layers.emplace_back(new skp::Layer);
        model->metadata.layers.back()->hidden = true;
        SULayerRef lr = {};
        bool visible = true;
        CHECK(SUModelGetNumLayers(m, &n) == 0 && n == 1);
        CHECK(SUModelGetLayers(m, 1, &lr, &n) == 0 && n == 1);
        CHECK(SULayerGetVisibility(lr, &visible) == 0 && !visible);
        CHECK(SULayerGetMaterial(lr, &mr) == 0 && mr.ptr == &model->metadata.layers[0]->material);
        SUCameraRef cr = {};
        CHECK(SUModelGetCamera(m, &cr) == 0);
        double cv = 7;
        CHECK(SUCameraGetAspectRatio(cr, &cv) == SU_ERROR_NO_DATA && cv == 0);
        cv = 7;
        CHECK(SUCameraGetOrthographicFrustumHeight(cr, &cv) == SU_ERROR_NO_DATA && cv == 7);
        CHECK(SUCameraGetPerspectiveFrustumFOV(cr, &cv) == 0 && cv == 35);
        model->metadata.camera.perspective = false;
        model->metadata.camera.aspect_ratio = 1.5;
        CHECK(SUCameraGetAspectRatio(cr, &cv) == 0 && cv == 1.5);
        CHECK(SUCameraGetPerspectiveFrustumFOV(cr, &cv) == SU_ERROR_NO_DATA);
        SUPoint3D eye, target;
        SUVector3D up;
        CHECK(SUCameraGetOrientation(cr, &eye, &target, &up) == 0 && eye.z == 1 && target.z == 0 &&
              up.y == 1);
        CHECK(SUModelRelease(&m) == 0 && !m.ptr);
        CHECK(SUModelRelease(&m) == SU_ERROR_INVALID_INPUT);
        CHECK(SUModelCreateFromFile(&m, nullptr) == SU_ERROR_NULL_POINTER_INPUT);
        skp::Vertex v[8];
        double coords[8][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {1, 1}, {1, 2}, {2, 2}, {2, 1}};
        skp::Loop outer, inner;
        for (int i = 0; i < 8; ++i) {
            v[i].position.x = coords[i][0];
            v[i].position.y = coords[i][1];
            (i < 4 ? outer : inner).vertices.push_back(&v[i]);
        }
        skp::Face f;
        f.outer = &outer;
        f.inner.push_back(&inner);
        SUFaceRef fr = {&f};
        double area = 0;
        CHECK(SUFaceGetArea(fr, &area) == 0 && std::fabs(area - 15) < 1e-12);
        SUVector3D norm;
        CHECK(SUFaceGetNormal(fr, &norm) == 0 && norm.z == 1);
        SUPlane3D plane;
        CHECK(SUFaceGetPlane(fr, &plane) == 0 && plane.c == 1 && plane.d == 0);
        skp::Material face_material;
        SUMaterialRef fm = {};
        CHECK(SUFaceGetFrontMaterial(fr, &fm) == SU_ERROR_NO_DATA);
        f.front_material = &face_material;
        CHECK(SUFaceGetFrontMaterial(fr, &fm) == 0 && fm.ptr == &face_material);
        CHECK(SUFaceGetBackMaterial(fr, &fm) == SU_ERROR_NO_DATA && fm.ptr == &face_material);
        f.back_material = &face_material;
        CHECK(SUFaceGetBackMaterial(fr, &fm) == 0 && fm.ptr == &face_material);
        CHECK(SUFaceGetNumVertices(fr, &n) == 0 && n == 8);
        SUVertexRef vo[3];
        CHECK(SUFaceGetVertices(fr, 3, vo, &n) == 0 && n == 3 && vo[2].ptr == &v[2]);
        CHECK(SUFaceGetVertices(fr, 0, vo, &n) == 0 && n == 0);
        CHECK(SUFaceGetVertices(fr, 0, nullptr, &n) == SU_ERROR_NULL_POINTER_OUTPUT);
        SUVertexRef all[9] = {};
        all[8].ptr = reinterpret_cast<void *>(1);
        CHECK(SUFaceGetVertices(fr, 8, all, &n) == 0 && n == 8 && all[4].ptr == &v[4] &&
              all[7].ptr == &v[7] && all[8].ptr == reinterpret_cast<void *>(1));
        skp::Edge edges[3];
        edges[1].standalone = true;
        skp::Entities es;
        for (auto &edge : edges)
            es.edges.push_back(&edge);
        SUEntitiesRef er = {&es};
        SUEdgeRef eo[2] = {{nullptr}, {reinterpret_cast<void *>(1)}};
        CHECK(SUEntitiesGetNumEdges(er, true, &n) == 0 && n == 1);
        CHECK(SUEntitiesGetEdges(er, true, 1, eo, &n) == 0 && n == 1 && eo[0].ptr == &edges[1] &&
              eo[1].ptr == reinterpret_cast<void *>(1));
        CHECK(SUEntitiesGetEdges(er, false, 0, eo, &n) == 0 && n == 0);
        CHECK(SUEntitiesGetNumEdges(SUEntitiesRef{}, false, nullptr) ==
              SU_ERROR_NULL_POINTER_OUTPUT);
        CHECK(SUFaceGetNumVertices(SUFaceRef{}, nullptr) == SU_ERROR_INVALID_INPUT);
        f.has_plane = true;
        f.plane[2] = -1;
        f.plane[3] = 4;
        CHECK(SUFaceGetNormal(fr, &norm) == 0 && norm.z == -1);
        CHECK(SUFaceGetPlane(fr, &plane) == 0 && plane.c == -1 && plane.d == 4);
        skp::Face empty_face;
        CHECK(SUFaceGetNormal(SUFaceRef{&empty_face}, &norm) == SU_ERROR_NO_DATA);
        CHECK(SUFaceGetPlane(SUFaceRef{&empty_face}, &plane) == SU_ERROR_NO_DATA);
        SUStringRef s = {};
        CHECK(SUStringCreateFromUTF8(&s, "abc") == 0);
        char buf[5] = {};
        CHECK(SUStringGetUTF8(s, 2, buf, &n) == 0 && n == 2 && buf[0] == 'a' && buf[1] == 'b');
        CHECK(SUStringGetUTF8(s, 5, buf, &n) == 0 && n == 3 && buf[3] == 0 && buf[4] == 0);
        CHECK(SUStringRelease(&s) == 0);
        std::puts("compat API ownership, errors, geometry, holes, list truncation, strings passed");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
