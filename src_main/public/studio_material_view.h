// Non-owning access to the material names and search paths of a studio model.
#ifndef STUDIO_MATERIAL_VIEW_H
#define STUDIO_MATERIAL_VIEW_H

#include <cstddef>

class StudioMaterialView
{
public:
    typedef const char *(*Lookup)( const void *context, size_t index );

    StudioMaterialView( const void *context, size_t materials, size_t paths,
                        Lookup materialName, Lookup materialPath )
        : m_context( context ), m_materials( materials ), m_paths( paths ),
          m_materialName( materialName ), m_materialPath( materialPath ) {}

    size_t MaterialCount() const { return m_materials; }
    size_t PathCount() const { return m_paths; }
    const char *MaterialName( size_t i ) const
    {
        return m_materialName && i < m_materials ? m_materialName( m_context, i ) : nullptr;
    }
    const char *Path( size_t i ) const
    {
        return m_materialPath && i < m_paths ? m_materialPath( m_context, i ) : nullptr;
    }
private:
    const void *m_context;
    size_t m_materials, m_paths;
    Lookup m_materialName, m_materialPath;
};
#endif
