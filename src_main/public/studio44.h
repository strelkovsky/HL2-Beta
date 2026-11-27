// MDL v44 on-disk studio header, based on Valve SE2007 studiohdr_t.
// Legacy game code still uses the v37 studiohdr_t in studio.h. This namespace
// makes the ABI distinction explicit while consumers are migrated.
#ifndef STUDIO44_H
#define STUDIO44_H
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace Studio44
{
    struct studiohdr_t
    {
        int32_t id, version, checksum;
        char name[64];
        int32_t length;
        float eyeposition[3], illumposition[3], hull_min[3], hull_max[3], view_bbmin[3], view_bbmax[3];
        int32_t flags, numbones, boneindex, numbonecontrollers, bonecontrollerindex;
        int32_t numhitboxsets, hitboxsetindex, numlocalanim, localanimindex, numlocalseq, localseqindex;
        int32_t activitylistversion, eventsindexed;
        int32_t numtextures, textureindex, numcdtextures, cdtextureindex;
        int32_t numskinref, numskinfamilies, skinindex, numbodyparts, bodypartindex;
        int32_t numlocalattachments, localattachmentindex, numlocalnodes, localnodeindex, localnodenameindex;
        int32_t numflexdesc, flexdescindex, numflexcontrollers, flexcontrollerindex, numflexrules, flexruleindex;
        int32_t numikchains, ikchainindex, nummouths, mouthindex, numlocalposeparameters, localposeparamindex;
        int32_t surfacepropindex, keyvalueindex, keyvaluesize, numlocalikautoplaylocks, localikautoplaylockindex;
        float mass;
        int32_t contents, numincludemodels, includemodelindex;
        uint32_t virtualModel; // 32-bit runtime slot in the disk format, never dereference.
        int32_t szanimblocknameindex, numanimblocks, animblockindex;
    };
    static_assert(sizeof(studiohdr_t)==360,"MDL 44 header prefix layout");
    static_assert(offsetof(studiohdr_t,length)==76,"MDL 44 length layout");
    static_assert(offsetof(studiohdr_t,numbones)==156,"MDL 44 bone layout");
    static_assert(offsetof(studiohdr_t,numlocalanim)==180,"MDL 44 animation layout");
    static_assert(offsetof(studiohdr_t,numlocalseq)==188,"MDL 44 sequence layout");
    static_assert(offsetof(studiohdr_t,numtextures)==204,"MDL 44 material layout");
    static_assert(offsetof(studiohdr_t,numbodyparts)==232,"MDL 44 body layout");
    static_assert(offsetof(studiohdr_t,numincludemodels)==336,"MDL 44 include layout");
    static_assert(offsetof(studiohdr_t,numanimblocks)==352,"MDL 44 block layout");

    inline bool ValidVector(const float value[3])
    { return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]); }
    inline bool Bounds(const studiohdr_t &header, bool render, float mins[3], float maxs[3])
    {
        if (!mins || !maxs) return false;
        const bool hasView=header.view_bbmin[0]!=0 || header.view_bbmin[1]!=0 || header.view_bbmin[2]!=0 ||
                           header.view_bbmax[0]!=0 || header.view_bbmax[1]!=0 || header.view_bbmax[2]!=0;
        const float *lo=render && hasView ? header.view_bbmin : header.hull_min;
        const float *hi=render && hasView ? header.view_bbmax : header.hull_max;
        if(!ValidVector(lo) || !ValidVector(hi)) return false;
        for(int i=0;i<3;++i) if(lo[i]>hi[i]) return false;
        std::memcpy(mins,lo,3*sizeof(float)); std::memcpy(maxs,hi,3*sizeof(float));
        return true;
    }

    // Copy rather than cast: callers can provide unaligned, untrusted buffers.
    inline bool ReadHeader(const unsigned char *bytes, size_t size, size_t required, studiohdr_t &out)
    {
        if (!bytes || required<80 || required>sizeof(out) || size<required || std::memcmp(bytes,"IDST",4)) return false;
        studiohdr_t header={};
        std::memcpy(&header,bytes,size<sizeof(header)?size:sizeof(header));
        if(header.version!=44 || header.length<0 || static_cast<size_t>(header.length)<required ||
           static_cast<size_t>(header.length)>size) return false;
        out=header; return true;
    }
}
#endif
