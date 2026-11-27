// Experimental native MDL 44 resources owned by a map's static-prop dictionary.
#ifndef ENGINE_MDL44_MAPCACHE_H
#define ENGINE_MDL44_MAPCACHE_H
struct Mdl44MapCache;
enum Mdl44DrawPass { MDL44_DRAW_ALL, MDL44_DRAW_OPAQUE, MDL44_DRAW_TRANSLUCENT };
Mdl44MapCache *Mdl44MapCacheCreate( const char *name );
bool Mdl44MapCacheCreateGpu( Mdl44MapCache *cache );
bool Mdl44MapCacheReleaseGpu( Mdl44MapCache *cache );
void Mdl44MapCacheDestroy( Mdl44MapCache *cache );
void Mdl44MapCacheReleaseLevelGpu();
void Mdl44MapCacheRestoreLevelGpu();
bool Mdl44MapCacheBounds( const Mdl44MapCache *cache, float *mins, float *maxs, float *illumination );
bool Mdl44MapCacheDraw( Mdl44MapCache *cache, const float *transform, unsigned int skin, float alpha, Mdl44DrawPass pass );
bool Mdl44MapCacheCanDraw( const Mdl44MapCache *cache, unsigned int skin );
bool Mdl44MapCacheHasOpaque( const Mdl44MapCache *cache );
bool Mdl44MapCacheHasTranslucent( const Mdl44MapCache *cache );
#endif
