// Internal handshake between the material system and the shader API.
#ifndef IHDRSHADERCONTROL_H
#define IHDRSHADERCONTROL_H

#define HDR_SHADER_CONTROL_INTERFACE_VERSION "HDRShaderControl001"

class IHDRShaderControl
{
public:
    virtual void SetHDRShaderAvailable( bool available ) = 0;
};

#endif
