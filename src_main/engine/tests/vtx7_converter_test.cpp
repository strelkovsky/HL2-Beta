// Build standalone with: cl /EHsc /std:c++17 vtx7_converter_test.cpp
#include "../vtx7_converter.h"
#include <cassert>
#include <cstring>
#include <vector>

static void Put32( std::vector<unsigned char> &data, size_t offset, uint32_t value )
{
    std::memcpy( data.data() + offset, &value, sizeof( value ) );
}

int main()
{
    // One body, model, LOD, mesh, strip group and vertex with three indices.
    std::vector<unsigned char> source( 140, 0 );
    Put32( source, 0, 7 ); Put32( source, 16, 0x12345678 );
    Put32( source, 20, 1 ); Put32( source, 28, 1 ); Put32( source, 32, 36 );
    Put32( source, 36, 1 ); Put32( source, 40, 8 );
    Put32( source, 44, 1 ); Put32( source, 48, 8 );
    Put32( source, 52, 1 ); Put32( source, 56, 12 );
    Put32( source, 64, 1 ); Put32( source, 68, 9 );
    Put32( source, 73, 1 ); Put32( source, 77, 25 );
    Put32( source, 81, 3 ); Put32( source, 85, 34 );
    Put32( source, 89, 1 ); Put32( source, 93, 40 ); source[97] = 2;
    source[98] = 2; source[99] = 1; source[100] = 0; source[101] = 1;
    source[102] = 0x34; source[103] = 0x12; source[104] = 7;
    source[105] = 0xff; source[106] = 0xfe;
    Put32( source, 113, 3 ); Put32( source, 121, 1 );
    source[129] = 1; source[131] = 1;

    std::vector<unsigned char> converted;
    assert( Vtx7Converter::Convert( source.data(), source.size(), converted ) );
    assert( converted.size() == source.size() + 15 );
    assert( Vtx7Converter::Read32( converted.data(), 0 ) == 6 );
    assert( Vtx7Converter::Read32( converted.data(), 16 ) == 0x12345678 );
    assert( Vtx7Converter::Read32( converted.data(), 77 ) == source.size() - 73 );
    const unsigned char *v = converted.data() + source.size();
    assert( v[0] == 2 && v[1] == 1 && v[2] == 0 && v[14] == 1 );
    int16_t ids[4]; std::memcpy( ids, v + 4, sizeof( ids ) );
    assert( ids[0] == 7 && ids[1] == -1 && ids[2] == -2 && ids[3] == -1 );
    assert( v[12] == 0x34 && v[13] == 0x12 );

    assert( !Vtx7Converter::Convert( source.data(), 35, converted ) );
    source[101] = 4;
    assert( !Vtx7Converter::Convert( source.data(), source.size(), converted ) );
    source[101] = 1; Put32( source, 85, 1000 );
    assert( !Vtx7Converter::Convert( source.data(), source.size(), converted ) );
}
