// Experimental, separate interface for MDL 44 geometry.
#ifndef ISTUDIORENDER44_H
#define ISTUDIORENDER44_H

#include "studio_vertex_view.h"
#include <cstdint>

#define STUDIO_RENDER_44_INTERFACE_VERSION "VStudioRender44_005"

class IMaterialSystem;
class IMaterial;

struct Studio44UploadVertex
{
    StudioVertexView::Vertex vertex;
    float tangent[4];
    unsigned char weightIndex[3], boneCount;
    int8_t boneId[3];
};

struct Studio44Strip
{
    uint32_t indexCount, indexOffset, vertexCount, vertexOffset, boneStateCount;
    uint16_t boneCount;
    unsigned char flags;
};

// Source data are borrowed only for the duration of CreateMesh.
class IStudio44MeshSource
{
public:
    virtual unsigned int VertexCount() const = 0;
    virtual unsigned int IndexCount() const = 0;
    virtual bool Vertex( unsigned int i, Studio44UploadVertex &out ) const = 0;
    virtual bool Index( unsigned int i, uint16_t &out ) const = 0;
    virtual unsigned int StripCount() const = 0;
    virtual bool Strip( unsigned int i, Studio44Strip &out ) const = 0;
    virtual bool BoneState( unsigned int strip, unsigned int i, int32_t &hardware, int32_t &bone ) const = 0;
};

class IStudioRender44
{
public:
    // The returned opaque handle owns one GPU mesh; release it before the
    // material system or StudioRender is shut down.
    virtual void *CreateMesh( IMaterialSystem *materials, IMaterial *material, const IStudio44MeshSource &source ) = 0;
    virtual void DestroyMesh( void *handle ) = 0;
    // transform points to a row-major 3x4 model-to-world matrix.
    virtual bool DrawMesh( void *handle, IMaterial *material, const float *transform, float alpha ) = 0;
    // boneMatrices contains boneCount row-major 3x4 model-space skin matrices.
    virtual bool DrawSkinnedMesh( void *handle, IMaterial *material, const float *transform,
                                  const float *boneMatrices, unsigned int boneCount, float alpha ) = 0;
};
#endif
