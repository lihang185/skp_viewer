#include "skp_mesh_api.h"
#include <vector>
#include <iostream>
#include <stdexcept>
static void check(bool ok){if(!ok)throw std::runtime_error("Mesh API contract failure");}
int main(int argc,char**argv){try{
    check(argc==2);SUInitialize();SUModelRef model={};SUEntitiesRef entities={};
    check(SUModelCreateFromFile(&model,argv[1])==SU_ERROR_NONE);
    check(SUModelGetEntities(model,&entities)==SU_ERROR_NONE);
    size_t count=0;check(SUEntitiesGetNumFaces(entities,&count)==SU_ERROR_NONE&&count>0);
    std::vector<SUFaceRef> faces(count);check(SUEntitiesGetFaces(entities,count,faces.data(),&count)==SU_ERROR_NONE);
    SKPMeshRef mesh={};
    check(SKPFaceCreateMesh(SUFaceRef{},&mesh)==SU_ERROR_INVALID_INPUT);
    check(SKPFaceCreateMesh(faces[0],nullptr)==SU_ERROR_NULL_POINTER_OUTPUT);
    check(SKPFaceCreateMesh(faces[0],&mesh)==SU_ERROR_NONE);
    void* saved=mesh.ptr;check(SKPFaceCreateMesh(faces[0],&mesh)==SU_ERROR_INVALID_OUTPUT&&mesh.ptr==saved);
    // A mesh owns its coordinates; it remains usable after the model is released.
    check(SUModelRelease(&model)==SU_ERROR_NONE);
    size_t nv=0,ni=0;check(SKPMeshGetSizes(mesh,&nv,&ni)==SU_ERROR_NONE&&nv>=3&&ni>=3&&ni%3==0);
    std::vector<SUPoint3D> p(nv);std::vector<unsigned int> i(ni);
    check(SKPMeshGetData(mesh,nv-1,p.data(),ni,i.data())==SU_ERROR_INSUFFICIENT_SIZE);
    check(SKPMeshGetData(mesh,nv,p.data(),ni,i.data())==SU_ERROR_NONE);
    for(auto idx:i)check(idx<nv);
    check(SKPMeshRelease(&mesh)==SU_ERROR_NONE&&!mesh.ptr);
    check(SKPMeshRelease(&mesh)==SU_ERROR_INVALID_INPUT);
    SUTerminate();std::cout<<"Mesh API passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
