// Bind VVD 4 vertex influences to a validated MDL 44 skeleton.
#ifndef ENGINE_MDL44_BINDING_H
#define ENGINE_MDL44_BINDING_H
#include "mdl44_animation.h"
#include "vvd4_native.h"
#include "../public/studio_vertex_view.h"
#include <cmath>

namespace Mdl44Binding
{
    struct Influence { uint16_t bone; float weight; };
    struct Vertex { unsigned int count; Influence influences[3]; };
    inline bool Bind(const Mdl44Skeleton::Skeleton &skeleton, const Vvd4Native::Vertices &vertices,
                     std::vector<Vertex> &out)
    {
        if (skeleton.bones.empty()) return false;
        std::vector<Vertex> result; result.reserve(vertices.vertices.size());
        for (size_t i = 0; i < vertices.vertices.size(); ++i)
        {
            StudioVertexView::Vertex source;
            if (!StudioVertexView::ReadVvd4(vertices.vertices[i].data, sizeof(vertices.vertices[i].data), source) ||
                !source.boneCount || source.boneCount > 3) return false;
            Vertex bound = {}; bound.count = source.boneCount;
            float total = 0.0f;
            for (unsigned int j = 0; j < source.boneCount; ++j)
            {
                if (source.bones[j] < 0 || static_cast<size_t>(source.bones[j]) >= skeleton.bones.size() ||
                    !std::isfinite(source.weights[j]) || source.weights[j] < 0.0f) return false;
                bound.influences[j].bone = static_cast<uint16_t>(source.bones[j]);
                bound.influences[j].weight = source.weights[j];
                total += source.weights[j];
            }
            if (!std::isfinite(total) || total <= 0.0f) return false;
            result.push_back(bound);
        }
        out.swap(result);
        return true;
    }

    struct Matrix { float m[3][4]; };
    inline Matrix Multiply(const Matrix &a, const Matrix &b)
    {
        Matrix r = {};
        for (int i=0;i<3;++i)
        {
            for (int j=0;j<3;++j) for(int k=0;k<3;++k) r.m[i][j] += a.m[i][k]*b.m[k][j];
            r.m[i][3]=a.m[i][3];
            for(int k=0;k<3;++k) r.m[i][3]+=a.m[i][k]*b.m[k][3];
        }
        return r;
    }
    inline bool LocalMatrix(const Mdl44Animation::LocalPose &pose, Matrix &out)
    {
        float q[4], length=0;
        for(int i=0;i<4;++i) { if(!std::isfinite(pose.rotation[i])) return false; length+=pose.rotation[i]*pose.rotation[i]; }
        if (!std::isfinite(length) || length < 0.000001f) return false;
        length=1.0f/std::sqrt(length); for(int i=0;i<4;++i) q[i]=pose.rotation[i]*length;
        const float x=q[0],y=q[1],z=q[2],w=q[3];
        Matrix r = {};
        r.m[0][0]=1-2*(y*y+z*z); r.m[0][1]=2*(x*y-z*w); r.m[0][2]=2*(x*z+y*w);
        r.m[1][0]=2*(x*y+z*w); r.m[1][1]=1-2*(x*x+z*z); r.m[1][2]=2*(y*z-x*w);
        r.m[2][0]=2*(x*z-y*w); r.m[2][1]=2*(y*z+x*w); r.m[2][2]=1-2*(x*x+y*y);
        for(int i=0;i<3;++i) { if(!std::isfinite(pose.position[i])) return false; r.m[i][3]=pose.position[i]; }
        out=r; return true;
    }
    // Produces model-space skin matrices: global animated bone * inverse bind.
    inline bool BuildSkinMatrices(const Mdl44Skeleton::Skeleton &skeleton,
                                  const std::vector<Mdl44Animation::LocalPose> &pose,
                                  std::vector<Matrix> &out, std::vector<Matrix> *animatedBones = NULL)
    {
        const size_t count=skeleton.bones.size();
        if (!count || pose.size()!=count) return false;
        std::vector<Matrix> world(count), result(count);
        std::vector<unsigned char> ready(count,0);
        for(size_t i=0;i<count;++i)
        {
            std::vector<size_t> path; int32_t bone=static_cast<int32_t>(i);
            while(bone!=-1)
            {
                if(bone<0 || static_cast<size_t>(bone)>=count || path.size()>=count) return false;
                if(ready[bone]) break;
                path.push_back(static_cast<size_t>(bone)); bone=skeleton.bones[bone].parent;
                if(bone < -1 || (bone>=0 && static_cast<size_t>(bone)>=count)) return false;
            }
            while(!path.empty())
            {
                const size_t current=path.back(); path.pop_back(); Matrix local;
                if(!LocalMatrix(pose[current],local)) return false;
                const int32_t parent=skeleton.bones[current].parent;
                world[current]=parent<0 ? local : Multiply(world[parent],local); ready[current]=1;
            }
        }
        for(size_t i=0;i<count;++i)
        {
            Matrix inverse; std::memcpy(inverse.m,skeleton.bones[i].poseToBone,sizeof(inverse.m));
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) if(!std::isfinite(inverse.m[r][c])) return false;
            result[i]=Multiply(world[i],inverse);
        }
        out.swap(result); if(animatedBones) animatedBones->swap(world); return true;
    }
    inline bool DeformPosition(const StudioVertexView::Vertex &vertex, const Vertex &binding,
                               const std::vector<Matrix> &matrices, float result[3])
    {
        if (!result || !binding.count || binding.count>3) return false;
        float p[3]={0,0,0}, total=0;
        for(unsigned int i=0;i<binding.count;++i)
        {
            const Influence &influence=binding.influences[i];
            if(influence.bone>=matrices.size() || !std::isfinite(influence.weight) || influence.weight<0) return false;
            total+=influence.weight; const Matrix &m=matrices[influence.bone];
            for(int r=0;r<3;++r)
                p[r]+=influence.weight*(m.m[r][0]*vertex.position[0]+m.m[r][1]*vertex.position[1]+m.m[r][2]*vertex.position[2]+m.m[r][3]);
        }
        if(!std::isfinite(total) || total<=0) return false;
        for(int i=0;i<3;++i) { if(!std::isfinite(p[i])) return false; result[i]=p[i]/total; }
        return true;
    }
}
#endif
