// Format-independent vertex data used by StudioRender.
// MDL 37 stores 64-byte vertices inline; retail VVD 4 stores 48-byte vertices.
#ifndef STUDIO_VERTEX_VIEW_H
#define STUDIO_VERTEX_VIEW_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace StudioVertexView
{
    struct Vertex
    {
        float position[3], normal[3], texcoord[2];
        float weights[4];
        int16_t bones[4];
        unsigned int boneCount;
    };

    inline bool Read37( const void *source, size_t size, Vertex &out )
    {
        if ( !source || size < 64 ) return false;
        const unsigned char *p = static_cast<const unsigned char *>( source );
        int16_t count;
        std::memcpy( &count, p + 24, sizeof( count ) );
        if ( count < 0 || count > 4 ) return false;
        Vertex result = {};
        std::memcpy( result.weights, p, 16 );
        std::memcpy( result.bones, p + 16, 8 );
        std::memcpy( result.position, p + 32, 12 );
        std::memcpy( result.normal, p + 44, 12 );
        std::memcpy( result.texcoord, p + 56, 8 );
        result.boneCount = static_cast<unsigned int>( count );
        out = result;
        return true;
    }

    inline bool ReadVvd4( const void *source, size_t size, Vertex &out )
    {
        if ( !source || size < 48 ) return false;
        const unsigned char *p = static_cast<const unsigned char *>( source );
        if ( p[15] > 3 ) return false;
        Vertex result = {};
        std::memcpy( result.weights, p, 12 );
        for ( int i = 0; i < 3; ++i ) result.bones[i] = p[12 + i];
        result.boneCount = p[15];
        std::memcpy( result.position, p + 16, 12 );
        std::memcpy( result.normal, p + 28, 12 );
        std::memcpy( result.texcoord, p + 40, 8 );
        out = result;
        return true;
    }

    // Non-owning view: the model cache must outlive the view.
    class Mesh
    {
    public:
        enum Format { Studio37, Vvd4 };
        Mesh( Format format, const void *vertices, const float *tangents, size_t count )
            : m_format( format ), m_vertices( static_cast<const unsigned char *>( vertices ) ),
              m_tangents( reinterpret_cast<const unsigned char *>( tangents ) ), m_count( count ) {}

        bool Read( size_t index, Vertex &out ) const
        {
            if ( !m_vertices || index >= m_count ) return false;
            const size_t stride = m_format == Studio37 ? 64 : 48;
            if ( index > static_cast<size_t>( -1 ) / stride ) return false;
            const unsigned char *vertex = m_vertices + index * stride;
            return m_format == Studio37 ? Read37( vertex, stride, out ) : ReadVvd4( vertex, stride, out );
        }

        const float *Tangent( size_t index ) const
        {
            return m_tangents && index < m_count && index <= static_cast<size_t>( -1 ) / 16
                ? reinterpret_cast<const float *>( m_tangents + index * 16 ) : nullptr;
        }

    private:
        Format m_format;
        const unsigned char *m_vertices;
        const unsigned char *m_tangents;
        size_t m_count;
    };
}
#endif
