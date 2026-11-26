//========= Copyright © 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "OptionsDialog.h"

#include <vgui_controls/RadioButton.h>
#include <KeyValues.h>
#include <vgui_controls/Label.h>

#include <vgui_controls/PropertySheet.h>

#include "OptionsSubKeyboard.h"
#include "OptionsSubMouse.h"
#include "OptionsSubAudio.h"
#include "OptionsSubVideo.h"
#include "OptionsSubVoice.h"
#include "OptionsSubAdvanced.h"
#include "OptionsSubMultiplayer.h"
#include "MultiplayerAdvancedDialog.h"
#include "ModInfo.h"
#include "EngineInterface.h"
#include <string.h>

using namespace vgui;

#include "Taskbar.h"
extern CTaskbar *g_pTaskbar;
#include <vgui/ILocalize.h>

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

static bool UseHalfLife2Options()
{
	const char *gamePath = engine->GetGameDirectory();
	const char *separator = strrchr( gamePath, '/' );
	if ( !separator )
		separator = strrchr( gamePath, '\\' );

	const char *gameName = separator ? separator + 1 : gamePath;
	return !_stricmp( gameName, "hl2" ) && !ModInfo().IsMultiplayerOnly()
		&& ( !engine->IsInGame() || engine->GetMaxClients() <= 1 );
}

class CDifficultyPage : public PropertyPage
{
public:
	CDifficultyPage( Panel *parent ) : PropertyPage( parent, "Difficulty" )
	{
		m_selectedSkill = m_appliedSkill = 1;

		Label *heading = new Label( this, "DifficultyHeading", "#OptionsUI_DifficultyHeading" );
		heading->SetBounds( 28, 22, 440, 22 );
		heading->SetPaintBackgroundEnabled( false );

		const char *const levels[] = { "#OptionsUI_Easy", "#OptionsUI_Normal", "#OptionsUI_Hard" };
		const char *const descriptions[] = {
			"#OptionsUI_EasyDescription",
			"#OptionsUI_NormalDescription",
			"#OptionsUI_HardDescription"
		};

		for ( int i = 0; i < ARRAYSIZE( m_choices ); ++i )
		{
			const int y = 58 + i * 58;
			RadioButton *choice = new RadioButton( this, levels[i] + 1, levels[i] );
			choice->SetBounds( 28, y, 172, 22 );
			choice->SetTabPosition( 1 );
			choice->SetSubTabPosition( i + 1 );
			choice->SetSelected( false );
			choice->SetPaintBackgroundEnabled( false );
			choice->AddActionSignalTarget( this );
			m_choices[i] = choice;

			Label *description = new Label( this, descriptions[i] + 1, descriptions[i] );
			description->SetBounds( 48, y + 19, 422, 21 );
			description->SetPaintBackgroundEnabled( false );
		}
	}

	DECLARE_PANELMAP();

protected:
	virtual void OnResetData()
	{
		const ConVar *skill = cvar->FindVar( "skill" );
		m_appliedSkill = skill ? max( 1, min( skill->GetInt(), ARRAYSIZE( m_choices ) ) ) : 1;
		m_selectedSkill = m_appliedSkill;
		m_choices[m_selectedSkill - 1]->SetSelected( true );
	}

	virtual void OnApplyChanges()
	{
		if ( m_selectedSkill == m_appliedSkill )
			return;

		ConVar *skill = const_cast<ConVar *>( cvar->FindVar( "skill" ) );
		if ( skill )
		{
			skill->SetValue( m_selectedSkill );
			m_appliedSkill = m_selectedSkill;
		}
	}

	void OnDifficultySelected( Panel *choice )
	{
		for ( int i = 0; i < ARRAYSIZE( m_choices ); ++i )
		{
			if ( choice == m_choices[i] && m_selectedSkill != i + 1 )
			{
				m_selectedSkill = i + 1;
				PostActionSignal( new KeyValues( "ApplyButtonEnable" ) );
				return;
			}
		}
	}

private:
	typedef PropertyPage BaseClass;

	RadioButton *m_choices[3];
	int m_appliedSkill;
	int m_selectedSkill;
};

MessageMapItem_t CDifficultyPage::m_MessageMap[] =
{
	MAP_MESSAGE_PTR( CDifficultyPage, "RadioButtonChecked", OnDifficultySelected, "panel" ),
};
IMPLEMENT_PANELMAP( CDifficultyPage, BaseClass );

//-----------------------------------------------------------------------------
// Purpose: Builds the settings dialog for the current game.
//-----------------------------------------------------------------------------
COptionsDialog::COptionsDialog( Panel *parent )
	: PropertyDialog( parent, "OptionsDialog" ),
	  m_useSinglePlayerLayout( UseHalfLife2Options() )
{
	SetBounds(0, 0, 512, 406);
	SetSizeable( false );
	g_pTaskbar->AddTask( GetVPanel() );

	const bool hl2Options = m_useSinglePlayerLayout;
	SetTitle( hl2Options ? "#OptionsUI_Title" : "#GameUI_Options", true );
	SetOKButtonText( hl2Options ? "#OptionsUI_OK" : "#GameUI_OK" );
	SetCancelButtonText( hl2Options ? "#OptionsUI_Cancel" : "#GameUI_Cancel" );
	SetApplyButtonText( hl2Options ? "#OptionsUI_Apply" : "#GameUI_Apply" );
	SetApplyButtonVisible( true );

	if ( hl2Options )
	{
		SetPaintBorderEnabled( false );
		SetMenuButtonVisible( false );
		GetPropertySheet()->SetTabWidth( 66 );
		AddPage( new CDifficultyPage( this ), "#OptionsUI_Difficulty" );
	}
	else if ( ModInfo().IsMultiplayerOnly() )
	{
		AddPage( new COptionsSubMultiplayer( this ), "#GameUI_Multiplayer" );
	}

	AddPage( new COptionsSubKeyboard( this ), hl2Options ? "#OptionsUI_Keyboard" : "#GameUI_Keyboard" );
	AddPage( new COptionsSubMouse( this ), hl2Options ? "#OptionsUI_Mouse" : "#GameUI_Mouse" );
	AddPage( new COptionsSubAudio( this ), hl2Options ? "#OptionsUI_Audio" : "#GameUI_Audio" );
	AddPage( new COptionsSubVideo( this ), hl2Options ? "#OptionsUI_Video" : "#GameUI_Video" );

	if ( hl2Options )
	{
		GetPropertySheet()->SetPaintBackgroundEnabled( false );
		for ( int i = 0; i < GetPropertySheet()->GetNumPages(); ++i )
			GetPropertySheet()->GetPage( i )->SetPaintBackgroundEnabled( false );
	}
	else
	{
		AddPage( new COptionsSubVoice( this ), "#GameUI_Voice" );
		AddPage( new COptionsSubAdvanced( this ), "#GameUI_Advanced" );

		if ( ModInfo().IsSinglePlayerOnly() )
			AddPage( new CMultiplayerAdvancedPage( this ), "#GameUI_AdvancedNoEllipsis" );
		else if ( !ModInfo().IsMultiplayerOnly() )
			AddPage( new COptionsSubMultiplayer( this ), "#GameUI_Multiplayer" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
COptionsDialog::~COptionsDialog()
{
}

//-----------------------------------------------------------------------------
// Purpose: Overrides the base class so it can setup the taskbar title properly
//-----------------------------------------------------------------------------
void COptionsDialog::SetTitle(const char *title, bool surfaceTitle)
{
	BaseClass::SetTitle(title,surfaceTitle);
	if (g_pTaskbar)
	{
		wchar_t *wTitle;
		wchar_t w_szTitle[1024];

		wTitle = vgui::localize()->Find(title);

		if(!wTitle)
		{
			vgui::localize()->ConvertANSIToUnicode(title,w_szTitle,sizeof(w_szTitle));
			wTitle = w_szTitle;
		}

		g_pTaskbar->SetTitle(GetVPanel(), wTitle);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Brings the dialog to the fore
//-----------------------------------------------------------------------------
void COptionsDialog::Activate()
{
	BaseClass::Activate();
	EnableApplyButton(false);
}

//-----------------------------------------------------------------------------
// Purpose: Opens the dialog
//-----------------------------------------------------------------------------
void COptionsDialog::Run()
{
	Activate();
}

void COptionsDialog::GetClientArea( int &x, int &y, int &wide, int &tall )
{
	BaseClass::GetClientArea( x, y, wide, tall );
	if ( m_useSinglePlayerLayout )
	{
		x += 5;
		wide -= 10;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Deletes the dialog on close
//-----------------------------------------------------------------------------
void COptionsDialog::OnClose()
{
	BaseClass::OnClose();
	MarkForDeletion();
}
