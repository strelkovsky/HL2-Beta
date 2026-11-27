// Matrix conversion for the experimental StudioRender 44 interface.
#ifndef STUDIO44_TRANSFORM_H
#define STUDIO44_TRANSFORM_H
#include <cstring>
#include <cmath>
#include "studio_vertex_view.h"
namespace Studio44Transform
{
    // Inputs are row-major 3x4; material-system matrices are column-major 4x4.
    inline void ToMaterial(const float *world, const float *bone, float (&matrix)[4][4])
    {
        std::memset(matrix,0,sizeof(matrix));
        for(int i=0;i<3;++i)
        {
            for(int j=0;j<3;++j)
            {
                float value=bone ? 0.0f : world[i*4+j];
                if(bone) for(int k=0;k<3;++k) value+=world[i*4+k]*bone[k*4+j];
                matrix[j][i]=value;
            }
            float value=world[i*4+3];
            if(bone) for(int k=0;k<3;++k) value+=world[i*4+k]*bone[k*4+3];
            matrix[3][i]=value;
        }
        matrix[3][3]=1.0f;
    }
    // Source bone IDs refer to the skeleton, not hardware palette slots.
    inline bool SkinVertex(const StudioVertexView::Vertex &src, const float tangent[4],
                           const float *bones, unsigned int boneCount,
                           float pos[3], float normal[3], float transformedTangent[4])
    {
        if(!bones || !boneCount || !src.boneCount || src.boneCount>3) return false;
        float p[3]={0,0,0}, n[3]={0,0,0}, t[3]={0,0,0}, total=0;
        for(unsigned int j=0;j<src.boneCount;++j)
        {
            if(src.bones[j]<0 || static_cast<unsigned int>(src.bones[j])>=boneCount ||
               !std::isfinite(src.weights[j]) || src.weights[j]<0) return false;
            const float *m=bones+static_cast<unsigned int>(src.bones[j])*12, weight=src.weights[j]; total+=weight;
            for(int r=0;r<3;++r)
            {
                p[r]+=weight*(m[r*4]*src.position[0]+m[r*4+1]*src.position[1]+m[r*4+2]*src.position[2]+m[r*4+3]);
                n[r]+=weight*(m[r*4]*src.normal[0]+m[r*4+1]*src.normal[1]+m[r*4+2]*src.normal[2]);
                t[r]+=weight*(m[r*4]*tangent[0]+m[r*4+1]*tangent[1]+m[r*4+2]*tangent[2]);
            }
        }
        if(!std::isfinite(total) || total<=0) return false;
        for(int r=0;r<3;++r)
        {
            pos[r]=p[r]/total; normal[r]=n[r]/total; transformedTangent[r]=t[r]/total;
            if(!std::isfinite(pos[r]) || !std::isfinite(normal[r]) || !std::isfinite(transformedTangent[r])) return false;
        }
        transformedTangent[3]=tangent[3]; return std::isfinite(transformedTangent[3]);
    }

}
#endif

