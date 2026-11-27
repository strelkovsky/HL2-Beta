#ifndef SCRIPT_INTRO_CLIENT_H
#define SCRIPT_INTRO_CLIENT_H

#include "utlvector.h"

// Intro state and blend passes from the SE2007 client renderer.
struct IntroDataBlendPass_t
{
    int m_BlendMode;
    float m_Alpha;
};
struct IntroData_t
{
    bool m_bDrawPrimary;
    Vector m_vecCameraView;
    QAngle m_vecCameraViewAngles;
    float m_playerViewFOV;
    CUtlVector<IntroDataBlendPass_t> m_Passes;
    float m_flCurrentFadeColor[4];
};
extern IntroData_t *g_pIntroData;
#endif
