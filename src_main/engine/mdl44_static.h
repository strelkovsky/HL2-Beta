// Read-only description of static retail studio models (MDL version 44).
// This is a preparation step; it does not yet produce an engine studiohdr_t.
#ifndef ENGINE_MDL44_STATIC_H
#define ENGINE_MDL44_STATIC_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "vtx7_converter.h"

namespace Mdl44Static
{
    struct Mesh { uint32_t material, vertexOffset, vertexCount; };
    struct Model { uint32_t vertexOffset, vertexCount; std::vector<Mesh> meshes; };
    struct BodyPart { std::vector<Model> models; };
    struct Description
    {
        Description() : checksum(0), boneCount(0), skinRefs(0), skinFamilies(0) {}
        uint32_t checksum, boneCount;
        float hullMin[3], hullMax[3], viewMin[3], viewMax[3], illumination[3];
        std::vector<std::string> materials, materialPaths;
        std::vector<uint16_t> skins;
        uint32_t skinRefs, skinFamilies;
        std::vector<BodyPart> bodyParts;
    };

    inline uint32_t Read32( const unsigned char *p, size_t offset )
    {
        uint32_t v; std::memcpy( &v, p + offset, sizeof( v ) ); return v;
    }
    inline bool Span( size_t offset, size_t count, size_t stride, size_t size )
    {
        return offset <= size && ( stride == 0 || count <= ( size - offset ) / stride );
    }
    inline bool ReadString( const unsigned char *p, size_t size, size_t start, std::string &out )
    {
        if ( start >= size ) return false;
        const void *end = std::memchr( p + start, 0, size - start );
        if ( !end ) return false;
        out.assign( reinterpret_cast<const char *>( p + start ),
                    static_cast<const unsigned char *>( end ) - ( p + start ) );
        return true;
    }
    inline bool Relative( size_t base, uint32_t relative, size_t size, size_t &result )
    {
        if ( base > size || relative > size - base ) return false;
        result = base + relative; return true;
    }

    // A vertex limit from the checksum-matched VVD keeps MDL geometry in range.
    inline bool Parse( const unsigned char *p, size_t size, uint32_t vertexLimit,
                       Description &output )
    {
        const size_t headerSize = 240, textureSize = 64, bodySize = 16;
        const size_t modelSize = 148, meshSize = 116, vertexSize = 48;
        if ( !p || size < headerSize || std::memcmp( p, "IDST", 4 ) || Read32( p, 4 ) != 44 ) return false;
        const uint32_t length = Read32( p, 76 );
        if ( length < headerSize || length > size || !( Read32( p, 152 ) & 16 ) ) return false;
        size = length;
        Description result;
        std::memcpy( result.illumination, p + 92, 12 );
        std::memcpy( result.hullMin, p + 104, 12 );
        std::memcpy( result.hullMax, p + 116, 12 );
        std::memcpy( result.viewMin, p + 128, 12 );
        std::memcpy( result.viewMax, p + 140, 12 );
        result.checksum = Read32( p, 8 ); result.boneCount = Read32( p, 156 );
        // Animated models, attachments and multiple bones need a separate converter.
        if ( result.boneCount != 1 ) return false;
        const uint32_t textures = Read32( p, 204 ), textureStart = Read32( p, 208 );
        const uint32_t paths = Read32( p, 212 ), pathStart = Read32( p, 216 );
        result.skinRefs = Read32( p, 220 ); result.skinFamilies = Read32( p, 224 );
        const uint32_t skinStart = Read32( p, 228 );
        const uint32_t bodies = Read32( p, 232 ), bodyStart = Read32( p, 236 );
        if ( !Span( textureStart, textures, textureSize, size ) ||
             !Span( pathStart, paths, 4, size ) ||
             !Span( bodyStart, bodies, bodySize, size ) ||
             result.skinFamilies > size / 2 || result.skinRefs > size / 2 ||
             ( result.skinFamilies && result.skinRefs > ( size / 2 ) / result.skinFamilies ) ||
             !Span( skinStart, static_cast<size_t>( result.skinRefs ) * result.skinFamilies, 2, size ) ) return false;
        for ( uint32_t i = 0; i < textures; ++i )
        {
            const size_t tex = textureStart + static_cast<size_t>( i ) * textureSize;
            size_t name; std::string value;
            if ( !Relative( tex, Read32( p, tex ), size, name ) || !ReadString( p, size, name, value ) ) return false;
            result.materials.push_back( value );
        }
        for ( uint32_t i = 0; i < paths; ++i )
        {
            std::string value;
            if ( !ReadString( p, size, Read32( p, pathStart + static_cast<size_t>( i ) * 4 ), value ) ) return false;
            result.materialPaths.push_back( value );
        }
        const size_t skinCount = static_cast<size_t>( result.skinRefs ) * result.skinFamilies;
        for ( size_t i = 0; i < skinCount; ++i )
        {
            uint16_t skin; std::memcpy( &skin, p + skinStart + i * 2, 2 );
            if ( skin >= textures ) return false;
            result.skins.push_back( skin );
        }
        for ( uint32_t i = 0; i < bodies; ++i )
        {
            const size_t body = bodyStart + static_cast<size_t>( i ) * bodySize;
            const uint32_t modelCount = Read32( p, body + 4 ); size_t modelStart;
            if ( !Relative( body, Read32( p, body + 12 ), size, modelStart ) ||
                 !Span( modelStart, modelCount, modelSize, size ) ) return false;
            BodyPart part;
            for ( uint32_t j = 0; j < modelCount; ++j )
            {
                const size_t model = modelStart + static_cast<size_t>( j ) * modelSize;
                const uint32_t meshCount = Read32( p, model + 72 ); size_t meshStart;
                Model item; item.vertexCount = Read32( p, model + 80 );
                const uint32_t vertexBytes = Read32( p, model + 84 );
                if ( vertexBytes % vertexSize ) return false;
                item.vertexOffset = vertexBytes / vertexSize;
                if ( item.vertexOffset > vertexLimit || item.vertexCount > vertexLimit - item.vertexOffset ||
                     !Relative( model, Read32( p, model + 76 ), size, meshStart ) ||
                     !Span( meshStart, meshCount, meshSize, size ) ) return false;
                for ( uint32_t k = 0; k < meshCount; ++k )
                {
                    const size_t mesh = meshStart + static_cast<size_t>( k ) * meshSize;
                    Mesh m = { Read32( p, mesh ), Read32( p, mesh + 12 ), Read32( p, mesh + 8 ) };
                    if ( m.material >= result.skinRefs || m.vertexOffset > item.vertexCount ||
                         m.vertexCount > item.vertexCount - m.vertexOffset ) return false;
                    item.meshes.push_back( m );
                }
                part.models.push_back( item );
            }
            result.bodyParts.push_back( part );
        }
        output = result;
        return true;
    }

    // Check that the optimized geometry describes this MDL, not just a VTX
    // with the same checksum. VTX vertex indices are relative to each MDL mesh.
    inline bool ValidateVtx( const unsigned char *p, size_t size, const Description &mdl )
    {
        if ( !p || size < 36 || Read32( p, 0 ) != 7 || Read32( p, 16 ) != mdl.checksum ) return false;
        if ( !Vtx7Converter::Validate( p, size ) ) return false;
        size_t bodies; uint32_t bodyCount;
        if ( !Vtx7Converter::Child( p, size, 0, 28, 32, 8, bodies, bodyCount ) ||
             bodyCount != mdl.bodyParts.size() ) return false;
        for ( uint32_t b = 0; b < bodyCount; ++b )
        {
            size_t models; uint32_t modelCount;
            if ( !Vtx7Converter::Child( p, size, bodies + static_cast<size_t>( b ) * 8, 0, 4, 8, models, modelCount ) ||
                 modelCount != mdl.bodyParts[b].models.size() ) return false;
            for ( uint32_t m = 0; m < modelCount; ++m )
            {
                const Model &model = mdl.bodyParts[b].models[m];
                size_t lods; uint32_t lodCount;
                if ( !Vtx7Converter::Child( p, size, models + static_cast<size_t>( m ) * 8, 0, 4, 12, lods, lodCount ) ) return false;
                for ( uint32_t l = 0; l < lodCount; ++l )
                {
                    size_t meshes; uint32_t meshCount;
                    if ( !Vtx7Converter::Child( p, size, lods + static_cast<size_t>( l ) * 12, 0, 4, 9, meshes, meshCount ) ||
                         meshCount != model.meshes.size() ) return false;
                    for ( uint32_t k = 0; k < meshCount; ++k )
                    {
                        size_t groups; uint32_t groupCount;
                        if ( !Vtx7Converter::Child( p, size, meshes + static_cast<size_t>( k ) * 9, 0, 4, 25, groups, groupCount ) ) return false;
                        for ( uint32_t g = 0; g < groupCount; ++g )
                        {
                            size_t verts; uint32_t count;
                            if ( !Vtx7Converter::Child( p, size, groups + static_cast<size_t>( g ) * 25, 0, 4, 9, verts, count ) ) return false;
                            for ( uint32_t i = 0; i < count; ++i )
                            {
                                uint16_t index; std::memcpy( &index, p + verts + static_cast<size_t>( i ) * 9 + 4, 2 );
                                if ( index >= model.meshes[k].vertexCount ) return false;
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
}
#endif
