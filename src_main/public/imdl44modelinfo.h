// Experimental MDL 44 metadata. Indices belong only to this interface and
// must not be passed to the legacy studiohdr_t APIs.
#ifndef IMDL44MODELINFO_H
#define IMDL44MODELINFO_H
#include "studio44.h"
#include "imdl44entitypreview.h"
struct model_t;
#define MDL44_MODEL_INFO_VERSION "Mdl44ModelInfo006"
class IMdl44ModelInfo
{
public:
    virtual bool HasModel(const model_t *model) const = 0;
    // Copies the validated v44 on-disk header prefix, not a legacy v37 header.
    virtual bool GetStudioHeader(const model_t *model, Studio44::studiohdr_t &out) const = 0;
    // The returned name remains valid only until the model is unloaded.
    virtual const char *OriginalName(const model_t *model) const = 0;
    virtual int SequenceCount(const model_t *model) const = 0;
    virtual int FindSequence(const model_t *model, const char *name) const = 0;
    virtual const char *SequenceName(const model_t *model, int sequence) const = 0;
    // Duration is available only for sequences with one animation; unsupported
    // pose-parameter blends return false instead of a guessed value.
    virtual bool SequenceInfo(const model_t *model, int sequence, int &flags, float &duration) const = 0;
    virtual int FlexControllerCount(const model_t *model) const = 0;
    virtual bool FlexController(const model_t *model, int index, const char *&name, const char *&type, float &minimum, float &maximum) const = 0;
    virtual int FindAttachment(const model_t *model, const char *name) const = 0;
    // Model-space attachment transform with the supported base pose and overlays.
    virtual bool AttachmentTransform(const model_t *model, int attachment, float time, const Studio44Layer *layers, unsigned int layerCount, float *matrix3x4) const = 0;
};
#endif
