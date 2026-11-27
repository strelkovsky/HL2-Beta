// Read animation descriptors and compressed scalar channels from MDL 44.
// Sampling poses, sequence blending and rendering are separate steps.
#ifndef ENGINE_MDL44_ANIMATION_H
#define ENGINE_MDL44_ANIMATION_H
#include "mdl44_skeleton.h"
#include <cmath>

namespace Mdl44Animation
{
    enum Flags { RawPosition=1, RawRotation=2, AnimatedPosition=4, AnimatedRotation=8, Delta=16, RawRotation64=32 };
    struct Descriptor
    {
        std::string name;
        size_t offset;
        float fps;
        int32_t flags, frames, block, index, sectionOffset, sectionFrames;
    };
    struct Block { uint32_t begin, end; };
    struct Animations { std::vector<Descriptor> clips; std::vector<Block> blocks; };
    struct Data { const unsigned char *bytes; size_t size, start; };
    struct Track { size_t offset; unsigned char bone, flags; };
    enum ChannelResult { ChannelInvalid, ChannelAbsent, ChannelValue };

    inline int16_t ReadI16(const unsigned char *p, size_t at)
    { int16_t v; std::memcpy(&v, p + at, sizeof(v)); return v; }
    inline bool Span(size_t start, size_t length, size_t total)
    { return start <= total && length <= total - start; }
    inline bool Parse(const unsigned char *p, size_t size, Animations &out)
    {
        using namespace Mdl44Skeleton;
        const size_t headerSize = 360, descSize = 100;
        if (!p || size < headerSize || std::memcmp(p, "IDST", 4) || ReadU32(p, 4) != 44) return false;
        const size_t length = ReadU32(p, 76);
        if (length < headerSize || length > size) return false;
        const uint32_t count = ReadU32(p, 180), start = ReadU32(p, 184);
        const uint32_t blockCount = ReadU32(p, 352), blockStart = ReadU32(p, 356);
        if (start > length || count > (length - start) / descSize || blockStart > length ||
            blockCount > (length - blockStart) / 8) return false;
        Animations result;
        for (uint32_t i = 0; i < blockCount; ++i)
        {
            Block b = { ReadU32(p, blockStart + size_t(i) * 8), ReadU32(p, blockStart + size_t(i) * 8 + 4) };
            // Block zero is reserved and may contain uninitialized values.
            if (i && (b.begin > b.end)) return false;
            result.blocks.push_back(b);
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t at = start + size_t(i) * descSize;
            const int64_t nameAt = static_cast<int64_t>(at) + ReadI32(p, at + 4);
            if (nameAt < 0 || static_cast<uint64_t>(nameAt) >= length) return false;
            const char *name = reinterpret_cast<const char *>(p + static_cast<size_t>(nameAt));
            const char *end = static_cast<const char *>(std::memchr(name, 0, length - static_cast<size_t>(nameAt)));
            if (!end || end == name) return false;
            Descriptor d; d.name.assign(name, end); d.offset = at;
            std::memcpy(&d.fps, p + at + 8, sizeof(d.fps));
            d.flags = ReadI32(p, at + 12); d.frames = ReadI32(p, at + 16);
            d.block = ReadI32(p, at + 52); d.index = ReadI32(p, at + 56);
            d.sectionOffset = ReadI32(p, at + 80); d.sectionFrames = ReadI32(p, at + 84);
            if (!std::isfinite(d.fps) || d.fps < 0 || d.frames <= 0 || d.block < -1 ||
                (d.block > 0 && static_cast<size_t>(d.block) >= result.blocks.size()) || d.index < 0 ||
                d.sectionOffset < 0 || d.sectionFrames < 0) return false;
            result.clips.push_back(d);
        }
        out = result;
        return true;
    }
    // MDL block 0 uses descriptor-relative offsets; external blocks use
    // offsets from the beginning of the selected block in the matching ANI.
    inline bool Resolve(const unsigned char *mdl, size_t mdlSize, const unsigned char *ani, size_t aniSize,
                        const Animations &anims, const Descriptor &clip, Data &out)
    {
        if (!mdl || clip.block < 0 || clip.index < 0 || clip.sectionFrames != 0) return false;
        if (clip.block == 0)
        {
            if (mdlSize < 80) return false;
            const size_t length = Mdl44Skeleton::ReadU32(mdl, 76);
            if (length > mdlSize || clip.offset > length || static_cast<size_t>(clip.index) > length - clip.offset) return false;
            out = { mdl, length, clip.offset + static_cast<size_t>(clip.index) };
            return Span(out.start, 4, out.size);
        }
        if (!ani || aniSize < 8 || std::memcmp(ani, "IDAG", 4) || Mdl44Skeleton::ReadU32(ani, 4) != 44 ||
            static_cast<size_t>(clip.block) >= anims.blocks.size()) return false;
        const Block &block = anims.blocks[clip.block];
        if (block.begin > block.end || block.end > aniSize || static_cast<size_t>(clip.index) > block.end - block.begin) return false;
        out = { ani, block.end, block.begin + static_cast<size_t>(clip.index) };
        return Span(out.start, 4, out.size);
    }
    inline bool FindTrack(const Data &data, unsigned int bone, size_t boneCount, Track &out)
    {
        if (!data.bytes || bone >= boneCount || !Span(data.start, 4, data.size)) return false;
        size_t at = data.start;
        for (size_t i = 0; i < boneCount; ++i)
        {
            if (!Span(at, 4, data.size)) return false;
            const unsigned int index = data.bytes[at], flags = data.bytes[at + 1];
            if (index >= boneCount) return false;
            if (index == bone) { out = { at, static_cast<unsigned char>(index), static_cast<unsigned char>(flags) }; return true; }
            const int16_t next = ReadI16(data.bytes, at + 2);
            if (next == 0) return false;
            if (next < 4 || !Span(at, static_cast<size_t>(next), data.size)) return false;
            at += static_cast<size_t>(next);
        }
        return false;
    }
    // The run header stores [valid,total], followed by signed 16-bit values.
    // Once valid values are exhausted the last one repeats through the run.
    inline bool SampleStream(const unsigned char *p, size_t size, size_t at, unsigned int frame, int16_t &out)
    {
        if (!p) return false;
        while (Span(at, 2, size))
        {
            const unsigned int valid = p[at], total = p[at + 1];
            if (!valid || !total || valid > total || !Span(at + 2, size_t(valid) * 2, size)) return false;
            if (frame < total)
            {
                const size_t index = frame < valid ? frame : valid - 1;
                out = ReadI16(p, at + 2 + index * 2); return true;
            }
            frame -= total;
            at += 2 + size_t(valid) * 2;
        }
        return false;
    }
    inline ChannelResult SampleChannel(const Data &data, const Track &track, bool rotation,
                                       unsigned int axis, unsigned int frame, int16_t &out)
    {
        if (!data.bytes || axis >= 3 || !Span(track.offset, 4, data.size)) return ChannelInvalid;
        const unsigned int needed = rotation ? AnimatedRotation : AnimatedPosition;
        if (!(track.flags & needed)) return ChannelAbsent;
        size_t table = track.offset + 4;
        if (!rotation && (track.flags & AnimatedRotation)) table += 6;
        if (!Span(table, 6, data.size)) return ChannelInvalid;
        const int16_t relative = ReadI16(data.bytes, table + axis * 2);
        if (relative == 0) { out = 0; return ChannelValue; }
        if (relative < 0 || static_cast<size_t>(relative) > data.size - table) return ChannelInvalid;
        return SampleStream(data.bytes, data.size, table + static_cast<size_t>(relative), frame, out) ? ChannelValue : ChannelInvalid;
    }

    struct LocalPose { float position[3], rotation[4]; };
    inline uint16_t ReadU16(const unsigned char *p, size_t at)
    { uint16_t v; std::memcpy(&v, p + at, sizeof(v)); return v; }
    inline bool HalfFloat(uint16_t bits, float &out)
    {
        const unsigned int exponent = (bits >> 10) & 31, mantissa = bits & 1023;
        if (exponent == 31) return false;
        float value = exponent ? std::ldexp(static_cast<float>(1024 + mantissa), static_cast<int>(exponent) - 25)
                               : std::ldexp(static_cast<float>(mantissa), -24);
        out = (bits & 0x8000) ? -value : value;
        return std::isfinite(out);
    }
    inline bool PackedQuaternion(const unsigned char *p, size_t size, size_t at, bool wide, float out[4])
    {
        if (!Span(at, wide ? 8 : 6, size)) return false;
        bool negative;
        if (wide)
        {
            uint64_t bits; std::memcpy(&bits, p + at, 8);
            const uint64_t mask = (uint64_t(1) << 21) - 1;
            for (int i=0;i<3;++i) out[i] = (static_cast<int>((bits >> (21*i)) & mask) - 1048576) / 1048576.5f;
            negative = (bits >> 63) != 0;
        }
        else
        {
            const uint16_t x=ReadU16(p,at), y=ReadU16(p,at+2), z=ReadU16(p,at+4);
            out[0]=(static_cast<int>(x)-32768)/32768.0f; out[1]=(static_cast<int>(y)-32768)/32768.0f;
            out[2]=(static_cast<int>(z & 0x7fff)-16384)/16384.0f; negative=(z & 0x8000)!=0;
        }
        const float remaining = 1.0f - out[0]*out[0] - out[1]*out[1] - out[2]*out[2];
        if (remaining < -0.001f) return false;
        out[3] = std::sqrt(remaining > 0.0f ? remaining : 0.0f);
        if (negative) out[3] = -out[3];
        return true;
    }
    inline void EulerQuaternion(const float angle[3], float q[4])
    {
        const float sr=std::sin(angle[0]*0.5f), cr=std::cos(angle[0]*0.5f);
        const float sp=std::sin(angle[1]*0.5f), cp=std::cos(angle[1]*0.5f);
        const float sy=std::sin(angle[2]*0.5f), cy=std::cos(angle[2]*0.5f);
        q[0]=sr*cp*cy-cr*sp*sy; q[1]=cr*sp*cy+sr*cp*sy;
        q[2]=cr*cp*sy-sr*sp*cy; q[3]=cr*cp*cy+sr*sp*sy;
    }
    // Sample local transforms at an integer frame. Delta clips default to an
    // identity transform; blending and fractional frames are separate steps.
    inline bool SamplePoseImpl(const Data &data, const Descriptor &clip, const Mdl44Skeleton::Skeleton &skeleton,
                               unsigned int frame, bool deltaClip, std::vector<LocalPose> &out)
    {
        if (!data.bytes || frame >= static_cast<unsigned int>(clip.frames) || bool(clip.flags & 4) != deltaClip ||
            skeleton.bones.empty() || !Span(data.start,4,data.size)) return false;
        std::vector<LocalPose> result(skeleton.bones.size());
        std::vector<bool> seen(skeleton.bones.size(),false);
        for(size_t i=0;i<result.size();++i)
        {
            if (deltaClip)
            {
                std::memset(result[i].position,0,sizeof(result[i].position));
                std::memset(result[i].rotation,0,sizeof(result[i].rotation)); result[i].rotation[3]=1.0f;
            }
            else
            {
                std::memcpy(result[i].position,skeleton.bones[i].position,sizeof(result[i].position));
                std::memcpy(result[i].rotation,skeleton.bones[i].quaternion,sizeof(result[i].rotation));
            }
        }
        size_t at=data.start;
        for(size_t count=0; count <= skeleton.bones.size(); ++count)
        {
            if (!Span(at,4,data.size)) return false;
            const unsigned int bone=data.bytes[at], flags=data.bytes[at+1];
            if (bone==255) { out.swap(result); return true; }
            if (bone>=result.size() || seen[bone] || (flags & ~63u) ||
                ((flags & RawRotation) && (flags & RawRotation64)) ||
                ((flags & (RawRotation|RawRotation64)) && (flags & AnimatedRotation)) ||
                ((flags & RawPosition) && (flags & AnimatedPosition))) return false;
            seen[bone]=true;
            const Mdl44Skeleton::Bone &base=skeleton.bones[bone]; LocalPose &pose=result[bone];
            // Track flags, not descriptor flags, decide whether channels are offsets.
            const bool deltaTrack=(flags & Delta)!=0;
            if (deltaTrack)
            {
                std::memset(pose.position,0,sizeof(pose.position));
                std::memset(pose.rotation,0,sizeof(pose.rotation)); pose.rotation[3]=1.0f;
            }
            else
            {
                std::memcpy(pose.position,base.position,sizeof(pose.position));
                std::memcpy(pose.rotation,base.quaternion,sizeof(pose.rotation));
            }
            Track track={at,static_cast<unsigned char>(bone),static_cast<unsigned char>(flags)};
            if (flags & (RawRotation|RawRotation64))
            {
                if (!PackedQuaternion(data.bytes,data.size,at+4,(flags & RawRotation64)!=0,pose.rotation)) return false;
            }
            else if (flags & AnimatedRotation)
            {
                float angles[3];
                for(unsigned axis=0;axis<3;++axis)
                {
                    int16_t v=0; if (SampleChannel(data,track,true,axis,frame,v)!=ChannelValue) return false;
                    angles[axis]=(deltaTrack ? 0.0f : base.rotation[axis])+v*base.rotationScale[axis];
                    if (!std::isfinite(angles[axis])) return false;
                }
                EulerQuaternion(angles,pose.rotation);
                if (!deltaTrack && (base.flags & 0x00100000))
                {
                    float dot=0; for(int j=0;j<4;++j) dot+=pose.rotation[j]*base.alignment[j];
                    if(dot<0) for(int j=0;j<4;++j) pose.rotation[j]=-pose.rotation[j];
                }
            }
            if (flags & RawPosition)
            {
                const size_t pos=at+4+((flags & RawRotation)?6:0)+((flags & RawRotation64)?8:0);
                if (!Span(pos,6,data.size)) return false;
                for(unsigned axis=0;axis<3;++axis) if(!HalfFloat(ReadU16(data.bytes,pos+axis*2),pose.position[axis])) return false;
            }
            else if (flags & AnimatedPosition)
            {
                for(unsigned axis=0;axis<3;++axis)
                {
                    int16_t v=0; if(SampleChannel(data,track,false,axis,frame,v)!=ChannelValue) return false;
                    pose.position[axis]=(deltaTrack ? 0.0f : base.position[axis])+v*base.positionScale[axis];
                    if (!std::isfinite(pose.position[axis])) return false;
                }
            }
            const int16_t next=ReadI16(data.bytes,at+2);
            if (!next) { out.swap(result); return true; }
            if (next<4 || !Span(at,static_cast<size_t>(next),data.size)) return false;
            at+=static_cast<size_t>(next);
        }
        return false;
    }
    inline bool SamplePose(const Data &data, const Descriptor &clip, const Mdl44Skeleton::Skeleton &skeleton,
                           unsigned int frame, std::vector<LocalPose> &out)
    { return SamplePoseImpl(data,clip,skeleton,frame,false,out); }
    inline bool SampleDeltaPose(const Data &data, const Descriptor &clip, const Mdl44Skeleton::Skeleton &skeleton,
                                unsigned int frame, std::vector<LocalPose> &out)
    { return SamplePoseImpl(data,clip,skeleton,frame,true,out); }
}
#endif
