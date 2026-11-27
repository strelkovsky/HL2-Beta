// Validated MDL 44 local sequence and included-model metadata.
// Sequence blending and event playback are intentionally separate.
#ifndef ENGINE_MDL44_SEQUENCE_H
#define ENGINE_MDL44_SEQUENCE_H
#include "mdl44_animation.h"
#include <cctype>

namespace Mdl44Sequence
{
    struct Layer
    {
        int16_t sequence, pose;
        int32_t flags;
        float start, peak, tail, end;
        bool validTiming;
    };
    struct Sequence
    {
        std::string label, activity;
        int32_t flags, width, height, activityWeight;
        std::vector<unsigned int> animations;
        std::vector<float> boneWeights;
        std::vector<Layer> layers;
    };
    struct Include { std::string label, name; };
    struct Data { std::vector<Sequence> sequences; std::vector<Include> includes; };

    inline bool RelativeString(const unsigned char *p, size_t size, size_t base, int32_t offset, std::string &out)
    {
        const int64_t at=static_cast<int64_t>(base)+offset;
        if (at<0 || static_cast<uint64_t>(at)>=size) return false;
        const char *start=reinterpret_cast<const char *>(p+static_cast<size_t>(at));
        const char *end=static_cast<const char *>(std::memchr(start,0,size-static_cast<size_t>(at)));
        if (!end) return false;
        out.assign(start,end); return true;
    }
    inline bool Parse(const unsigned char *p, size_t size, Data &out)
    {
        using namespace Mdl44Skeleton;
        Studio44::studiohdr_t header;
        if (!Studio44::ReadHeader(p,size,344,header)) return false;
        const size_t length=static_cast<size_t>(header.length), seqCount=static_cast<uint32_t>(header.numlocalseq), seqStart=static_cast<uint32_t>(header.localseqindex);
        const size_t includeCount=static_cast<uint32_t>(header.numincludemodels), includeStart=static_cast<uint32_t>(header.includemodelindex), animCount=static_cast<uint32_t>(header.numlocalanim);
        const size_t seqSize=212;
        if (length<344 || length>size || seqStart>length || seqCount>(length-seqStart)/seqSize ||
            includeStart>length || includeCount>(length-includeStart)/8) return false;
        Data parsed;
        for(size_t i=0;i<seqCount;++i)
        {
            const size_t at=seqStart+i*seqSize;
            Sequence seq; seq.flags=ReadI32(p,at+12); seq.width=ReadI32(p,at+68); seq.height=ReadI32(p,at+72); seq.activityWeight=ReadI32(p,at+20);
            const int32_t activityOffset=ReadI32(p,at+8);
            if(activityOffset && !RelativeString(p,length,at,activityOffset,seq.activity)) return false;
            const int32_t blendCount=ReadI32(p,at+56), blendOffset=ReadI32(p,at+60);
            if (!RelativeString(p,length,at,ReadI32(p,at+4),seq.label) || seq.label.empty() ||
                seq.width<=0 || seq.height<=0 || blendCount<=0 ||
                static_cast<int64_t>(seq.width)*seq.height!=blendCount || blendOffset<0 ||
                static_cast<size_t>(blendOffset)>length-at) return false;
            const size_t blendStart=at+static_cast<size_t>(blendOffset);
            if (static_cast<size_t>(blendCount)>(length-blendStart)/2) return false;
            for(int32_t j=0;j<blendCount;++j)
            {
                const int16_t anim=Mdl44Animation::ReadI16(p,blendStart+size_t(j)*2);
                if (anim<0 || static_cast<size_t>(anim)>=animCount) return false;
                seq.animations.push_back(static_cast<unsigned int>(anim));
            }
            const int32_t layerCount=ReadI32(p,at+148), layerOffset=ReadI32(p,at+152);
            const int32_t weightOffset=ReadI32(p,at+156);
            const size_t boneCount=static_cast<uint32_t>(header.numbones);
            if (layerCount<0 || layerOffset<0 || weightOffset<0 || boneCount>128 ||
                static_cast<size_t>(layerOffset)>length-at || static_cast<size_t>(weightOffset)>length-at) return false;
            const size_t layerStart=at+static_cast<size_t>(layerOffset), weightStart=at+static_cast<size_t>(weightOffset);
            if (static_cast<size_t>(layerCount)>(length-layerStart)/24 || boneCount>(length-weightStart)/4) return false;
            for(size_t j=0;j<boneCount;++j)
            {
                float weight; std::memcpy(&weight,p+weightStart+j*4,4);
                if (!std::isfinite(weight) || weight<0.0f || weight>1.0f) return false;
                seq.boneWeights.push_back(weight);
            }
            for(int32_t j=0;j<layerCount;++j)
            {
                const size_t pos=layerStart+size_t(j)*24; Layer layer;
                layer.sequence=Mdl44Animation::ReadI16(p,pos); layer.pose=Mdl44Animation::ReadI16(p,pos+2);
                layer.flags=ReadI32(p,pos+4);
                std::memcpy(&layer.start,p+pos+8,4); std::memcpy(&layer.peak,p+pos+12,4);
                std::memcpy(&layer.tail,p+pos+16,4); std::memcpy(&layer.end,p+pos+20,4);
                if (layer.sequence<0 || static_cast<size_t>(layer.sequence)>=seqCount) return false;
                // Some retail animation groups contain non-finite editor data.
                // Keep the sequence but never evaluate an invalid layer.
                layer.validTiming=std::isfinite(layer.start) && std::isfinite(layer.peak) &&
                                  std::isfinite(layer.tail) && std::isfinite(layer.end);
                seq.layers.push_back(layer);
            }
            parsed.sequences.push_back(seq);
        }
        for(size_t i=0;i<includeCount;++i)
        {
            const size_t at=includeStart+i*8; Include item;
            if (!RelativeString(p,length,at,ReadI32(p,at),item.label) ||
                !RelativeString(p,length,at,ReadI32(p,at+4),item.name) || item.name.empty()) return false;
            parsed.includes.push_back(item);
        }
        out=parsed; return true;
    }
    inline bool EqualName(const std::string &a, const std::string &b)
    {
        if(a.size()!=b.size()) return false;
        for(size_t i=0;i<a.size();++i)
            if(std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i]))) return false;
        return true;
    }
    inline const Sequence *Find(const Data &data, const char *label)
    {
        if (!label) return NULL;
        for(size_t i=0;i<data.sequences.size();++i)
            if(EqualName(data.sequences[i].label,label)) return &data.sequences[i];
        return NULL;
    }
    // SE2007 virtualgroup_t::masterBone: local included-model bone -> master bone.
    // A missing bone is represented by -1, as in the engine's virtual model.
    inline bool MapBones(const Mdl44Skeleton::Skeleton &master, const Mdl44Skeleton::Skeleton &local,
                         std::vector<int> &out)
    {
        if(master.bones.empty() || local.bones.empty()) return false;
        std::vector<int> result(local.bones.size(),-1);
        for(size_t i=0;i<local.bones.size();++i)
            for(size_t j=0;j<master.bones.size();++j)
                if(EqualName(local.bones[i].name,master.bones[j].name)) { result[i]=static_cast<int>(j); break; }
        out.swap(result); return true;
    }
}
#endif
