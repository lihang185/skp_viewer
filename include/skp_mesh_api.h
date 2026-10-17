#pragma once
#include "slapi_compat.h"
// Project extension, not an emulation of the original SUMeshHelper API.
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SKPMeshRef {
    void *ptr;
} SKPMeshRef;
SU_API SUResult SU_CALL SKPEdgeGetHidden(SUEdgeRef edge, bool *hidden);
SU_API SUResult SU_CALL SKPEdgeGetSoft(SUEdgeRef edge, bool *soft);
SU_API SUResult SU_CALL SKPTextureGetDataSize(SUTextureRef texture, size_t *size);
SU_API SUResult SU_CALL SKPMaterialGetColorizeShift(SUMaterialRef material, bool *shift);
SU_API SUResult SU_CALL SKPTextureGetData(SUTextureRef texture, size_t capacity,
                                          unsigned char *data);
SU_API SUResult SU_CALL SKPGroupGetMaterial(SUGroupRef group, SUMaterialRef *material);
SU_API SUResult SU_CALL SKPInstanceGetMaterial(SUComponentInstanceRef instance,
                                               SUMaterialRef *material);
// UVQ is homogeneous: sample (u/q,v/q); points are component-local, material is
// effective/inherited.
// PARTIAL_SUCCESS means singular UVs used default planar mapping; output remains usable.
SU_API SUResult SU_CALL SKPFaceGetUVQ(SUFaceRef face, bool back, SUMaterialRef material,
                                      size_t count, const SUPoint3D *points, SUPoint3D *uvq);
SU_API SUResult SU_CALL SKPFaceCreateMesh(SUFaceRef face, SKPMeshRef *mesh);
SU_API SUResult SU_CALL SKPMeshRelease(SKPMeshRef *mesh);
SU_API SUResult SU_CALL SKPMeshGetSizes(SKPMeshRef mesh, size_t *vertices, size_t *indices);
SU_API SUResult SU_CALL SKPMeshGetData(SKPMeshRef mesh, size_t vertex_capacity, SUPoint3D *vertices,
                                       size_t index_capacity, unsigned int *indices);
#ifdef __cplusplus
}
#endif
