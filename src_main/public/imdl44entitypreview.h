#ifndef IMDL44ENTITYPREVIEW_H
#define IMDL44ENTITYPREVIEW_H

// Optional experimental renderer; model-to-world is a row-major 3x4 matrix.
#define MDL44_ENTITY_PREVIEW_VERSION "Mdl44EntityPreview006"
struct model_t;
struct Studio44Layer { int sequence; float cycle, weight; };
class IMdl44EntityPreview
{
public:
    virtual bool Draw(const model_t *model, const char *name, const float *transform, unsigned int skin, float time, int sequence, float cycle, const Studio44Layer *layers, unsigned int layerCount, const float *flexWeights, unsigned int flexCount, const float *viewTarget) = 0;
    virtual bool RigidBounds(const char *name, float *mins, float *maxs) = 0;
};
extern IMdl44EntityPreview *g_pMdl44EntityPreview;
#endif
