//========= Copyright © 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose: Fires an output when the map spawns (or respawns if not set to 
//			only fire once). It can be set to check a global state before firing.
//
// $NoKeywords: $
//=============================================================================

#include "cbase.h"
#include "entityinput.h"
#include "entityoutput.h"
#include "eventqueue.h"
#include "mathlib.h"
#include "globalstate.h"
#include "script_intro.h"
#include "vstdlib/icommandline.h"


// Debug shortcut for repeated testing of the retail opening map.
static ConVar sv_skip_gman_intro( "sv_skip_gman_intro", "0", 0, "Skip the G-Man intro on a new d1_trainstation_01 game." );

const int SF_AUTO_FIREONCE		= 0x01;
const int SF_AUTO_FIREONRELOAD	= 0x02;


class CLogicAuto : public CBaseEntity
{
public:
	DECLARE_CLASS( CLogicAuto, CBaseEntity );

	void Activate(void);
	void Think(void);

	int ObjectCaps(void) { return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	DECLARE_DATADESC();

private:

	// fired no matter why the map loaded
	COutputEvent m_OnMapSpawn;

	// fired for specified types of map loads
	COutputEvent m_OnNewGame;
	COutputEvent m_OnLoadGame;
	COutputEvent m_OnMapTransition;
	COutputEvent m_OnBackgroundMap;

	string_t m_globalstate;
};

LINK_ENTITY_TO_CLASS(logic_auto, CLogicAuto);


BEGIN_DATADESC( CLogicAuto )

	DEFINE_KEYFIELD(CLogicAuto, m_globalstate, FIELD_STRING, "globalstate"),

	// Outputs
	DEFINE_OUTPUT(CLogicAuto, m_OnMapSpawn, "OnMapSpawn"),
	DEFINE_OUTPUT(CLogicAuto, m_OnNewGame, "OnNewGame"),
	DEFINE_OUTPUT(CLogicAuto, m_OnLoadGame, "OnLoadGame"),
	DEFINE_OUTPUT(CLogicAuto, m_OnMapTransition, "OnMapTransition"),
	DEFINE_OUTPUT(CLogicAuto, m_OnBackgroundMap, "OnBackgroundMap"),

END_DATADESC()


//------------------------------------------------------------------------------
// Purpose : Fire my outputs here if I fire on map reload
//------------------------------------------------------------------------------
void CLogicAuto::Activate(void)
{
	BaseClass::Activate();
	// An immediate OnNewGame fade is authored to cover the opening frame.
	// Pre-arm only a matching script_intro Activate + opaque FadeTo pair;
	// the actual outputs still fire normally at the SE2007 0.2s delay.
	if ( gpGlobals->eLoadType == MapLoad_NewGame &&
		 (!m_globalstate || GlobalEntity_GetState(m_globalstate) == GLOBAL_ON) )
	{
		for ( const CEventAction *fade=m_OnNewGame.GetFirstAction(); fade; fade=fade->m_pNext )
		{
			if ( fade->m_flDelay>0 || fade->m_nTimesToFire==0 || fade->m_iTarget==NULL_STRING ||
				 fade->m_iTargetInput==NULL_STRING || Q_stricmp(STRING(fade->m_iTargetInput),"FadeTo") ) continue;
			float alpha=0, duration=0;
			if ( fade->m_iParameter==NULL_STRING || sscanf(STRING(fade->m_iParameter),"%f %f",&alpha,&duration)!=2 ||
				 alpha<255.0f || duration<0 || duration>0.01f ) continue;
			bool activates=false;
			for ( const CEventAction *action=m_OnNewGame.GetFirstAction(); action; action=action->m_pNext )
				if ( action->m_flDelay<=0 && action->m_nTimesToFire!=0 && action->m_iTarget!=NULL_STRING &&
					 action->m_iTargetInput!=NULL_STRING && !Q_stricmp(STRING(action->m_iTargetInput),"Activate") &&
					 !Q_stricmp(STRING(action->m_iTarget),STRING(fade->m_iTarget)) ) { activates=true; break; }
			if (!activates) continue;
			CBaseEntity *target=gEntList.FindEntityByName(NULL,fade->m_iTarget,NULL);
			if (target && FClassnameIs(target,"script_intro"))
				static_cast<CScriptIntro *>(target)->PrepareStartupFade(alpha,duration);
		}
	}
	SetNextThink( gpGlobals->curtime + 0.2 );
}


//-----------------------------------------------------------------------------
// Purpose: Called shortly after level spawn. Checks the global state and fires
//			targets if the global state is set or if there is not global state
//			to check.
//-----------------------------------------------------------------------------
void CLogicAuto::Think(void)
{
	if (!m_globalstate || GlobalEntity_GetState(m_globalstate) == GLOBAL_ON)
	{
		if (gpGlobals->eLoadType == MapLoad_Transition)
		{
			m_OnMapTransition.FireOutput(NULL, this);
		}
		else if (gpGlobals->eLoadType == MapLoad_NewGame)
		{
	            bool skipIntro = false;
            if ( !Q_stricmp( STRING(gpGlobals->mapname), "d1_trainstation_01" ) &&
                 (sv_skip_gman_intro.GetBool() || CommandLine()->FindParm("-skip_gman_intro")) )
            {
                bool hasIntro = false, hasScene = false;
                for ( const CEventAction *action = m_OnNewGame.GetFirstAction(); action; action = action->m_pNext )
                {
                    if (action->m_iTarget == NULL_STRING || action->m_iTargetInput == NULL_STRING || action->m_nTimesToFire == 0) continue;
                    const char *target = STRING(action->m_iTarget), *input = STRING(action->m_iTargetInput);
                    if (!Q_stricmp(target, "intro_version3") && !Q_stricmp(input, "Activate")) hasIntro = true;
                    if (!Q_stricmp(target, "scene1_start") && !Q_stricmp(input, "Trigger")) hasScene = true;
                }
                skipIntro = hasIntro && hasScene && gEntList.FindEntityByName(NULL, "logic_start_train", NULL);
            }
            if (skipIntro)
            {
                variant_t empty;
                g_EventQueue.AddEvent("intro_version3", "Activate", empty, 0, NULL, this);
                g_EventQueue.AddEvent("fog_gman", "TurnOff", empty, 0, NULL, this);
                g_EventQueue.AddEvent("fog_train", "TurnOn", empty, 0, NULL, this);
                g_EventQueue.AddEvent("logic_killme_1", "Trigger", empty, 0, NULL, this);
                g_EventQueue.AddEvent("logic_killme_2", "Trigger", empty, 0, NULL, this);
                g_EventQueue.AddEvent("logic_killme_3", "Trigger", empty, 0, NULL, this);
                g_EventQueue.AddEvent("logic_start_train", "Trigger", empty, 0, NULL, this);
                Msg("Skipping G-Man intro; starting train ride.\n");
            }
            else
                m_OnNewGame.FireOutput(NULL, this);
		}
		else if (gpGlobals->eLoadType == MapLoad_LoadGame)
		{
			m_OnLoadGame.FireOutput(NULL, this);
		}

		else if (gpGlobals->eLoadType == MapLoad_Background)
		{
			m_OnBackgroundMap.FireOutput(NULL, this);
		}

		m_OnMapSpawn.FireOutput(NULL, this);

		if (m_spawnflags & SF_AUTO_FIREONCE)
		{
			UTIL_Remove(this);
		}
	}
}

