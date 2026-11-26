// Reads release VVD v4 vertex data into the 2003 studio vertex layout.
// This does not convert the MDL header or make version 44 models renderable.
#ifndef ENGINE_VVD4_CONVERTER_H
#define ENGINE_VVD4_CONVERTER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Vvd4Converter
{
    struct Vertex37
    {
        float weights[4];
        int16_t bones[4];
        int16_t boneCount, material, firstRef, lastRef;
        float position[3], normal[3], texcoord[2];
    };
    struct Tangent { float value[4]; };
    static_assert( sizeof( Vertex37 ) == 64, "2003 studio vertex layout" );
    static_assert( sizeof( Tangent ) == 16, "studio tangent layout" );

    struct Vertices
    {
        std::vector<Vertex37> vertices;
        std::vector<Tangent> tangents;
    };

    inline uint32_t Read32( const unsigned char *data, size_t offset )
    {
        uint32_t value;
        std::memcpy( &value, data + offset, sizeof( value ) );
        return value;
    }

    inline bool Span( size_t offset, size_t count, size_t stride, size_t size )
    {
        return offset <= size && ( stride == 0 || count <= ( size - offset ) / stride );
    }

    // Expand fixups exactly as Studio_LoadVertexes does for the requested root LOD.
    inline bool Convert( const unsigned char *data, size_t size, uint32_t expectedChecksum,
                         unsigned int rootLod, Vertices &output )
    {
        const size_t headerSize = 64, vertexSize = 48, tangentSize = 16, fixupSize = 12;
        if ( !data || size < headerSize || std::memcmp( data, "IDSV", 4 ) != 0 ||
             Read32( data, 4 ) != 4 || Read32( data, 8 ) != expectedChecksum ) return false;
        const uint32_t lodCount = Read32( data, 12 );
        if ( lodCount == 0 || lodCount > 8 || rootLod >= lodCount ) return false;
        const uint32_t sourceCount = Read32( data, 16 );
        const uint32_t targetCount = Read32( data, 16 + rootLod * 4 );
        const uint32_t fixupCount = Read32( data, 48 ), fixupStart = Read32( data, 52 );
        const uint32_t vertexStart = Read32( data, 56 ), tangentStart = Read32( data, 60 );
        if ( targetCount > sourceCount || !Span( vertexStart, sourceCount, vertexSize, size ) ||
             ( tangentStart && !Span( tangentStart, sourceCount, tangentSize, size ) ) ||
             !Span( fixupStart, fixupCount, fixupSize, size ) ) return false;

        std::vector<uint32_t> order;
        order.reserve( targetCount );
        if ( fixupCount )
        {
            for ( uint32_t i = 0; i < fixupCount; ++i )
            {
                const size_t entry = fixupStart + i * fixupSize;
                const uint32_t lod = Read32( data, entry ), first = Read32( data, entry + 4 );
                const uint32_t count = Read32( data, entry + 8 );
                if ( lod >= lodCount || first > sourceCount || count > sourceCount - first ) return false;
                if ( lod >= rootLod )
                {
                    if ( count > targetCount - order.size() ) return false;
                    for ( uint32_t j = 0; j < count; ++j ) order.push_back( first + j );
                }
            }
        }
        else
        {
            for ( uint32_t i = 0; i < targetCount; ++i ) order.push_back( i );
        }
        if ( order.size() != targetCount ) return false;

        Vertices result;
        result.vertices.resize( targetCount );
        if ( tangentStart ) result.tangents.resize( targetCount );
        for ( uint32_t i = 0; i < targetCount; ++i )
        {
            const unsigned char *src = data + vertexStart + static_cast<size_t>( order[i] ) * vertexSize;
            if ( src[15] > 3 ) return false;
            Vertex37 &vertex = result.vertices[i];
            std::memset( &vertex, 0, sizeof( vertex ) );
            std::memcpy( vertex.weights, src, 3 * sizeof( float ) );
            for ( int j = 0; j < 3; ++j ) vertex.bones[j] = src[12 + j];
            vertex.boneCount = src[15];
            std::memcpy( vertex.position, src + 16, 3 * sizeof( float ) );
            std::memcpy( vertex.normal, src + 28, 3 * sizeof( float ) );
            std::memcpy( vertex.texcoord, src + 40, 2 * sizeof( float ) );
            if ( tangentStart )
                std::memcpy( &result.tangents[i], data + tangentStart + static_cast<size_t>( order[i] ) * tangentSize, tangentSize );
        }
        output.vertices.swap( result.vertices );
        output.tangents.swap( result.tangents );
        return true;
    }
}

#endif
