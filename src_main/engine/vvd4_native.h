// Materialize VVD v4 fixups without converting release vertices to MDL 37.
#ifndef ENGINE_VVD4_NATIVE_H
#define ENGINE_VVD4_NATIVE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Vvd4Native
{
    struct Vertex { unsigned char data[48]; };
    struct Tangent { float data[4]; };
    static_assert( sizeof( Vertex ) == 48, "VVD vertex layout" );
    static_assert( sizeof( Tangent ) == 16, "VVD tangent layout" );
    struct Vertices
    {
        std::vector<Vertex> vertices;
        std::vector<Tangent> tangents;
    };

    inline uint32_t Read32( const unsigned char *p, size_t offset )
    {
        uint32_t value; std::memcpy( &value, p + offset, sizeof( value ) ); return value;
    }
    inline bool Span( size_t offset, size_t count, size_t stride, size_t size )
    {
        return offset <= size && ( stride == 0 || count <= ( size - offset ) / stride );
    }

    inline bool Load( const unsigned char *p, size_t size, uint32_t checksum,
                      unsigned int rootLod, Vertices &output )
    {
        if ( !p || size < 64 || std::memcmp( p, "IDSV", 4 ) ||
             Read32( p, 4 ) != 4 || Read32( p, 8 ) != checksum ) return false;
        const uint32_t lodCount = Read32( p, 12 );
        if ( !lodCount || lodCount > 8 || rootLod >= lodCount ) return false;
        const uint32_t sourceCount = Read32( p, 16 ), count = Read32( p, 16 + rootLod * 4 );
        const uint32_t fixupCount = Read32( p, 48 ), fixupStart = Read32( p, 52 );
        const uint32_t vertexStart = Read32( p, 56 ), tangentStart = Read32( p, 60 );
        if ( count > sourceCount || !Span( vertexStart, sourceCount, 48, size ) ||
             ( tangentStart && !Span( tangentStart, sourceCount, 16, size ) ) ||
             !Span( fixupStart, fixupCount, 12, size ) ) return false;

        std::vector<uint32_t> indices;
        indices.reserve( count );
        if ( !fixupCount )
        {
            for ( uint32_t i = 0; i < count; ++i ) indices.push_back( i );
        }
        else
        {
            for ( uint32_t i = 0; i < fixupCount; ++i )
            {
                const size_t at = fixupStart + static_cast<size_t>( i ) * 12;
                const uint32_t lod = Read32( p, at ), first = Read32( p, at + 4 ), n = Read32( p, at + 8 );
                if ( lod >= lodCount || first > sourceCount || n > sourceCount - first ) return false;
                if ( lod >= rootLod )
                {
                    if ( n > count - indices.size() ) return false;
                    for ( uint32_t j = 0; j < n; ++j ) indices.push_back( first + j );
                }
            }
        }
        if ( indices.size() != count ) return false;
        Vertices result;
        result.vertices.resize( count );
        if ( tangentStart ) result.tangents.resize( count );
        for ( uint32_t i = 0; i < count; ++i )
        {
            const unsigned char *source = p + vertexStart + static_cast<size_t>( indices[i] ) * 48;
            if ( source[15] > 3 ) return false;
            std::memcpy( &result.vertices[i], source, 48 );
            if ( tangentStart )
                std::memcpy( &result.tangents[i], p + tangentStart + static_cast<size_t>( indices[i] ) * 16, 16 );
        }
        output.vertices.swap( result.vertices );
        output.tangents.swap( result.tangents );
        return true;
    }
}
#endif
