// Standalone: mdl44_static_test <model.mdl> <model.vvd> <model.dx80.vtx>
#include "../mdl44_static.h"
#include "../vvd4_converter.h"
#include "../mdl44_model.h"
#include "../../public/studio_material_view.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>

static const char *MaterialName( const void *context, size_t i )
{
    return static_cast<const Mdl44Static::Description *>( context )->materials[i].c_str();
}
static const char *MaterialPath( const void *context, size_t i )
{
    return static_cast<const Mdl44Static::Description *>( context )->materialPaths[i].c_str();
}

static std::vector<unsigned char> Read( const char *name )
{
    std::ifstream in( name, std::ios::binary );
    return std::vector<unsigned char>( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}
int main( int argc, char **argv )
{
    if ( argc != 4 ) return 2;
    std::vector<unsigned char> mdl = Read( argv[1] ), vvd = Read( argv[2] ), vtx = Read( argv[3] );
    if ( mdl.size() < 240 || vvd.size() < 64 ) return 3;
    const uint32_t checksum = Mdl44Static::Read32( mdl.data(), 8 );
    Vvd4Converter::Vertices vertices;
    if ( !Vvd4Converter::Convert( vvd.data(), vvd.size(), checksum, 0, vertices ) ) return 4;
    Mdl44Static::Description description;
    if ( !Mdl44Static::Parse( mdl.data(), mdl.size(), static_cast<uint32_t>( vertices.vertices.size() ), description ) ) return 5;
    if ( !Mdl44Static::ValidateVtx( vtx.data(), vtx.size(), description ) ) return 6;
    Mdl44::StaticModel model;
    if ( !model.Load( mdl.data(), mdl.size(), vvd.data(), vvd.size(), vtx.data(), vtx.size() ) ) return 7;
    StudioMaterialView linkedMaterials = model.Materials();
    assert( linkedMaterials.MaterialCount() == description.materials.size() );
    if ( !description.materials.empty() )
    {
        assert( std::strcmp( model.MaterialName( 0, 0 ), description.materials[0].c_str() ) == 0 );
        // Retail VTX often stores replacement tables before their LOD lists.
        std::vector<unsigned char> replacement = vtx;
        const size_t entry = replacement.size();
        const char name[] = "test_lod_material";
        const size_t list = entry + 6 + sizeof( name );
        const uint32_t lods = Vtx7Converter::Read32( vtx.data(), 20 );
        replacement.resize( list + lods * 8, 0 );
        const int32_t stringOffset = 6, entryOffset = static_cast<int32_t>( entry ) - static_cast<int32_t>( list );
        const uint32_t one = 1, listOffset = static_cast<uint32_t>( list );
        std::memcpy( replacement.data() + entry + 2, &stringOffset, 4 );
        std::memcpy( replacement.data() + entry + 6, name, sizeof( name ) );
        std::memcpy( replacement.data() + list, &one, 4 );
        std::memcpy( replacement.data() + list + 4, &entryOffset, 4 );
        std::memcpy( replacement.data() + 24, &listOffset, 4 );
        Mdl44::StaticModel alternate;
        assert( alternate.Load( mdl.data(), mdl.size(), vvd.data(), vvd.size(), replacement.data(), replacement.size() ) );
        assert( std::strcmp( alternate.MaterialName( 0, 0 ), name ) == 0 );
        if ( lods > 1 ) assert( std::strcmp( alternate.MaterialName( 1, 0 ), description.materials[0].c_str() ) == 0 );
        replacement[entry] = 0xff; replacement[entry + 1] = 0xff;
        assert( !alternate.Load( mdl.data(), mdl.size(), vvd.data(), vvd.size(), replacement.data(), replacement.size() ) );
    }
    for ( size_t b = 0; b < description.bodyParts.size(); ++b )
        for ( size_t m = 0; m < description.bodyParts[b].models.size(); ++m )
            for ( size_t k = 0; k < description.bodyParts[b].models[m].meshes.size(); ++k )
            {
                StudioVertexView::Mesh mesh( StudioVertexView::Mesh::Vvd4, nullptr, nullptr, 0 );
                assert( model.Mesh( b, m, k, mesh ) );
                for ( size_t skin = 0; skin < description.skinFamilies; ++skin )
                {
                    uint16_t material;
                    assert( model.MeshMaterial( b, m, k, skin, material ) );
                    assert( material < linkedMaterials.MaterialCount() );
                }
                for ( uint32_t lod = 0; lod < Mdl44Static::Read32( vtx.data(), 20 ); ++lod )
                    for ( size_t g = 0; ; ++g )
                    {
                        Mdl44::GroupView group;
                        if ( !model.Group( b, m, lod, k, g, group ) ) break;
                        for ( size_t i = 0; i < group.VertexCount(); ++i )
                        {
                            Mdl44::OptimizedVertex optimized;
                            StudioVertexView::Vertex vertex;
                            assert( group.Vertex( i, optimized ) );
                            assert( mesh.Read( optimized.meshVertex, vertex ) );
                        }
                        for ( size_t i = 0; i < group.IndexCount(); ++i )
                        {
                            uint16_t index;
                            assert( group.Index( i, index ) && index < group.VertexCount() );
                        }
                        for ( size_t i = 0; i < group.StripCount(); ++i )
                        {
                            Mdl44::Strip strip;
                            assert( group.GetStrip( i, strip ) );
                            assert( strip.indexOffset <= group.IndexCount() && strip.indexCount <= group.IndexCount() - strip.indexOffset );
                            assert( strip.vertexOffset <= group.VertexCount() && strip.vertexCount <= group.VertexCount() - strip.vertexOffset );
                            for ( size_t j = 0; j < strip.boneStateCount; ++j )
                            {
                                int32_t hardware, bone;
                                assert( group.BoneState( i, j, hardware, bone ) );
                            }
                        }
                    }
            }
    StudioMaterialView materials( &description, description.materials.size(), description.materialPaths.size(), MaterialName, MaterialPath );
    for ( size_t i = 0; i < materials.MaterialCount(); ++i )
        assert( std::strcmp( materials.MaterialName( i ), description.materials[i].c_str() ) == 0 );
    for ( size_t i = 0; i < materials.PathCount(); ++i )
        assert( std::strcmp( materials.Path( i ), description.materialPaths[i].c_str() ) == 0 );
    assert( !materials.MaterialName( materials.MaterialCount() ) );
    assert( !materials.Path( materials.PathCount() ) );
    assert( description.checksum == checksum );
    std::printf( "%zu vertices, %zu body parts, %zu materials\n", vertices.vertices.size(), description.bodyParts.size(), description.materials.size() );
    // Failure must leave the previously parsed description intact.
    const size_t bodyCount = description.bodyParts.size();
    const uint32_t invalid = 0xffffffffu;
    std::memcpy( mdl.data() + 208, &invalid, 4 );
    assert( !Mdl44Static::Parse( mdl.data(), mdl.size(), static_cast<uint32_t>( vertices.vertices.size() ), description ) );
    assert( description.bodyParts.size() == bodyCount );
    assert( !Mdl44Static::Parse( mdl.data(), 100, 0, description ) );
    assert( !Vvd4Converter::Convert( vvd.data(), vvd.size(), checksum + 1, 0, vertices ) );
    assert( !Mdl44Static::ValidateVtx( vtx.data(), 20, description ) );
    size_t bodies, models, lods, meshes, groups, verts; uint32_t n;
    assert( Vtx7Converter::Child( vtx.data(), vtx.size(), 0, 28, 32, 8, bodies, n ) && n );
    assert( Vtx7Converter::Child( vtx.data(), vtx.size(), bodies, 0, 4, 8, models, n ) && n );
    assert( Vtx7Converter::Child( vtx.data(), vtx.size(), models, 0, 4, 12, lods, n ) && n );
    assert( Vtx7Converter::Child( vtx.data(), vtx.size(), lods, 0, 4, 9, meshes, n ) && n );
    assert( Vtx7Converter::Child( vtx.data(), vtx.size(), meshes, 0, 4, 25, groups, n ) && n );
    assert( Vtx7Converter::Child( vtx.data(), vtx.size(), groups, 0, 4, 9, verts, n ) && n );
    // A valid VTX can still reference a vertex outside the corresponding MDL mesh.
    const unsigned char original0 = vtx[verts + 4], original1 = vtx[verts + 5];
    vtx[verts + 4] = vtx[verts + 5] = 0xff;
    assert( !Mdl44Static::ValidateVtx( vtx.data(), vtx.size(), description ) );
    vtx[verts + 4] = original0; vtx[verts + 5] = original1;
    vtx[16] ^= 1;
    assert( !Mdl44Static::ValidateVtx( vtx.data(), vtx.size(), description ) );
    assert( !model.Load( mdl.data(), mdl.size(), vvd.data(), vvd.size(), vtx.data(), vtx.size() ) );
    assert( model.Materials().MaterialCount() == description.materials.size() );
}
