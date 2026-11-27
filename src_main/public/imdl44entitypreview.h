#ifndef IMDL44ENTITYPREVIEW_H
#define IMDL44ENTITYPREVIEW_H

// Optional experimental renderer; model-to-world is a row-major 3x4 matrix.
#define MDL44_ENTITY_PREVIEW_VERSION "Mdl44EntityPreview001"
class IMdl44EntityPreview
{
public:
    virtual bool Draw(const char *name, const float *transform, unsigned int skin, float time) = 0;
};
extern IMdl44EntityPreview *g_pMdl44EntityPreview;
#endif
