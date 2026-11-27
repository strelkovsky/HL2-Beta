// Validated MDL 44 local sequence and included-model metadata.
// Sequence blending and event playback are intentionally separate.
#ifndef ENGINE_MDL44_SEQUENCE_H
#define ENGINE_MDL44_SEQUENCE_H
#include "mdl44_animation.h"
#include <cctype>

namespace Mdl44Sequence
{
    struct Sequence
    {
        std::string label;
        int32_t flags, width, height;
        std::vector<unsigned int> animations;
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
        if (!p || size<344 || std::memcmp(p,"IDST",4) || ReadU32(p,4)!=44) return false;
        const size_t length=ReadU32(p,76), seqCount=ReadU32(p,188), seqStart=ReadU32(p,192);
        const size_t includeCount=ReadU32(p,336), includeStart=ReadU32(p,340), animCount=ReadU32(p,180);
        const size_t seqSize=212;
        if (length<344 || length>size || seqStart>length || seqCount>(length-seqStart)/seqSize ||
            includeStart>length || includeCount>(length-includeStart)/8) return false;
        Data parsed;
        for(size_t i=0;i<seqCount;++i)
        {
            const size_t at=seqStart+i*seqSize;
            Sequence seq; seq.flags=ReadI32(p,at+12); seq.width=ReadI32(p,at+68); seq.height=ReadI32(p,at+72);
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
