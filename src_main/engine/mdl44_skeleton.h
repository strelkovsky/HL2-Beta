// Validated skeleton metadata from retail MDL 44; no animation or rendering.
#ifndef ENGINE_MDL44_SKELETON_H
#define ENGINE_MDL44_SKELETON_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Mdl44Skeleton
{
    struct Bone
    {
        std::string name;
        int32_t parent;
        int32_t controllers[6];
        float position[3], quaternion[4], rotation[3];
        float positionScale[3], rotationScale[3];
        float poseToBone[3][4], alignment[4];
        int32_t flags, procedureType, procedureOffset, physicsBone, contents;
    };
    struct Skeleton
    {
        uint32_t checksum;
        std::vector<Bone> bones;
    };

    inline uint32_t ReadU32( const unsigned char *p, size_t at )
    { uint32_t value; std::memcpy( &value, p + at, sizeof(value) ); return value; }
    inline int32_t ReadI32( const unsigned char *p, size_t at )
    { int32_t value; std::memcpy( &value, p + at, sizeof(value) ); return value; }

    // Offsets and 216-byte bone records match the retail layout and Valve's
    // mstudiobone_t. On failure, leave the caller's skeleton unchanged.
    inline bool Parse( const unsigned char *data, size_t size, Skeleton &output )
    {
        const size_t headerSize = 164, boneSize = 216, maxBones = 128;
        if ( !data || size < headerSize || std::memcmp(data, "IDST", 4) || ReadU32(data, 4) != 44 ) return false;
        const size_t length = ReadU32(data, 76);
        if ( length < headerSize || length > size ) return false;
        const uint32_t count = ReadU32(data, 156), offset = ReadU32(data, 160);
        if ( count > maxBones || offset > length || count > (length - offset) / boneSize ) return false;
        Skeleton parsed; parsed.checksum = ReadU32(data, 8);
        parsed.bones.reserve(count);
        for ( uint32_t i = 0; i < count; ++i )
        {
            const size_t base = offset + static_cast<size_t>(i) * boneSize;
            const int32_t relative = ReadI32(data, base);
            const int64_t namePos = static_cast<int64_t>(base) + relative;
            if ( namePos < 0 || static_cast<uint64_t>(namePos) >= length ) return false;
            const char *name = reinterpret_cast<const char *>(data + static_cast<size_t>(namePos));
            const char *end = static_cast<const char *>(std::memchr(name, 0, length - static_cast<size_t>(namePos)));
            if ( !end || end == name ) return false;
            Bone bone; bone.name.assign(name, end); bone.parent = ReadI32(data, base + 4);
            if ( bone.parent < -1 || bone.parent >= static_cast<int32_t>(count) || bone.parent == static_cast<int32_t>(i) ) return false;
            std::memcpy(bone.controllers, data + base + 8, sizeof(bone.controllers));
            std::memcpy(bone.position, data + base + 32, sizeof(bone.position));
            std::memcpy(bone.quaternion, data + base + 44, sizeof(bone.quaternion));
            std::memcpy(bone.rotation, data + base + 60, sizeof(bone.rotation));
            std::memcpy(bone.positionScale, data + base + 72, sizeof(bone.positionScale));
            std::memcpy(bone.rotationScale, data + base + 84, sizeof(bone.rotationScale));
            std::memcpy(bone.poseToBone, data + base + 96, sizeof(bone.poseToBone));
            std::memcpy(bone.alignment, data + base + 144, sizeof(bone.alignment));
            bone.flags = ReadI32(data, base + 160); bone.procedureType = ReadI32(data, base + 164);
            bone.procedureOffset = ReadI32(data, base + 168); bone.physicsBone = ReadI32(data, base + 172);
            bone.contents = ReadI32(data, base + 180);
            parsed.bones.push_back(bone);
        }
        for ( size_t i = 0; i < parsed.bones.size(); ++i )
        {
            int32_t parent = parsed.bones[i].parent;
            size_t depth = 0;
            while ( parent != -1 )
            {
                if ( ++depth > parsed.bones.size() ) return false;
                parent = parsed.bones[parent].parent;
            }
        }
        output = parsed;
        return true;
    }
}
#endif
