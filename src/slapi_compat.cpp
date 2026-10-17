#include "slapi_compat.h"
#include "skp_model.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
namespace {
template <class T> T *obj(void *p) {
    return static_cast<T *>(p);
}
template <class T, class R>
SUResult list(void *input, const std::vector<T *> &v, size_t cap, R *out, size_t *n) {
    if (!input)
        return SU_ERROR_INVALID_INPUT;
    if (!out || !n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = std::min(cap, v.size());
    for (size_t i = 0; i < *n; ++i)
        out[i].ptr = v[i];
    return SU_ERROR_NONE;
}
template <class T> SUResult count(void *input, const std::vector<T *> &v, size_t *n) {
    if (!input)
        return SU_ERROR_INVALID_INPUT;
    if (!n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = v.size();
    return SU_ERROR_NONE;
}
template <class T, class R> SUResult ref(void *input, T *p, R *out) {
    if (!input)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    out->ptr = p;
    return SU_ERROR_NONE;
}
SUResult name(void *input, const std::string &text, SUStringRef *out) {
    if (!input)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    if (!out->ptr)
        return SU_ERROR_INVALID_OUTPUT;
    try {
        *obj<std::string>(out->ptr) = text;
        return SU_ERROR_NONE;
    } catch (...) {
        return SU_ERROR_UNKNOWN_EXCEPTION;
    }
}
// Traverse directly: getters must not allocate temporary lists or throw across C ABI.
template <class T> size_t face_count(skp::Face *f, std::vector<T *> skp::Loop::*member) {
    size_t n = f->outer ? (f->outer->*member).size() : 0;
    for (auto l : f->inner)
        if (l)
            n += (l->*member).size();
    return n;
}
template <class T, class R>
SUResult face_list(skp::Face *f, std::vector<T *> skp::Loop::*member, size_t cap, R *out,
                   size_t *n) {
    if (!f)
        return SU_ERROR_INVALID_INPUT;
    if (!out || !n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = 0;
    auto append = [&](skp::Loop *l) {
        if (l)
            for (auto p : l->*member) {
                if (*n == cap)
                    break;
                out[(*n)++].ptr = p;
            }
    };
    append(f->outer);
    for (auto l : f->inner) {
        if (*n == cap)
            break;
        append(l);
    }
    return SU_ERROR_NONE;
}
skp::Point cross_loop(skp::Loop *l) {
    skp::Point n;
    if (!l)
        return n;
    for (size_t i = 0; i < l->vertices.size(); ++i) {
        auto a = l->vertices[i]->position, b = l->vertices[(i + 1) % l->vertices.size()]->position;
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}
double magnitude(skp::Point n) {
    return std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
}
template <class T, class R>
SUResult owned_list(const std::vector<std::unique_ptr<T>> &v, size_t cap, R *out, size_t *n) {
    if (!out || !n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = std::min(cap, v.size());
    for (size_t i = 0; i < *n; ++i)
        out[i].ptr = v[i].get();
    return SU_ERROR_NONE;
}
} // namespace
extern "C" {
void SU_CALL SUInitialize() {}
void SU_CALL SUTerminate() {}
void SU_CALL SUGetAPIVersion(size_t *a, size_t *b) {
    if (a)
        *a = 3;
    if (b)
        *b = 0;
}
SUResult SU_CALL SUModelCreate(SUModelRef *m) {
    if (!m)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    if (m->ptr)
        return SU_ERROR_OVERWRITE_VALID;
    try {
        m->ptr = new skp::Model;
        return SU_ERROR_NONE;
    } catch (...) {
        return SU_ERROR_UNKNOWN_EXCEPTION;
    }
}
SUResult SU_CALL SUModelCreateFromFile(SUModelRef *m, const char *path) {
    if (!m)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    if (m->ptr)
        return SU_ERROR_OVERWRITE_VALID;
    if (!path)
        return SU_ERROR_NULL_POINTER_INPUT;
    try {
        auto p = skp::load_model_file(path);
        if (!p)
            return SU_ERROR_SERIALIZATION;
        m->ptr = p.release();
        return SU_ERROR_NONE;
    } catch (const skp::LoadError &e) {
        return static_cast<SUResult>(e.result);
    } catch (...) {
        return SU_ERROR_SERIALIZATION;
    }
}
SUResult SU_CALL SUModelRelease(SUModelRef *m) {
    if (!m)
        return SU_ERROR_NULL_POINTER_INPUT;
    if (!m->ptr)
        return SU_ERROR_INVALID_INPUT;
    delete obj<skp::Model>(m->ptr);
    m->ptr = nullptr;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUModelGetEntities(SUModelRef m, SUEntitiesRef *out) {
    return ref(m.ptr, m.ptr ? &obj<skp::Model>(m.ptr)->entities : nullptr, out);
}
SUResult SU_CALL SUModelGetName(SUModelRef m, SUStringRef *out) {
    if (!m.ptr)
        return SU_ERROR_INVALID_INPUT;
    return name(m.ptr, obj<skp::Model>(m.ptr)->name, out);
}
SUResult SU_CALL SUModelGetNumComponentDefinitions(SUModelRef m, size_t *n) {
    if (!m.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = obj<skp::Model>(m.ptr)->definitions.size();
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUModelGetComponentDefinitions(SUModelRef m, size_t cap,
                                                SUComponentDefinitionRef *out, size_t *n) {
    if (!m.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out || !n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto &v = obj<skp::Model>(m.ptr)->definitions;
    *n = std::min(cap, v.size());
    for (size_t i = 0; i < *n; ++i)
        out[i].ptr = v[i].get();
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUModelGetNumMaterials(SUModelRef m, size_t *n) {
    if (!m.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = obj<skp::Model>(m.ptr)->metadata.materials.size();
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUModelGetMaterials(SUModelRef m, size_t cap, SUMaterialRef *out, size_t *n) {
    if (!m.ptr)
        return SU_ERROR_INVALID_INPUT;
    return owned_list(obj<skp::Model>(m.ptr)->metadata.materials, cap, out, n);
}
SUResult SU_CALL SUModelGetNumLayers(SUModelRef m, size_t *n) {
    if (!m.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = obj<skp::Model>(m.ptr)->metadata.layers.size();
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUModelGetLayers(SUModelRef m, size_t cap, SULayerRef *out, size_t *n) {
    if (!m.ptr)
        return SU_ERROR_INVALID_INPUT;
    return owned_list(obj<skp::Model>(m.ptr)->metadata.layers, cap, out, n);
}
SUResult SU_CALL SUModelGetCamera(SUModelRef m, SUCameraRef *out) {
    return ref(m.ptr, m.ptr ? &obj<skp::Model>(m.ptr)->metadata.camera : nullptr, out);
}
SUResult SU_CALL SUMaterialGetName(SUMaterialRef r, SUStringRef *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    if (!out->ptr)
        return SU_ERROR_INVALID_OUTPUT;
    try {
        const auto &s = obj<skp::Material>(r.ptr)->name;
        *obj<std::string>(out->ptr) =
            s.size() >= 2 && s.front() == '[' && s.back() == ']' ? s.substr(1, s.size() - 2) : s;
        return SU_ERROR_NONE;
    } catch (...) {
        return SU_ERROR_UNKNOWN_EXCEPTION;
    }
}
SUResult SU_CALL SUMaterialGetColor(SUMaterialRef r, SUColor *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto m = obj<skp::Material>(r.ptr);
    if (!(m->rgba[0] | m->rgba[1] | m->rgba[2] | m->rgba[3]))
        return SU_ERROR_NO_DATA;
    out->red = m->rgba[0];
    out->green = m->rgba[1];
    out->blue = m->rgba[2];
    out->alpha = m->rgba[3];
    if (m->use_opacity)
        out->alpha = static_cast<unsigned char>(std::floor((1 - m->transparency) * 255 + 0.5));
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUMaterialGetOpacity(SUMaterialRef r, double *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto m = obj<skp::Material>(r.ptr);
    *out = m->use_opacity ? 1 - m->transparency : 1;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUMaterialGetUseOpacity(SUMaterialRef r, bool *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *out = obj<skp::Material>(r.ptr)->use_opacity;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUMaterialGetType(SUMaterialRef r, SUMaterialType *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *out = static_cast<SUMaterialType>(obj<skp::Material>(r.ptr)->type);
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUMaterialGetTexture(SUMaterialRef r, SUTextureRef *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto t = obj<skp::Material>(r.ptr)->texture.get();
    if (!t)
        return SU_ERROR_NO_DATA;
    out->ptr = t;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SULayerGetName(SULayerRef r, SUStringRef *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    return name(r.ptr, obj<skp::Layer>(r.ptr)->name, out);
}
SUResult SU_CALL SULayerGetMaterial(SULayerRef r, SUMaterialRef *out) {
    return ref(r.ptr, r.ptr ? &obj<skp::Layer>(r.ptr)->material : nullptr, out);
}
SUResult SU_CALL SULayerGetVisibility(SULayerRef r, bool *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *out = !obj<skp::Layer>(r.ptr)->hidden;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUCameraGetOrientation(SUCameraRef r, SUPoint3D *eye, SUPoint3D *target,
                                        SUVector3D *up) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!eye || !target || !up)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto c = obj<skp::Camera>(r.ptr);
    eye->x = c->eye[0];
    eye->y = c->eye[1];
    eye->z = c->eye[2];
    target->x = c->target[0];
    target->y = c->target[1];
    target->z = c->target[2];
    up->x = c->up[0];
    up->y = c->up[1];
    up->z = c->up[2];
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUCameraGetPerspective(SUCameraRef r, bool *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *out = obj<skp::Camera>(r.ptr)->perspective;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUCameraGetPerspectiveFrustumFOV(SUCameraRef r, double *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto c = obj<skp::Camera>(r.ptr);
    if (!c->perspective)
        return SU_ERROR_NO_DATA;
    *out = c->fov;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUCameraGetOrthographicFrustumHeight(SUCameraRef r, double *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto c = obj<skp::Camera>(r.ptr);
    if (c->perspective)
        return SU_ERROR_NO_DATA;
    *out = c->orthographic_height;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUCameraGetAspectRatio(SUCameraRef r, double *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    double a = obj<skp::Camera>(r.ptr)->aspect_ratio;
    *out = a >= 0.00001 ? a : 0;
    return a >= 0.00001 ? SU_ERROR_NONE : SU_ERROR_NO_DATA;
}
SUResult SU_CALL SUTextureGetDimensions(SUTextureRef r, size_t *width, size_t *height, double *s,
                                        double *t) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!width || !height || !s || !t)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto tex = obj<skp::Texture>(r.ptr);
    if (!tex->pixel_width || !tex->pixel_height)
        return SU_ERROR_NO_DATA;
    *width = tex->pixel_width;
    *height = tex->pixel_height;
    *s = std::fabs(1 / tex->width);
    *t = std::fabs(1 / tex->height);
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUTextureGetFileName(SUTextureRef r, SUStringRef *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    if (!out->ptr)
        return SU_ERROR_INVALID_OUTPUT;
    try {
        const auto &path = obj<skp::Texture>(r.ptr)->filename;
        size_t slash = path.find_last_of("/\\");
        return name(r.ptr, slash == std::string::npos ? path : path.substr(slash + 1), out);
    } catch (...) {
        return SU_ERROR_UNKNOWN_EXCEPTION;
    }
}
#define LIST(P, R, T, C, M, S)                                                                     \
    SUResult SU_CALL P##GetNum##S(R r, size_t *n) {                                                \
        if (!r.ptr)                                                                                \
            return SU_ERROR_INVALID_INPUT;                                                         \
        return count(r.ptr, obj<C>(r.ptr)->M, n);                                                  \
    }                                                                                              \
    SUResult SU_CALL P##Get##S(R r, size_t cap, T *out, size_t *n) {                               \
        if (!r.ptr)                                                                                \
            return SU_ERROR_INVALID_INPUT;                                                         \
        return list(r.ptr, obj<C>(r.ptr)->M, cap, out, n);                                         \
    }
LIST(SUEntities, SUEntitiesRef, SUFaceRef, skp::Entities, faces, Faces)
LIST(SUEntities, SUEntitiesRef, SUGroupRef, skp::Entities, groups, Groups)
LIST(SUEntities, SUEntitiesRef, SUComponentInstanceRef, skp::Entities, instances, Instances)
LIST(SUFace, SUFaceRef, SULoopRef, skp::Face, inner, InnerLoops)
LIST(SULoop, SULoopRef, SUVertexRef, skp::Loop, vertices, Vertices)
#undef LIST
SUResult SU_CALL SUEntitiesGetNumEdges(SUEntitiesRef r, bool alone, size_t *n) {
    if (!n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    *n = 0;
    for (auto e : obj<skp::Entities>(r.ptr)->edges)
        if (!alone || e->standalone)
            ++*n;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUEntitiesGetEdges(SUEntitiesRef r, bool alone, size_t cap, SUEdgeRef *out,
                                    size_t *n) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out || !n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = 0;
    for (auto e : obj<skp::Entities>(r.ptr)->edges) {
        if (*n == cap)
            break;
        if (!alone || e->standalone)
            out[(*n)++].ptr = e;
    }
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUFaceGetNumVertices(SUFaceRef r, size_t *n) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = face_count(obj<skp::Face>(r.ptr), &skp::Loop::vertices);
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUFaceGetVertices(SUFaceRef r, size_t cap, SUVertexRef *o, size_t *n) {
    return face_list(obj<skp::Face>(r.ptr), &skp::Loop::vertices, cap, o, n);
}
SUResult SU_CALL SUFaceGetNumEdges(SUFaceRef r, size_t *n) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = face_count(obj<skp::Face>(r.ptr), &skp::Loop::edges);
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUFaceGetEdges(SUFaceRef r, size_t cap, SUEdgeRef *o, size_t *n) {
    return face_list(obj<skp::Face>(r.ptr), &skp::Loop::edges, cap, o, n);
}
SUResult SU_CALL SULoopGetEdges(SULoopRef r, size_t cap, SUEdgeRef *o, size_t *n) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    return list(r.ptr, obj<skp::Loop>(r.ptr)->edges, cap, o, n);
}
SUResult SU_CALL SUFaceGetOuterLoop(SUFaceRef r, SULoopRef *o) {
    return ref(r.ptr, r.ptr ? obj<skp::Face>(r.ptr)->outer : nullptr, o);
}
SUResult SU_CALL SUFaceGetFrontMaterial(SUFaceRef r, SUMaterialRef *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto m = obj<skp::Face>(r.ptr)->front_material;
    if (!m)
        return SU_ERROR_NO_DATA;
    out->ptr = m;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUFaceGetBackMaterial(SUFaceRef r, SUMaterialRef *out) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto m = obj<skp::Face>(r.ptr)->back_material;
    if (!m)
        return SU_ERROR_NO_DATA;
    out->ptr = m;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUFaceGetNormal(SUFaceRef r, SUVector3D *o) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!o)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto f = obj<skp::Face>(r.ptr);
    auto n = cross_loop(f->outer);
    if (f->has_plane) {
        n.x = f->plane[0];
        n.y = f->plane[1];
        n.z = f->plane[2];
    }
    double m = magnitude(n);
    if (m == 0)
        return SU_ERROR_NO_DATA;
    o->x = n.x / m;
    o->y = n.y / m;
    o->z = n.z / m;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUFaceGetPlane(SUFaceRef r, SUPlane3D *o) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!o)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto f = obj<skp::Face>(r.ptr);
    if (f->has_plane) {
        o->a = f->plane[0];
        o->b = f->plane[1];
        o->c = f->plane[2];
        o->d = f->plane[3];
        return SU_ERROR_NONE;
    }
    SUVector3D n;
    auto result = SUFaceGetNormal(r, &n);
    if (result)
        return result;
    auto p = obj<skp::Face>(r.ptr)->outer->vertices[0]->position;
    o->a = n.x;
    o->b = n.y;
    o->c = n.z;
    o->d = -(n.x * p.x + n.y * p.y + n.z * p.z);
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUFaceGetArea(SUFaceRef r, double *o) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!o)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto f = obj<skp::Face>(r.ptr);
    double a = magnitude(cross_loop(f->outer));
    for (auto l : f->inner)
        a -= magnitude(cross_loop(l));
    *o = a / 2;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUEdgeGetStartVertex(SUEdgeRef r, SUVertexRef *o) {
    return ref(r.ptr, r.ptr ? obj<skp::Edge>(r.ptr)->start : nullptr, o);
}
SUResult SU_CALL SUEdgeGetEndVertex(SUEdgeRef r, SUVertexRef *o) {
    return ref(r.ptr, r.ptr ? obj<skp::Edge>(r.ptr)->end : nullptr, o);
}
SUResult SU_CALL SUVertexGetPosition(SUVertexRef r, SUPoint3D *o) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!o)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto p = obj<skp::Vertex>(r.ptr)->position;
    o->x = p.x;
    o->y = p.y;
    o->z = p.z;
    return SU_ERROR_NONE;
}
#define ENT(P, R, C)                                                                               \
    SUResult SU_CALL P##GetEntities(R r, SUEntitiesRef *o) {                                       \
        return ref(r.ptr, r.ptr ? &obj<C>(r.ptr)->entities : nullptr, o);                          \
    }
ENT(SUGroup, SUGroupRef, skp::Group)
ENT(SUComponentDefinition, SUComponentDefinitionRef, skp::Definition)
#undef ENT
#define TRANS(P, R, C)                                                                             \
    SUResult SU_CALL P##GetTransform(R r, SUTransformation *o) {                                   \
        if (!r.ptr)                                                                                \
            return SU_ERROR_INVALID_INPUT;                                                         \
        if (!o)                                                                                    \
            return SU_ERROR_NULL_POINTER_OUTPUT;                                                   \
        std::copy(obj<C>(r.ptr)->transform.values, obj<C>(r.ptr)->transform.values + 16,           \
                  o->values);                                                                      \
        return SU_ERROR_NONE;                                                                      \
    }
TRANS(SUGroup, SUGroupRef, skp::Group)
TRANS(SUComponentInstance, SUComponentInstanceRef, skp::Instance)
#undef TRANS
#define NAME(P, R, C)                                                                              \
    SUResult SU_CALL P##GetName(R r, SUStringRef *o) {                                             \
        if (!r.ptr)                                                                                \
            return SU_ERROR_INVALID_INPUT;                                                         \
        return name(r.ptr, obj<C>(r.ptr)->name, o);                                                \
    }
NAME(SUGroup, SUGroupRef, skp::Group)
NAME(SUComponentInstance, SUComponentInstanceRef, skp::Instance)
NAME(SUComponentDefinition, SUComponentDefinitionRef, skp::Definition)
#undef NAME
SUResult SU_CALL SUComponentInstanceGetDefinition(SUComponentInstanceRef r,
                                                  SUComponentDefinitionRef *o) {
    return ref(r.ptr, r.ptr ? obj<skp::Instance>(r.ptr)->definition : nullptr, o);
}
SUResult SU_CALL SUStringCreate(SUStringRef *r) {
    return SUStringCreateFromUTF8(r, "");
}
SUResult SU_CALL SUStringCreateFromUTF8(SUStringRef *r, const char *s) {
    if (!s)
        return SU_ERROR_NULL_POINTER_INPUT;
    if (!r)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    if (r->ptr)
        return SU_ERROR_OVERWRITE_VALID;
    try {
        r->ptr = new std::string(s);
        return SU_ERROR_NONE;
    } catch (...) {
        return SU_ERROR_UNKNOWN_EXCEPTION;
    }
}
SUResult SU_CALL SUStringRelease(SUStringRef *r) {
    if (!r)
        return SU_ERROR_NULL_POINTER_INPUT;
    if (!r->ptr)
        return SU_ERROR_INVALID_INPUT;
    delete obj<std::string>(r->ptr);
    r->ptr = nullptr;
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUStringSetUTF8(SUStringRef r, const char *s) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!s)
        return SU_ERROR_NULL_POINTER_INPUT;
    try {
        *obj<std::string>(r.ptr) = s;
        return SU_ERROR_NONE;
    } catch (...) {
        return SU_ERROR_UNKNOWN_EXCEPTION;
    }
}
SUResult SU_CALL SUStringGetUTF8Length(SUStringRef r, size_t *n) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    *n = obj<std::string>(r.ptr)->size();
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUStringGetUTF8(SUStringRef r, size_t cap, char *out, size_t *n) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!out || !n)
        return SU_ERROR_NULL_POINTER_OUTPUT;
    auto &s = *obj<std::string>(r.ptr);
    *n = std::min(cap, s.size());
    std::memcpy(out, s.data(), *n);
    std::memset(out + *n, 0, cap - *n);
    return SU_ERROR_NONE;
}
SUResult SU_CALL SUModelSaveToFile(SUModelRef r, const char *path) {
    if (!r.ptr)
        return SU_ERROR_INVALID_INPUT;
    if (!path)
        return SU_ERROR_NULL_POINTER_INPUT;
    return SU_ERROR_UNSUPPORTED;
}
}
