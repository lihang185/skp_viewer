#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef _WIN32
#define SU_CALL __cdecl
#ifdef SLAPI_COMPAT_EXPORTS
#define SU_API __declspec(dllexport)
#else
#define SU_API
#endif
#else
#define SU_CALL
#define SU_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef enum SUResult {
    SU_ERROR_NONE = 0,
    SU_ERROR_NULL_POINTER_INPUT = 1,
    SU_ERROR_INVALID_INPUT = 2,
    SU_ERROR_NULL_POINTER_OUTPUT = 3,
    SU_ERROR_INVALID_OUTPUT = 4,
    SU_ERROR_OVERWRITE_VALID = 5,
    SU_ERROR_GENERIC = 6,
    SU_ERROR_SERIALIZATION = 7,
    SU_ERROR_OUT_OF_RANGE = 8,
    SU_ERROR_NO_DATA = 9,
    SU_ERROR_INSUFFICIENT_SIZE = 10,
    SU_ERROR_UNKNOWN_EXCEPTION = 11,
    SU_ERROR_MODEL_INVALID = 12,
    SU_ERROR_MODEL_VERSION = 13,
    SU_ERROR_LAYER_LOCKED = 14,
    SU_ERROR_DUPLICATE = 15,
    SU_ERROR_PARTIAL_SUCCESS = 16,
    SU_ERROR_UNSUPPORTED = 17
} SUResult;
#define SU_REF(name)                                                                               \
    typedef struct name {                                                                          \
        void *ptr;                                                                                 \
    } name
SU_REF(SUModelRef);
SU_REF(SUEntitiesRef);
SU_REF(SUFaceRef);
SU_REF(SULoopRef);
SU_REF(SUEdgeRef);
SU_REF(SUVertexRef);
SU_REF(SUGroupRef);
SU_REF(SUComponentInstanceRef);
SU_REF(SUComponentDefinitionRef);
SU_REF(SUStringRef);
SU_REF(SUMaterialRef);
SU_REF(SULayerRef);
SU_REF(SUCameraRef);
SU_REF(SUTextureRef);
#undef SU_REF
typedef struct SUPoint3D {
    double x, y, z;
} SUPoint3D;
typedef struct SUVector3D {
    double x, y, z;
} SUVector3D;
typedef struct SUPlane3D {
    double a, b, c, d;
} SUPlane3D;
typedef struct SUTransformation {
    double values[16];
} SUTransformation;
typedef struct SUColor {
    unsigned char red, green, blue, alpha;
} SUColor;
typedef enum SUMaterialType {
    SUMaterialType_Colored = 0,
    SUMaterialType_Textured = 1,
    SUMaterialType_ColorizedTexture = 2
} SUMaterialType;
SU_API void SU_CALL SUInitialize(void);
SU_API void SU_CALL SUTerminate(void);
SU_API void SU_CALL SUGetAPIVersion(size_t *, size_t *);
SU_API SUResult SU_CALL SUModelCreate(SUModelRef *);
SU_API SUResult SU_CALL SUModelCreateFromFile(SUModelRef *, const char *);
SU_API SUResult SU_CALL SUModelRelease(SUModelRef *);
SU_API SUResult SU_CALL SUModelGetEntities(SUModelRef, SUEntitiesRef *);
SU_API SUResult SU_CALL SUModelGetName(SUModelRef, SUStringRef *);
SU_API SUResult SU_CALL SUModelGetNumComponentDefinitions(SUModelRef, size_t *);
SU_API SUResult SU_CALL SUModelGetComponentDefinitions(SUModelRef, size_t,
                                                       SUComponentDefinitionRef *, size_t *);
SU_API SUResult SU_CALL SUModelGetNumMaterials(SUModelRef, size_t *);
SU_API SUResult SU_CALL SUModelGetMaterials(SUModelRef, size_t, SUMaterialRef *, size_t *);
SU_API SUResult SU_CALL SUModelGetNumLayers(SUModelRef, size_t *);
SU_API SUResult SU_CALL SUModelGetLayers(SUModelRef, size_t, SULayerRef *, size_t *);
SU_API SUResult SU_CALL SUModelGetCamera(SUModelRef, SUCameraRef *);
SU_API SUResult SU_CALL SUMaterialGetName(SUMaterialRef, SUStringRef *);
SU_API SUResult SU_CALL SUMaterialGetColor(SUMaterialRef, SUColor *);
SU_API SUResult SU_CALL SUMaterialGetOpacity(SUMaterialRef, double *);
SU_API SUResult SU_CALL SUMaterialGetUseOpacity(SUMaterialRef, bool *);
SU_API SUResult SU_CALL SUMaterialGetType(SUMaterialRef, SUMaterialType *);
SU_API SUResult SU_CALL SUMaterialGetTexture(SUMaterialRef, SUTextureRef *);
SU_API SUResult SU_CALL SULayerGetName(SULayerRef, SUStringRef *);
SU_API SUResult SU_CALL SULayerGetMaterial(SULayerRef, SUMaterialRef *);
SU_API SUResult SU_CALL SULayerGetVisibility(SULayerRef, bool *);
SU_API SUResult SU_CALL SUCameraGetOrientation(SUCameraRef, SUPoint3D *, SUPoint3D *, SUVector3D *);
SU_API SUResult SU_CALL SUCameraGetPerspective(SUCameraRef, bool *);
SU_API SUResult SU_CALL SUCameraGetPerspectiveFrustumFOV(SUCameraRef, double *);
SU_API SUResult SU_CALL SUCameraGetOrthographicFrustumHeight(SUCameraRef, double *);
SU_API SUResult SU_CALL SUCameraGetAspectRatio(SUCameraRef, double *);
SU_API SUResult SU_CALL SUTextureGetFileName(SUTextureRef, SUStringRef *);
SU_API SUResult SU_CALL SUTextureGetDimensions(SUTextureRef, size_t *, size_t *, double *,
                                               double *);
#define SU_LIST(prefix, ref, type, plural)                                                         \
    SU_API SUResult SU_CALL prefix##GetNum##plural(ref, size_t *);                                 \
    SU_API SUResult SU_CALL prefix##Get##plural(ref, size_t, type *, size_t *)
SU_LIST(SUEntities, SUEntitiesRef, SUFaceRef, Faces);
SU_LIST(SUEntities, SUEntitiesRef, SUGroupRef, Groups);
SU_LIST(SUEntities, SUEntitiesRef, SUComponentInstanceRef, Instances);
SU_LIST(SUFace, SUFaceRef, SUVertexRef, Vertices);
SU_LIST(SUFace, SUFaceRef, SUEdgeRef, Edges);
SU_LIST(SUFace, SUFaceRef, SULoopRef, InnerLoops);
SU_LIST(SULoop, SULoopRef, SUVertexRef, Vertices);
#undef SU_LIST
SU_API SUResult SU_CALL SUEntitiesGetNumEdges(SUEntitiesRef, bool, size_t *);
SU_API SUResult SU_CALL SUEntitiesGetEdges(SUEntitiesRef, bool, size_t, SUEdgeRef *, size_t *);
SU_API SUResult SU_CALL SULoopGetEdges(SULoopRef, size_t, SUEdgeRef *, size_t *);
SU_API SUResult SU_CALL SUFaceGetOuterLoop(SUFaceRef, SULoopRef *);
SU_API SUResult SU_CALL SUFaceGetNormal(SUFaceRef, SUVector3D *);
SU_API SUResult SU_CALL SUFaceGetPlane(SUFaceRef, SUPlane3D *);
SU_API SUResult SU_CALL SUFaceGetArea(SUFaceRef, double *);
SU_API SUResult SU_CALL SUFaceGetFrontMaterial(SUFaceRef, SUMaterialRef *);
SU_API SUResult SU_CALL SUFaceGetBackMaterial(SUFaceRef, SUMaterialRef *);
SU_API SUResult SU_CALL SUEdgeGetStartVertex(SUEdgeRef, SUVertexRef *);
SU_API SUResult SU_CALL SUEdgeGetEndVertex(SUEdgeRef, SUVertexRef *);
SU_API SUResult SU_CALL SUVertexGetPosition(SUVertexRef, SUPoint3D *);
SU_API SUResult SU_CALL SUGroupGetEntities(SUGroupRef, SUEntitiesRef *);
SU_API SUResult SU_CALL SUGroupGetTransform(SUGroupRef, SUTransformation *);
SU_API SUResult SU_CALL SUGroupGetName(SUGroupRef, SUStringRef *);
SU_API SUResult SU_CALL SUComponentInstanceGetDefinition(SUComponentInstanceRef,
                                                         SUComponentDefinitionRef *);
SU_API SUResult SU_CALL SUComponentInstanceGetTransform(SUComponentInstanceRef, SUTransformation *);
SU_API SUResult SU_CALL SUComponentInstanceGetName(SUComponentInstanceRef, SUStringRef *);
SU_API SUResult SU_CALL SUComponentDefinitionGetEntities(SUComponentDefinitionRef, SUEntitiesRef *);
SU_API SUResult SU_CALL SUComponentDefinitionGetName(SUComponentDefinitionRef, SUStringRef *);
SU_API SUResult SU_CALL SUStringCreate(SUStringRef *);
SU_API SUResult SU_CALL SUStringCreateFromUTF8(SUStringRef *, const char *);
SU_API SUResult SU_CALL SUStringRelease(SUStringRef *);
SU_API SUResult SU_CALL SUStringSetUTF8(SUStringRef, const char *);
SU_API SUResult SU_CALL SUStringGetUTF8Length(SUStringRef, size_t *);
SU_API SUResult SU_CALL SUStringGetUTF8(SUStringRef, size_t, char *, size_t *);
SU_API SUResult SU_CALL SUModelSaveToFile(SUModelRef, const char *);
#ifdef __cplusplus
}
#endif
