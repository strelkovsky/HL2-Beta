// Matrix conversion for the experimental StudioRender 44 interface.
#ifndef STUDIO44_TRANSFORM_H
#define STUDIO44_TRANSFORM_H
#include <cstring>
namespace Studio44Transform
{
    // Inputs are row-major 3x4; material-system matrices are column-major 4x4.
    inline void ToMaterial(const float *world, const float *bone, float (&matrix)[4][4])
    {
        std::memset(matrix,0,sizeof(matrix));
        for(int i=0;i<3;++i)
        {
            for(int j=0;j<3;++j)
            {
                float value=bone ? 0.0f : world[i*4+j];
                if(bone) for(int k=0;k<3;++k) value+=world[i*4+k]*bone[k*4+j];
                matrix[j][i]=value;
            }
            float value=world[i*4+3];
            if(bone) for(int k=0;k<3;++k) value+=world[i*4+k]*bone[k*4+3];
            matrix[3][i]=value;
        }
        matrix[3][3]=1.0f;
    }
}
#endif

