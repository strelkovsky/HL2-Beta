// Local sequence and additive layer evaluation for MDL 44.
// Equations follow SE2007 SlerpBones and AddSequenceLayers. Pose-parameter
// blends, local/world-space layers and IK require additional work.
#ifndef ENGINE_MDL44_POSE_H
#define ENGINE_MDL44_POSE_H
#include "mdl44_sequence.h"
#include <cfloat>

namespace Mdl44Pose
{
    inline bool Normalize(float q[4])
    {
        float n=0; for(int i=0;i<4;++i) { if(!std::isfinite(q[i])) return false; n+=q[i]*q[i]; }
        if(!std::isfinite(n) || n<1e-12f) return false;
        n=1.0f/std::sqrt(n); for(int i=0;i<4;++i) q[i]*=n; return true;
    }
    inline bool BlendQuaternion(const float a[4],const float b[4],float t,float out[4])
    {
        float dot=0; for(int i=0;i<4;++i) dot+=a[i]*b[i];
        const float sign=dot<0 ? -1.0f : 1.0f;
        for(int i=0;i<4;++i) out[i]=a[i]*(1-t)+b[i]*(t*sign);
        return Normalize(out);
    }
    inline bool SlerpQuaternion(const float a[4],const float b[4],float t,float out[4])
    {
        float dot=0; for(int i=0;i<4;++i) dot+=a[i]*b[i];
        const float sign=dot<0 ? -1.0f : 1.0f; dot=std::fabs(dot); if(dot>1) dot=1;
        if(dot>0.9995f) return BlendQuaternion(a,b,t,out);
        const float angle=std::acos(dot), denom=std::sin(angle);
        const float left=std::sin((1-t)*angle)/denom, right=std::sin(t*angle)/denom*sign;
        for(int i=0;i<4;++i) out[i]=a[i]*left+b[i]*right;
        return Normalize(out);
    }
    inline bool ScaleDelta(const float input[4],float weight,float out[4])
    {
        float q[4]; std::memcpy(q,input,sizeof(q)); if(!Normalize(q)) return false;
        float sn=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]); if(sn>1) sn=1;
        const float scaled=std::sin(std::asin(sn)*weight), factor=scaled/(sn+FLT_EPSILON);
        for(int i=0;i<3;++i) out[i]=q[i]*factor;
        out[3]=std::sqrt((1-scaled*scaled)>0 ? 1-scaled*scaled : 0);
        if(q[3]<0) out[3]=-out[3]; return true;
    }
    inline bool Multiply(const float a[4],const float b[4],float out[4])
    {
        float dot=0; for(int i=0;i<4;++i) dot+=a[i]*b[i]; const float s=dot<0 ? -1.0f : 1.0f;
        const float x=b[0]*s,y=b[1]*s,z=b[2]*s,w=b[3]*s;
        out[0]=a[0]*w+a[1]*z-a[2]*y+a[3]*x;
        out[1]=-a[0]*z+a[1]*w+a[2]*x+a[3]*y;
        out[2]=a[0]*y-a[1]*x+a[2]*w+a[3]*z;
        out[3]=-a[0]*x-a[1]*y-a[2]*z+a[3]*w;
        return Normalize(out);
    }
    struct Source
    {
        const unsigned char *mdl, *ani;
        size_t mdlSize, aniSize;
        const Mdl44Skeleton::Skeleton *skeleton;
        const Mdl44Animation::Animations *animations;
        const Mdl44Sequence::Data *sequences;
        const std::vector<int> *masterBone;
    };
    inline bool Sample(const Source &src, const Mdl44Animation::Descriptor &clip, float cycle,
                       std::vector<Mdl44Animation::LocalPose> &out)
    {
        if(!std::isfinite(cycle) || cycle<0 || cycle>1 || clip.frames<=0) return false;
        Mdl44Animation::Data data;
        if(!Mdl44Animation::Resolve(src.mdl,src.mdlSize,src.ani,src.aniSize,*src.animations,clip,data)) return false;
        const float frame=cycle*(clip.frames-1); const unsigned int a=static_cast<unsigned int>(frame);
        const unsigned int b=a+1<static_cast<unsigned int>(clip.frames)?a+1:a;
        std::vector<Mdl44Animation::LocalPose> first,second;
        const bool delta=(clip.flags & 4)!=0;
        if(!(delta ? Mdl44Animation::SampleDeltaPose(data,clip,*src.skeleton,a,first) :
                     Mdl44Animation::SamplePose(data,clip,*src.skeleton,a,first))) return false;
        const float fraction=frame-a;
        if(b!=a && fraction>0.00001f)
        {
            if(!(delta ? Mdl44Animation::SampleDeltaPose(data,clip,*src.skeleton,b,second) :
                         Mdl44Animation::SamplePose(data,clip,*src.skeleton,b,second))) return false;
            for(size_t i=0;i<first.size();++i)
            {
                for(int k=0;k<3;++k) first[i].position[k]=first[i].position[k]*(1-fraction)+second[i].position[k]*fraction;
                float q[4]; if(!BlendQuaternion(first[i].rotation,second[i].rotation,fraction,q)) return false;
                std::memcpy(first[i].rotation,q,sizeof(q));
            }
        }
        out.swap(first); return true;
    }
    inline bool AccumulateImpl(const Source &src,size_t seqIndex,float cycle,float weight,
                               std::vector<Mdl44Animation::LocalPose> &pose,unsigned int depth)
    {
        if(depth>16 || seqIndex>=src.sequences->sequences.size() || !std::isfinite(cycle) || !std::isfinite(weight) ||
           cycle<0 || cycle>1 || weight<0 || weight>1) return false;
        const Mdl44Sequence::Sequence &seq=src.sequences->sequences[seqIndex];
        if((seq.flags & (0x0200|0x4000)) || seq.animations.size()!=1 ||
           seq.boneWeights.size()!=src.skeleton->bones.size() || src.masterBone->size()!=seq.boneWeights.size()) return false;
        const unsigned int animation=seq.animations[0]; if(animation>=src.animations->clips.size()) return false;
        bool hasWeight=false; for(size_t i=0;i<seq.boneWeights.size();++i) if(seq.boneWeights[i]>0) {hasWeight=true;break;}
        if(hasWeight && weight>0)
        {
            const Mdl44Animation::Descriptor &clip=src.animations->clips[animation];
            if(bool(seq.flags & 4)!=bool(clip.flags & 4)) return false;
            std::vector<Mdl44Animation::LocalPose> sampled; if(!Sample(src,clip,cycle,sampled)) return false;
            for(size_t i=0;i<sampled.size();++i)
            {
                int bone=(*src.masterBone)[i]; if(bone<0) continue;
                if(static_cast<size_t>(bone)>=pose.size()) return false;
                const float w=weight*seq.boneWeights[i]; if(w<=0) continue;
                Mdl44Animation::LocalPose &dst=pose[bone]; const Mdl44Animation::LocalPose &delta=sampled[i];
                for(int k=0;k<3;++k)
                {
                    dst.position[k]=(seq.flags & 4) ? dst.position[k]+delta.position[k]*w : dst.position[k]*(1-w)+delta.position[k]*w;
                    if(!std::isfinite(dst.position[k])) return false;
                }
                float q[4];
                if(seq.flags & 4)
                {
                    float scaled[4]; if(!ScaleDelta(delta.rotation,w,scaled)) return false;
                    if(!(seq.flags & 16) ? !Multiply(scaled,dst.rotation,q) : !Multiply(dst.rotation,scaled,q)) return false;
                }
                else if(!SlerpQuaternion(dst.rotation,delta.rotation,w,q)) return false;
                std::memcpy(dst.rotation,q,sizeof(q));
            }
        }
        for(size_t i=0;i<seq.layers.size();++i)
        {
            const Mdl44Sequence::Layer &layer=seq.layers[i];
            if(!layer.validTiming) continue;
            if(layer.flags & (0x1000|0x4000)) return false; // local or pose-param layer
            float childCycle=cycle, childWeight=weight;
            if(layer.start!=layer.end)
            {
                if(cycle<layer.start || cycle>=layer.end) continue;
                float amount=1;
                if(cycle<layer.peak && layer.start!=layer.peak) amount=(cycle-layer.start)/(layer.peak-layer.start);
                else if(cycle>layer.tail && layer.end!=layer.tail) amount=(layer.end-cycle)/(layer.end-layer.tail);
                if(amount<0 || amount>1 || !std::isfinite(amount)) return false;
                if(layer.flags & 0x40) amount=amount*amount*(3-2*amount);
                if((layer.flags & 0x80) && cycle>layer.tail)
                { float denom=1-weight+amount*weight; childWeight=denom>0 ? amount*weight/denom : 0; }
                else childWeight=(layer.flags & 0x200) ? amount : weight*amount;
                childCycle=(cycle-layer.start)/(layer.end-layer.start);
            }
            if(childWeight>0 && !AccumulateImpl(src,static_cast<size_t>(layer.sequence),childCycle,childWeight,pose,depth+1)) return false;
        }
        return true;
    }
    // Transactional: invalid or unsupported data leaves the input pose unchanged.
    inline bool Accumulate(const Source &src,size_t seq,float cycle,float weight,
                           std::vector<Mdl44Animation::LocalPose> &pose)
    {
        if(!src.mdl || !src.skeleton || !src.animations || !src.sequences || !src.masterBone) return false;
        std::vector<Mdl44Animation::LocalPose> result=pose;
        if(!AccumulateImpl(src,seq,cycle,weight,result,0)) return false;
        pose.swap(result); return true;
    }
}
#endif
