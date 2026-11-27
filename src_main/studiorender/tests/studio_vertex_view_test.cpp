#include "../../public/studio_vertex_view.h"
#include "../../engine/vvd4_converter.h"
#include "../../engine/vvd4_native.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main( int argc, char **argv )
{
    if ( argc != 2 ) return 2;
    std::ifstream in( argv[1], std::ios::binary );
    std::vector<unsigned char> data( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
    if ( data.size() < 64 ) return 3;
    const uint32_t checksum = Vvd4Converter::Read32( data.data(), 8 );
    const uint32_t lodCount = Vvd4Converter::Read32( data.data(), 12 );
    for ( uint32_t lod = 0; lod < lodCount; ++lod )
    {
    Vvd4Converter::Vertices vertices;
    Vvd4Native::Vertices native;
    if ( !Vvd4Converter::Convert( data.data(), data.size(), checksum, lod, vertices ) ||
         !Vvd4Native::Load( data.data(), data.size(), checksum, lod, native ) ) return 4;
    assert( vertices.vertices.size() == native.vertices.size() );
    assert( vertices.tangents.size() == native.tangents.size() );
    const float *tangents = vertices.tangents.empty() ? nullptr : vertices.tangents[0].value;
    const float *nativeTangents = native.tangents.empty() ? nullptr : native.tangents[0].data;
    StudioVertexView::Mesh oldMesh( StudioVertexView::Mesh::Studio37,
        vertices.vertices.data(), tangents, vertices.vertices.size() );
    StudioVertexView::Mesh releaseMesh( StudioVertexView::Mesh::Vvd4,
        native.vertices.data(), nativeTangents, native.vertices.size() );
    for ( size_t i = 0; i < vertices.vertices.size(); ++i )
    {
        StudioVertexView::Vertex oldVertex, releaseVertex;
        assert( oldMesh.Read( i, oldVertex ) );
        assert( releaseMesh.Read( i, releaseVertex ) );
        if ( tangents ) assert( std::memcmp( oldMesh.Tangent( i ), releaseMesh.Tangent( i ), 16 ) == 0 );
        assert( oldVertex.boneCount == releaseVertex.boneCount );
        assert( std::memcmp( oldVertex.position, releaseVertex.position, sizeof( oldVertex.position ) ) == 0 );
        assert( std::memcmp( oldVertex.normal, releaseVertex.normal, sizeof( oldVertex.normal ) ) == 0 );
        assert( std::memcmp( oldVertex.texcoord, releaseVertex.texcoord, sizeof( oldVertex.texcoord ) ) == 0 );
        assert( std::memcmp( oldVertex.weights, releaseVertex.weights, sizeof( oldVertex.weights ) ) == 0 );
        assert( std::memcmp( oldVertex.bones, releaseVertex.bones, sizeof( oldVertex.bones ) ) == 0 );
    }
    StudioVertexView::Vertex v = {};
    assert( !oldMesh.Read( vertices.vertices.size(), v ) );
    assert( !releaseMesh.Tangent( vertices.vertices.size() ) );
    assert( !StudioVertexView::Read37( nullptr, 0, v ) );
    assert( !StudioVertexView::ReadVvd4( data.data(), 47, v ) );
    assert( !Vvd4Native::Load( data.data(), data.size(), checksum + 1, lod, native ) );
    assert( !Vvd4Native::Load( data.data(), 63, checksum, lod, native ) );
    std::printf( "LOD %u: %zu equivalent vertices\n", lod, vertices.vertices.size() );
    }
}
