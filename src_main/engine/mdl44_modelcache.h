#ifndef ENGINE_MDL44_MODELCACHE_H
#define ENGINE_MDL44_MODELCACHE_H
struct model_t;
// The optional v44 metadata is attached to the lifetime of the engine model.
void Mdl44ModelCacheAttach(model_t *model, const char *name);
void Mdl44ModelCacheDetach(model_t *model);
bool Mdl44ModelCacheHas(const model_t *model);
#endif
