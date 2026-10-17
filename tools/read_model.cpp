// Minimal client of the replacement's public C ABI; no original SDK needed.
#include "slapi_compat.h"
#include <cstdio>
int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: read_model <model.skp>\n");
        return 2;
    }
    SUInitialize();
    SUModelRef model = {};
    SUResult result = SUModelCreateFromFile(&model, argv[1]);
    if (result) {
        std::fprintf(stderr, "Unable to read archive (SUResult=%d)\n", int(result));
        SUTerminate();
        return 1;
    }
    SUEntitiesRef entities = {};
    size_t faces = 0, edges = 0, groups = 0, instances = 0, materials = 0, layers = 0;
    SUModelGetEntities(model, &entities);
    SUEntitiesGetNumFaces(entities, &faces);
    SUEntitiesGetNumEdges(entities, false, &edges);
    SUEntitiesGetNumGroups(entities, &groups);
    SUEntitiesGetNumInstances(entities, &instances);
    SUModelGetNumMaterials(model, &materials);
    SUModelGetNumLayers(model, &layers);
    std::printf(
        "Faces: %zu\nEdges: %zu\nGroups: %zu\nInstances: %zu\nMaterials: %zu\nLayers: %zu\n", faces,
        edges, groups, instances, materials, layers);
    SUModelRelease(&model);
    SUTerminate();
    return 0;
}
