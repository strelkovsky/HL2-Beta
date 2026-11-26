#include "../vvd4_converter.h"
#include <cassert>
#include <cstring>
#include <vector>

static void Put32( std::vector<unsigned char> &data, size_t offset, uint32_t value )
{
    std::memcpy( data.data() + offset, &value, sizeof( value ) );
}

int main()
{
    // Two LODs: fixups reorder vertices for LOD 0 and select one for LOD 1.
    std::vector<unsigned char> source( 64 + 24 + 3 * 48 + 3 * 16, 0 );
    std::memcpy( source.data(), "IDSV", 4 );
    Put32( source, 4, 4 ); Put32( source, 8, 123 ); Put32( source, 12, 2 );
    Put32( source, 16, 3 ); Put32( source, 20, 1 );
    Put32( source, 48, 2 ); Put32( source, 52, 64 );
    Put32( source, 56, 88 ); Put32( source, 60, 232 );
    Put32( source, 64, 1 ); Put32( source, 68, 2 ); Put32( source, 72, 1 );
    Put32( source, 76, 0 ); Put32( source, 80, 0 ); Put32( source, 84, 2 );
    for ( unsigned int i = 0; i < 3; ++i )
    {
        float value = static_cast<float>( i + 1 );
        std::memcpy( source.data() + 88 + i * 48 + 16, &value, sizeof( value ) );
        source[88 + i * 48 + 12] = static_cast<unsigned char>( 10 + i );
        source[88 + i * 48 + 15] = 1;
        std::memcpy( source.data() + 232 + i * 16, &value, sizeof( value ) );
    }
    Vvd4Converter::Vertices out;
    assert( Vvd4Converter::Convert( source.data(), source.size(), 123, 0, out ) );
    assert( out.vertices.size() == 3 && out.tangents.size() == 3 );
    assert( out.vertices[0].position[0] == 3 && out.vertices[1].position[0] == 1 );
    assert( out.vertices[0].bones[0] == 12 && out.vertices[0].boneCount == 1 );
    assert( out.tangents[0].value[0] == 3 );
    assert( Vvd4Converter::Convert( source.data(), source.size(), 123, 1, out ) );
    assert( out.vertices.size() == 1 && out.vertices[0].position[0] == 3 );
    assert( !Vvd4Converter::Convert( source.data(), source.size(), 124, 0, out ) );
    assert( !Vvd4Converter::Convert( source.data(), source.size() - 1, 123, 0, out ) );
    source[88 + 2 * 48 + 15] = 4;
    assert( !Vvd4Converter::Convert( source.data(), source.size(), 123, 0, out ) );
    assert( out.vertices.size() == 1 && out.vertices[0].position[0] == 3 );
}
