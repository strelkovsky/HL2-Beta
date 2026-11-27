// Owned, validated release-model data. Views returned from this object remain
// valid only until it is reloaded, assigned or destroyed.
#ifndef ENGINE_MDL44_MODEL_H
#define ENGINE_MDL44_MODEL_H

#include "mdl44_static.h"
#include "vvd4_native.h"
#include "../public/studio_vertex_view.h"
#include "../public/studio_material_view.h"

namespace Mdl44
{
    struct OptimizedVertex
    {
        unsigned char weightIndex[3], boneCount;
        uint16_t meshVertex;
        int8_t boneId[3];
    };

    struct Strip
    {
        uint32_t indexCount, indexOffset, vertexCount, vertexOffset, boneStateCount;
        uint16_t boneCount;
        unsigned char flags;
    };

    struct MaterialReplacement { uint16_t material; std::string name; };

    class GroupView
    {
    public:
        GroupView() : m_data(NULL), m_vertexStart(0), m_vertexCount(0), m_indexStart(0), m_indexCount(0), m_stripStart(0), m_stripCount(0), m_flags(0) {}
        size_t VertexCount() const { return m_vertexCount; }
        size_t IndexCount() const { return m_indexCount; }
        size_t StripCount() const { return m_stripCount; }
        unsigned char Flags() const { return m_flags; }
        bool Vertex( size_t i, OptimizedVertex &out ) const
        {
            if ( !m_data || i >= m_vertexCount ) return false;
            const unsigned char *p = m_data + m_vertexStart + i * 9;
            std::memcpy( out.weightIndex, p, 3 ); out.boneCount = p[3];
            std::memcpy( &out.meshVertex, p + 4, 2 );
            std::memcpy( out.boneId, p + 6, 3 );
            return true;
        }
        bool Index( size_t i, uint16_t &out ) const
        {
            if ( !m_data || i >= m_indexCount ) return false;
            std::memcpy( &out, m_data + m_indexStart + i * 2, 2 ); return true;
        }
        bool GetStrip( size_t i, Strip &out ) const
        {
            if ( !m_data || i >= m_stripCount ) return false;
            const size_t at = m_stripStart + i * 27;
            out.indexCount = Vtx7Converter::Read32( m_data, at );
            out.indexOffset = Vtx7Converter::Read32( m_data, at + 4 );
            out.vertexCount = Vtx7Converter::Read32( m_data, at + 8 );
            out.vertexOffset = Vtx7Converter::Read32( m_data, at + 12 );
            std::memcpy( &out.boneCount, m_data + at + 16, 2 );
            out.flags = m_data[at + 18];
            out.boneStateCount = Vtx7Converter::Read32( m_data, at + 19 );
            return true;
        }
        bool BoneState( size_t strip, size_t i, int32_t &hardwareId, int32_t &boneId ) const
        {
            Strip info;
            if ( !GetStrip( strip, info ) || i >= info.boneStateCount ) return false;
            const size_t at = m_stripStart + strip * 27;
            const size_t change = at + Vtx7Converter::Read32( m_data, at + 23 ) + i * 8;
            std::memcpy( &hardwareId, m_data + change, 4 );
            std::memcpy( &boneId, m_data + change + 4, 4 );
            return true;
        }
    private:
        friend class StudioModel;
        const unsigned char *m_data;
        size_t m_vertexStart, m_vertexCount, m_indexStart, m_indexCount, m_stripStart, m_stripCount;
        unsigned char m_flags;
    };

    class StudioModel
    {
    public:
        bool Load( const unsigned char *mdl, size_t mdlSize,
                   const unsigned char *vvd, size_t vvdSize,
                   const unsigned char *vtx, size_t vtxSize, bool staticOnly = true )
        {
            if ( !mdl || mdlSize < 12 || !vtx ) return false;
            const uint32_t checksum = Mdl44Static::Read32( mdl, 8 );
            Vvd4Native::Vertices vertices;
            Mdl44Static::Description description;
            std::vector< std::vector<MaterialReplacement> > replacements;
            if ( !Vvd4Native::Load( vvd, vvdSize, checksum, 0, vertices ) ||
                 !Mdl44Static::Parse( mdl, mdlSize, static_cast<uint32_t>( vertices.vertices.size() ), description, staticOnly ) ||
                 !Mdl44Static::ValidateVtx( vtx, vtxSize, description ) ||
                 !ReadReplacements( vtx, vtxSize, description.materials.size(), replacements ) ) return false;
            std::vector<unsigned char> optimized( vtx, vtx + vtxSize );
            m_description = description;
            m_vertices.vertices.swap( vertices.vertices );
            m_vertices.tangents.swap( vertices.tangents );
            m_vtx.swap( optimized );
            m_replacements.swap( replacements );
            return true;
        }

        const Mdl44Static::Description &Description() const { return m_description; }
        size_t LodCount() const { return m_replacements.size(); }
        StudioMaterialView Materials() const
        {
            return StudioMaterialView( &m_description, m_description.materials.size(),
                m_description.materialPaths.size(), LookupMaterialName, LookupMaterialPath );
        }
        const char *MaterialName( size_t lod, size_t material ) const
        {
            if ( lod >= m_replacements.size() || material >= m_description.materials.size() ) return NULL;
            for ( size_t i = 0; i < m_replacements[lod].size(); ++i )
                if ( m_replacements[lod][i].material == material ) return m_replacements[lod][i].name.c_str();
            return m_description.materials[material].c_str();
        }
        bool Mesh( size_t body, size_t model, size_t mesh, StudioVertexView::Mesh &out ) const
        {
            if ( body >= m_description.bodyParts.size() ||
                 model >= m_description.bodyParts[body].models.size() ) return false;
            const Mdl44Static::Model &m = m_description.bodyParts[body].models[model];
            if ( mesh >= m.meshes.size() ) return false;
            const Mdl44Static::Mesh &part = m.meshes[mesh];
            const size_t start = static_cast<size_t>( m.vertexOffset ) + part.vertexOffset;
            if ( start > m_vertices.vertices.size() || part.vertexCount > m_vertices.vertices.size() - start ) return false;
            const void *vertices = part.vertexCount ? &m_vertices.vertices[start] : NULL;
            const float *tangents = part.vertexCount && !m_vertices.tangents.empty() ? m_vertices.tangents[start].data : NULL;
            out = StudioVertexView::Mesh( StudioVertexView::Mesh::Vvd4, vertices, tangents, part.vertexCount );
            return true;
        }
        bool MeshMaterial( size_t body, size_t model, size_t mesh, size_t skin, uint16_t &out ) const
        {
            if ( body >= m_description.bodyParts.size() || model >= m_description.bodyParts[body].models.size() ) return false;
            const Mdl44Static::Model &m = m_description.bodyParts[body].models[model];
            if ( mesh >= m.meshes.size() || skin >= m_description.skinFamilies ) return false;
            const size_t index = skin * m_description.skinRefs + m.meshes[mesh].material;
            if ( index >= m_description.skins.size() ) return false;
            out = m_description.skins[index]; return true;
        }

        bool Group( size_t body, size_t model, size_t lod, size_t mesh, size_t group, GroupView &out ) const
        {
            if ( m_vtx.size() < 36 ) return false;
            const unsigned char *p = m_vtx.data(); const size_t size = m_vtx.size();
            size_t start; uint32_t count;
            if ( !Vtx7Converter::Child( p, size, 0, 28, 32, 8, start, count ) || body >= count ) return false;
            if ( !Vtx7Converter::Child( p, size, start + body * 8, 0, 4, 8, start, count ) || model >= count ) return false;
            if ( !Vtx7Converter::Child( p, size, start + model * 8, 0, 4, 12, start, count ) || lod >= count ) return false;
            if ( !Vtx7Converter::Child( p, size, start + lod * 12, 0, 4, 9, start, count ) || mesh >= count ) return false;
            if ( !Vtx7Converter::Child( p, size, start + mesh * 9, 0, 4, 25, start, count ) || group >= count ) return false;
            const size_t header = start + group * 25;
            GroupView result; size_t vertices, indices, strips; uint32_t nVertices, nIndices, nStrips;
            if ( !Vtx7Converter::Child( p, size, header, 0, 4, 9, vertices, nVertices ) ||
                 !Vtx7Converter::Child( p, size, header, 8, 12, 2, indices, nIndices ) ||
                 !Vtx7Converter::Child( p, size, header, 16, 20, 27, strips, nStrips ) ) return false;
            result.m_data = p; result.m_vertexStart = vertices; result.m_vertexCount = nVertices;
            result.m_indexStart = indices; result.m_indexCount = nIndices;
            result.m_stripStart = strips; result.m_stripCount = nStrips; result.m_flags = p[header + 24];
            out = result; return true;
        }
    private:
        static bool RelativeSigned( size_t base, int32_t relative, size_t size, size_t &out )
        {
            const int64_t value = static_cast<int64_t>( base ) + relative;
            if ( value < 0 || static_cast<uint64_t>( value ) > size ) return false;
            out = static_cast<size_t>( value ); return true;
        }
        static bool ReadReplacements( const unsigned char *p, size_t size, size_t materialCount,
                                      std::vector< std::vector<MaterialReplacement> > &out )
        {
            if ( !p || size < 36 ) return false;
            const uint32_t lodCount = Vtx7Converter::Read32( p, 20 ), start = Vtx7Converter::Read32( p, 24 );
            if ( !lodCount || lodCount > 8 || !Vtx7Converter::Span( start, lodCount, 8, size ) ) return false;
            std::vector< std::vector<MaterialReplacement> > result( lodCount );
            for ( uint32_t lod = 0; lod < lodCount; ++lod )
            {
                const size_t list = start + static_cast<size_t>( lod ) * 8;
                const uint32_t count = Vtx7Converter::Read32( p, list );
                if ( !count ) continue;
                int32_t relative; std::memcpy( &relative, p + list + 4, 4 );
                size_t entries;
                if ( !RelativeSigned( list, relative, size, entries ) || !Vtx7Converter::Span( entries, count, 6, size ) ) return false;
                for ( uint32_t i = 0; i < count; ++i )
                {
                    const size_t entry = entries + static_cast<size_t>( i ) * 6;
                    int16_t id; std::memcpy( &id, p + entry, 2 );
                    std::memcpy( &relative, p + entry + 2, 4 );
                    size_t name; MaterialReplacement item;
                    if ( id < 0 || static_cast<size_t>( id ) >= materialCount ||
                         !RelativeSigned( entry, relative, size, name ) ||
                         !Mdl44Static::ReadString( p, size, name, item.name ) ) return false;
                    item.material = static_cast<uint16_t>( id );
                    result[lod].push_back( item );
                }
            }
            out.swap( result ); return true;
        }
        static const char *LookupMaterialName( const void *p, size_t i )
        { return static_cast<const Mdl44Static::Description *>( p )->materials[i].c_str(); }
        static const char *LookupMaterialPath( const void *p, size_t i )
        { return static_cast<const Mdl44Static::Description *>( p )->materialPaths[i].c_str(); }
        Mdl44Static::Description m_description;
        Vvd4Native::Vertices m_vertices;
        std::vector<unsigned char> m_vtx;
        std::vector< std::vector<MaterialReplacement> > m_replacements;
    };
    typedef StudioModel StaticModel;
}
#endif
