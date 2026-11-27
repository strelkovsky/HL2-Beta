// Converts the version 7 VTX vertex layout to the version 6 layout used by
// the 2003 StudioRender. This does not convert MDL or VVD data.
#ifndef ENGINE_VTX7_CONVERTER_H
#define ENGINE_VTX7_CONVERTER_H

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Vtx7Converter
{
    struct Group { size_t header, vertices; uint32_t count; };

    inline bool Span( size_t offset, size_t count, size_t stride, size_t limit )
    {
        return offset <= limit && ( stride == 0 || count <= ( limit - offset ) / stride );
    }

    inline uint32_t Read32( const unsigned char *data, size_t offset )
    {
        uint32_t value;
        std::memcpy( &value, data + offset, sizeof( value ) );
        return value;
    }

    inline void Write32( unsigned char *data, size_t offset, uint32_t value )
    {
        std::memcpy( data + offset, &value, sizeof( value ) );
    }

    // All offsets in the VTX hierarchy are relative to their parent structure.
    inline bool Child( const unsigned char *data, size_t size, size_t parent,
                       size_t countOffset, size_t offsetOffset, size_t stride,
                       size_t &start, uint32_t &count )
    {
        if ( !Span( parent, offsetOffset + 4, 1, size ) ) return false;
        count = Read32( data, parent + countOffset );
        const uint32_t relative = Read32( data, parent + offsetOffset );
        if ( relative > size - parent ) return false;
        start = parent + relative;
        return Span( start, count, stride, size );
    }

    inline bool Process( const unsigned char *data, size_t size,
                         std::vector<unsigned char> *destination )
    {
        const size_t fileHeader = 36, bodySize = 8, modelSize = 8;
        const size_t lodSize = 12, meshSize = 9, groupSize = 25;
        const size_t sourceVertexSize = 9, targetVertexSize = 15, stripSize = 27;
        if ( !data || size < fileHeader || size > INT_MAX || Read32( data, 0 ) != 7 ||
             Read32( data, 20 ) == 0 || Read32( data, 20 ) > 8 ) return false;

        std::vector<Group> groups;
        size_t bodies; uint32_t bodyCount;
        if ( !Child( data, size, 0, 28, 32, bodySize, bodies, bodyCount ) ) return false;
        size_t extra = 0;
        for ( uint32_t b = 0; b < bodyCount; ++b )
        {
            size_t models; uint32_t modelCount;
            if ( !Child( data, size, bodies + b * bodySize, 0, 4, modelSize, models, modelCount ) ) return false;
            for ( uint32_t m = 0; m < modelCount; ++m )
            {
                size_t lods; uint32_t lodCount;
                if ( !Child( data, size, models + m * modelSize, 0, 4, lodSize, lods, lodCount ) || lodCount > 8 ) return false;
                for ( uint32_t l = 0; l < lodCount; ++l )
                {
                    size_t meshes; uint32_t meshCount;
                    if ( !Child( data, size, lods + l * lodSize, 0, 4, meshSize, meshes, meshCount ) ) return false;
                    for ( uint32_t m = 0; m < meshCount; ++m )
                    {
                        size_t meshGroups; uint32_t groupCount;
                        if ( !Child( data, size, meshes + m * meshSize, 0, 4, groupSize, meshGroups, groupCount ) ) return false;
                        for ( uint32_t g = 0; g < groupCount; ++g )
                        {
                            const size_t group = meshGroups + g * groupSize;
                            size_t vertices, indices, strips; uint32_t vertexCount, indexCount, stripCount;
                            if ( !Child( data, size, group, 0, 4, sourceVertexSize, vertices, vertexCount ) ||
                                 !Child( data, size, group, 8, 12, 2, indices, indexCount ) ||
                                 !Child( data, size, group, 16, 20, stripSize, strips, stripCount ) ) return false;
                            // Version 6 has no delta-flexed strip groups.
                            if ( data[group + 24] & ~3u ) return false;
                            if ( vertexCount > ( INT_MAX - size - extra ) / targetVertexSize ) return false;
                            for ( uint32_t i = 0; i < vertexCount; ++i )
                            {
                                const unsigned char *v = data + vertices + i * sourceVertexSize;
                                if ( v[3] > 3 ) return false;
                            }
                            for ( uint32_t i = 0; i < indexCount; ++i )
                            {
                                uint16_t index;
                                std::memcpy( &index, data + indices + i * 2, sizeof( index ) );
                                if ( index >= vertexCount ) return false;
                            }
                            for ( uint32_t i = 0; i < stripCount; ++i )
                            {
                                const size_t strip = strips + i * stripSize;
                                const uint32_t ni = Read32( data, strip ), io = Read32( data, strip + 4 );
                                const uint32_t nv = Read32( data, strip + 8 ), vo = Read32( data, strip + 12 );
                                uint16_t bones;
                                std::memcpy( &bones, data + strip + 16, sizeof( bones ) );
                                size_t changes; uint32_t changeCount;
                                if ( io > indexCount || ni > indexCount - io ||
                                     vo > vertexCount || nv > vertexCount - vo || bones > 16 ||
                                     !Child( data, size, strip, 19, 23, 8, changes, changeCount ) ) return false;
                            }
                            Group item = { group, vertices, vertexCount };
                            groups.push_back( item );
                            extra += static_cast<size_t>( vertexCount ) * targetVertexSize;
                        }
                    }
                }
            }
        }

        if ( !destination ) return true;
        std::vector<unsigned char> &output = *destination;
        output.assign( data, data + size );
        output.resize( size + extra, 0 );
        Write32( output.data(), 0, 6 );
        size_t dst = size;
        for ( size_t n = 0; n < groups.size(); ++n )
        {
            const Group &g = groups[n];
            if ( dst < g.header || dst - g.header > INT_MAX ) return false;
            Write32( output.data(), g.header + 4, static_cast<uint32_t>( dst - g.header ) );
            for ( uint32_t i = 0; i < g.count; ++i )
            {
                const unsigned char *src = data + g.vertices + i * sourceVertexSize;
                unsigned char *v = output.data() + dst;
                // v7: three weight indices, bone count, vertex index, three byte bone IDs.
                // v6: four weight indices, four short bone IDs, vertex index, bone count.
                std::memcpy( v, src, 3 );
                for ( int j = 0; j < 3; ++j )
                {
                    const int16_t bone = static_cast<int8_t>( src[6 + j] );
                    std::memcpy( v + 4 + j * 2, &bone, sizeof( bone ) );
                }
                const int16_t noBone = -1;
                std::memcpy( v + 10, &noBone, sizeof( noBone ) );
                std::memcpy( v + 12, src + 4, 2 );
                v[14] = src[3];
                dst += targetVertexSize;
            }
        }
        return true;
    }

    inline bool Validate( const unsigned char *data, size_t size )
    {
        return Process( data, size, NULL );
    }

    inline bool Convert( const unsigned char *data, size_t size,
                         std::vector<unsigned char> &output )
    {
        return Process( data, size, &output );
    }
}

#endif
