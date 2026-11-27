// Standalone: mdl44_skeleton_test <retail gman_high.mdl>
#include "../mdl44_skeleton.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static void WriteI32(std::vector<unsigned char> &data, size_t offset, int32_t value)
{ std::memcpy(data.data() + offset, &value, sizeof(value)); }

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Mdl44Skeleton::Skeleton result;
    assert(Mdl44Skeleton::Parse(data.data(), data.size(), result));
    assert(result.bones.size() == 70);
    assert(result.bones[0].name == "ValveBiped.Bip01_Pelvis");
    assert(result.bones[0].parent == -1);
    assert(result.bones[1].name == "ValveBiped.Bip01_Spine");
    assert(result.bones[1].parent == 0);
    assert(result.bones[6].name == "ValveBiped.Bip01_Head1");
    assert(result.bones[6].parent == 5);

    const size_t boneOffset = Mdl44Skeleton::ReadU32(data.data(), 160);
    const Mdl44Skeleton::Skeleton original = result;
    assert(!Mdl44Skeleton::Parse(data.data(), data.size() - 1, result));
    assert(result.bones.size() == original.bones.size());
    std::vector<unsigned char> bad = data;
    WriteI32(bad, 160, 0x7fffffff);
    assert(!Mdl44Skeleton::Parse(bad.data(), bad.size(), result));
    bad = data; WriteI32(bad, boneOffset, 0x7fffffff);
    assert(!Mdl44Skeleton::Parse(bad.data(), bad.size(), result));
    bad = data; WriteI32(bad, boneOffset + 4, 1); // 0 -> 1 -> 0
    assert(!Mdl44Skeleton::Parse(bad.data(), bad.size(), result));
    bad = data; WriteI32(bad, boneOffset + 216 + 4, 70);
    assert(!Mdl44Skeleton::Parse(bad.data(), bad.size(), result));
    bad = data; WriteI32(bad, 4, 37);
    assert(!Mdl44Skeleton::Parse(bad.data(), bad.size(), result));
    std::printf("MDL 44 skeleton: %u bones; malformed inputs rejected\n", static_cast<unsigned>(result.bones.size()));
}
