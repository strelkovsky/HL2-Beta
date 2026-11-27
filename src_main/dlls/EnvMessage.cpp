//========= Copyright © 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose: Implements visual effects entities: sprites, beams, bubbles, etc.
//
// $NoKeywords: $
//=============================================================================

#include "cbase.h"
#include "EnvMessage.h"
#include "engine/IEngineSound.h"


LINK_ENTITY_TO_CLASS( env_message, CMessage );

BEGIN_DATADESC( CMessage )

	DEFINE_KEYFIELD( CMessage, m_iszMessage, FIELD_STRING, "message" ),
	DEFINE_KEYFIELD( CMessage, m_sNoise, FIELD_SOUNDNAME, "messagesound" ),
	DEFINE_KEYFIELD( CMessage, m_MessageAttenuation, FIELD_INTEGER, "messageattenuation" ),
	DEFINE_KEYFIELD( CMessage, m_MessageVolume, FIELD_FLOAT, "messagevolume" ),

	DEFINE_FIELD( CMessage, m_Radius, FIELD_FLOAT ),

	DEFINE_INPUTFUNC( CMessage, FIELD_VOID, "ShowMessage", InputShowMessage ),

	DEFINE_OUTPUT(CMessage, m_OnShowMessage, "OnShowMessage"),

END_DATADESC()



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CMessage::Spawn( void )
{
	Precache();

	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );

	switch( m_MessageAttenuation )
	{
	case 1: // Medium radius
		m_Radius = ATTN_STATIC;
		break;
	
	case 2:	// Large radius
		m_Radius = ATTN_NORM;
		break;

	case 3:	//EVERYWHERE
		m_Radius = ATTN_NONE;
		break;
	
	default:
	case 0: // Small radius
		m_Radius = SNDLVL_IDLE;
		break;
	}
	m_MessageAttenuation = 0;

	// Remap volume from [0,10] to [0,1].
	m_MessageVolume *= 0.1;

	// No volume, use normal
	if ( m_MessageVolume <= 0 )
	{
		m_MessageVolume = 1.0;
	}
}


void CMessage::Precache( void )
{
	if ( m_sNoise != NULL_STRING )
		enginesound->PrecacheSound( STRING(m_sNoise) );
}


//-----------------------------------------------------------------------------
// Purpose: Input handler for showing the message and/or playing the sound.
//-----------------------------------------------------------------------------
void CMessage::InputShowMessage( inputdata_t &inputdata )
{
	CBaseEntity *pPlayer = NULL;

	if ( m_spawnflags & SF_MESSAGE_ALL )
	{
		UTIL_ShowMessageAll( STRING( m_iszMessage ) );
	}
	else
	{
		if ( inputdata.pActivator && inputdata.pActivator->IsPlayer() )
		{
			pPlayer = inputdata.pActivator;
		}
		else
		{
			pPlayer = CBaseEntity::Instance( engine->PEntityOfEntIndex( 1 ) );
		}

		if ( pPlayer )
		{
			UTIL_ShowMessage( STRING( m_iszMessage ), pPlayer );
		}
	}

	if ( m_sNoise != NULL_STRING )
	{
		CPASAttenuationFilter filter( this );

		EmitSound( filter, entindex(), CHAN_BODY, STRING(m_sNoise), m_MessageVolume, m_Radius );
	}

	if ( m_spawnflags & SF_MESSAGE_ONCE )
	{
		UTIL_Remove( this );
	}

	m_OnShowMessage.FireOutput( inputdata.pActivator, this );
}


void CMessage::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	inputdata_t inputdata;

	inputdata.pActivator	= NULL;
	inputdata.pCaller		= NULL;

	InputShowMessage( inputdata );
}


// Retail maps use env_credits to start the opening logo and credit rolls.
// The logo timing and messages follow SE2007; credit rolls can be added to
// the client independently of this entity.
class CCredits : public CPointEntity
{
    DECLARE_CLASS( CCredits, CPointEntity );
public:
    DECLARE_DATADESC();
    CCredits() : m_flLogoLength(0) {}
    void Spawn() { SetSolid(SOLID_NONE); SetMoveType(MOVETYPE_NONE); }
    void InputShowLogo( inputdata_t &inputdata );
    void InputSetLogoLength( inputdata_t &inputdata ) { m_flLogoLength=inputdata.value.Float(); }
    void InputRollCredits( inputdata_t &inputdata );
    void InputRollOutroCredits( inputdata_t &inputdata );
private:
    float m_flLogoLength;
    COutputEvent m_OnCreditsDone;
};
LINK_ENTITY_TO_CLASS( env_credits, CCredits );
BEGIN_DATADESC( CCredits )
    DEFINE_FIELD( CCredits, m_flLogoLength, FIELD_FLOAT ),
    DEFINE_OUTPUT( CCredits, m_OnCreditsDone, "OnCreditsDone" ),
    DEFINE_INPUTFUNC( CCredits, FIELD_VOID, "ShowLogo", InputShowLogo ),
    DEFINE_INPUTFUNC( CCredits, FIELD_FLOAT, "SetLogoLength", InputSetLogoLength ),
    DEFINE_INPUTFUNC( CCredits, FIELD_VOID, "RollCredits", InputRollCredits ),
    DEFINE_INPUTFUNC( CCredits, FIELD_VOID, "RollOutroCredits", InputRollOutroCredits ),
END_DATADESC()
void CCredits::InputShowLogo( inputdata_t &inputdata )
{
    CBasePlayer *player=UTIL_PlayerByIndex(1); if(!player) return;
    CSingleUserRecipientFilter recipient(player); recipient.MakeReliable();
    if (m_flLogoLength > 0) {
        UserMessageBegin(recipient,"LogoTimeMsg"); WRITE_FLOAT(m_flLogoLength); MessageEnd();
    } else {
        UserMessageBegin(recipient,"CreditsMsg"); WRITE_BYTE(1); MessageEnd();
    }
}
void CCredits::InputRollCredits( inputdata_t &inputdata )
{
    CBasePlayer *player=UTIL_PlayerByIndex(1); if(!player) return;
    CSingleUserRecipientFilter recipient(player); recipient.MakeReliable();
    UserMessageBegin(recipient,"CreditsMsg"); WRITE_BYTE(2); MessageEnd();
}
void CCredits::InputRollOutroCredits( inputdata_t &inputdata )
{
    CBasePlayer *player=UTIL_PlayerByIndex(1); if(!player) return;
    CSingleUserRecipientFilter recipient(player); recipient.MakeReliable();
    UserMessageBegin(recipient,"CreditsMsg"); WRITE_BYTE(3); MessageEnd();
}
