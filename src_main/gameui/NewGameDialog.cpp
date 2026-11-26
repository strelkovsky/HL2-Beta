//========= Copyright © 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "NewGameDialog.h"

#include "EngineInterface.h"
#include <vgui_controls/Button.h>
#include <vgui_controls/CheckButton.h>
#include <KeyValues.h>
#include <vgui_controls/Label.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui/ILocalize.h>
#include <vgui/IInput.h>
#include <vgui_controls/Controls.h>
#include <FileSystem.h>
#include <stdlib.h>
#include <string.h>
#include <vgui_controls/RadioButton.h>
#include <stdio.h>
#include "ModInfo.h"

using namespace vgui;

#include "GameUI_Interface.h"
#include "taskframe.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// PC chapter cards adapted from SE2007's NewGameDialog.
class CChapterCard : public vgui::Panel
{
public:
 CChapterCard( CNewGameDialog *parent ) : Panel( parent, "ChapterCard" ), m_owner( parent ), m_index( -1 ), m_texture( -1 ), m_selected( false ), m_unlocked( false )
 {
  SetSize( 172, 146 );
  m_number = new Label( this, "ChapterLabel", "" ); m_number->SetBounds( 0, 4, 172, 20 ); m_number->SetPaintBackgroundEnabled( false ); m_number->SetMouseInputEnabled( false );
  m_name = new Label( this, "ChapterNameLabel", "" ); m_name->SetBounds( 0, 20, 172, 20 ); m_name->SetPaintBackgroundEnabled( false ); m_name->SetMouseInputEnabled( false );
 }
 void SetChapter( int index, const char *file, bool unlocked, bool selected )
 {
  m_index = index; m_unlocked = unlocked; m_selected = selected;
  char id[32]; strncpy( id, file + 7, sizeof(id)-1 ); id[sizeof(id)-1]=0; char *ext = strrchr( id, '.' ); if ( ext ) *ext = 0;
  wchar_t number[32], text[96]; localize()->ConvertANSIToUnicode( id, number, sizeof(number) );
  const wchar_t *chapter = localize()->Find( "#NewGameUI_Chapter" );
  _snwprintf( text, ARRAYSIZE(text)-1, L"%s %s", chapter ? chapter : L"CHAPTER", number ); text[ARRAYSIZE(text)-1]=0; m_number->SetText( text );
  char token[80]; _snprintf( token, sizeof(token), "#ChapterUI_%s", id ); token[sizeof(token)-1]=0; m_name->SetText( unlocked ? token : "" );
  Color col = unlocked ? ( selected ? Color(255,255,255,255) : Color(210,210,210,255) ) : Color(120,120,120,255);
  m_number->SetFgColor( col ); m_name->SetFgColor( col );
  if ( m_texture < 0 ) m_texture = surface()->CreateNewTextureID();
  char material[128]; _snprintf( material, sizeof(material), "vgui/chapters/chapter%s", id ); material[sizeof(material)-1]=0;
  surface()->DrawSetTextureFile( m_texture, material, true, false ); Repaint();
 }
 virtual void PaintBackground()
 {
  surface()->DrawSetColor( m_selected ? Color(255,255,255,255) : Color(110,110,110,255) ); surface()->DrawFilledRect( 0, 40, 172, 146 );
  surface()->DrawSetColor( 255,255,255,m_unlocked ? 255 : 96 ); surface()->DrawSetTexture( m_texture ); surface()->DrawTexturedSubRect( 10, 50, 162, 136, 0.0f, 0.0f, 152.0f / 256.0f, 86.0f / 128.0f );
 }
 virtual void OnMousePressed( MouseCode code ) { if ( m_unlocked ) m_owner->SelectChapter( m_index, false ); }
 virtual void OnMouseDoublePressed( MouseCode code ) { if ( m_unlocked ) m_owner->SelectChapter( m_index, true ); }
private:
 CNewGameDialog *m_owner; Label *m_number, *m_name; int m_index, m_texture; bool m_selected, m_unlocked;
};
static int __cdecl CompareChapterFiles( const void *left, const void *right )
{
 const char *a = (const char *)left, *b = (const char *)right; int x = atoi( a + 7 ), y = atoi( b + 7 );
 if ( x != y ) return x < y ? -1 : 1; return _stricmp( a, b );
}
void CNewGameDialog::CreateChapterDialog()
{
 m_nChapterCount = 0; m_nSelectedChapter = 0; m_nChapterPage = 0;
 FileFindHandle_t handle = FILESYSTEM_INVALID_FIND_HANDLE;
 const char *file = filesystem()->FindFirst( "cfg/chapter*.cfg", &handle );
 while ( file && m_nChapterCount < ARRAYSIZE(m_chapters) )
 {
  char path[80]; _snprintf( path, sizeof(path), "cfg/%s", file ); path[sizeof(path)-1]=0;
  FileHandle_t f = filesystem()->Open( path, "rb", "MOD" );
  if ( f ) { if ( filesystem()->Size( f ) > 0 ) { strncpy( m_chapters[m_nChapterCount], file, 31 ); m_chapters[m_nChapterCount++][31]=0; } filesystem()->Close( f ); }
  file = filesystem()->FindNext( handle );
 }
 if ( handle != FILESYSTEM_INVALID_FIND_HANDLE ) filesystem()->FindClose( handle );
 qsort( m_chapters, m_nChapterCount, sizeof(m_chapters[0]), CompareChapterFiles );
 SetBounds( 0, 0, 600, 296 );
 m_pChapterNext = new Button( this, "Next", "#NewGameUI_Next" ); m_pChapterNext->SetCommand( "Next" );
 m_pChapterPrev = new Button( this, "Prev", "#NewGameUI_Prev" ); m_pChapterPrev->SetCommand( "Prev" );
 m_pChapterPlay = new Button( this, "Play", "#NewGameUI_Start" ); m_pChapterPlay->SetCommand( "Play" );
 Button *cancel = new Button( this, "Cancel", "#GameUI_Cancel" ); cancel->SetCommand( "Close" );
 LoadControlSettings( "Resource/NewGameChapters.res" );
 SetTitle( "#NewGameUI_Title", true );
 for ( int i=0; i<3; ++i ) { m_cards[i] = new CChapterCard( this ); m_cards[i]->SetPos( 24 + i * 188, 42 ); }
 RefreshChapters();
}
void CNewGameDialog::RefreshChapters()
{
 const ConVar *unlock = cvar->FindVar( "sv_unlockedchapters" );
 // This branch predates chapter progress tracking; keep chapters usable when the variable is absent.
 int unlocked = unlock ? max( 1, unlock->GetInt() ) : m_nChapterCount;
 for ( int i=0; i<3; ++i ) { int index = m_nChapterPage * 3 + i; m_cards[i]->SetVisible( index < m_nChapterCount ); if ( index < m_nChapterCount ) m_cards[i]->SetChapter( index, m_chapters[index], index < unlocked, index == m_nSelectedChapter ); }
 m_pChapterPrev->SetVisible( m_nChapterPage > 0 ); m_pChapterNext->SetVisible( (m_nChapterPage + 1) * 3 < m_nChapterCount );
 m_pChapterPlay->SetEnabled( m_nSelectedChapter >= 0 && m_nSelectedChapter < m_nChapterCount && m_nSelectedChapter < unlocked );
}
void CNewGameDialog::SelectChapter( int index, bool play )
{
 const ConVar *unlock = cvar->FindVar( "sv_unlockedchapters" );
 if ( index < 0 || index >= m_nChapterCount || (unlock && index >= max( 1, unlock->GetInt() )) ) return;
 m_nSelectedChapter = index; RefreshChapters(); if ( play ) OnCommand( "Play" );
}

//-----------------------------------------------------------------------------
// Purpose: Basic help dialog
//-----------------------------------------------------------------------------
CNewGameDialog::CNewGameDialog(vgui::Panel *parent) : CTaskFrame(parent, "NewGameDialog")
{
	SetBounds(0, 0, 372, 160);
	SetSizeable( false );

	MakePopup();
	g_pTaskbar->AddTask(GetVPanel());

	//vgui::surface()->CreatePopup( GetVPanel(), false );

	SetTitle("#GameUI_NewGame", true);
 const char *path = engine->GetGameDirectory(); const char *name = strrchr( path, '/' ); const char *backslash = strrchr( path, '\\' );
 if ( !name || (backslash && backslash > name) ) name = backslash;
 m_bChapterMode = !_stricmp( name ? name + 1 : path, "hl2" );
 if ( m_bChapterMode ) { CreateChapterDialog(); return; }
	new vgui::Label( this, "HelpText", "#GameUI_NewGameHelpText" );

	// radio buttons to tab between controls
	m_pTraining = new vgui::RadioButton(this, "Training", "#GameUI_TrainingRoom");
	m_pTraining->SetSelected( true );
	m_nPlayMode = 0;
	m_pEasy = new vgui::RadioButton(this, "Easy", "#GameUI_Easy");
	m_pMedium = new vgui::RadioButton(this, "Medium", "#GameUI_Medium");
	m_pHard = new vgui::RadioButton(this, "Hard", "#GameUI_Hard");

	vgui::Button *play = new vgui::Button( this, "Play", "#GameUI_Play" );
	play->SetCommand( "Play" );

	vgui::Button *cancel = new vgui::Button( this, "Cancel", "#GameUI_Cancel" );
	cancel->SetCommand( "Close" );

	LoadControlSettings("Resource\\NewGameDialog.res");
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CNewGameDialog::~CNewGameDialog()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *command - 
//-----------------------------------------------------------------------------
void CNewGameDialog::OnCommand( const char *command )
{
	if ( m_bChapterMode )
 {
  if ( !stricmp( command, "Next" ) ) { if ( (m_nChapterPage+1)*3 < m_nChapterCount ) { ++m_nChapterPage; m_nSelectedChapter = -1; SelectChapter( m_nChapterPage * 3, false ); RefreshChapters(); } return; }
  if ( !stricmp( command, "Prev" ) ) { if ( m_nChapterPage > 0 ) { --m_nChapterPage; m_nSelectedChapter = -1; SelectChapter( m_nChapterPage * 3, false ); RefreshChapters(); } return; }
  if ( !stricmp( command, "Play" ) )
  {
   const ConVar *unlock = cvar->FindVar( "sv_unlockedchapters" );
   if ( m_nSelectedChapter >= 0 && m_nSelectedChapter < m_nChapterCount && (!unlock || m_nSelectedChapter < max(1, unlock->GetInt())) )
   { char cmd[256]; _snprintf( cmd, sizeof(cmd), "disconnect\nmaxplayers 1\ndeathmatch 0\nexec %s\n", m_chapters[m_nSelectedChapter] ); cmd[sizeof(cmd)-1]=0; engine->ClientCmd( cmd ); OnClose(); }
   return;
  }
 }
 if ( !stricmp( command, "Play" ) )
	{
		// Determine mode
		m_nPlayMode = 0;

		if ( m_pEasy->IsSelected() )
		{
			m_nPlayMode = 1;
		}
		else if ( m_pMedium->IsSelected() )
		{
			m_nPlayMode = 2;
		}
		else if ( m_pHard->IsSelected() )
		{
			m_nPlayMode = 3;
		}

		char mapcommand[ 512 ];
		mapcommand[ 0 ] = 0;

		// Find appropriate key
		if ( m_nPlayMode == 0 )
		{
			sprintf(mapcommand, "disconnect\nmaxplayers 1\ndeathmatch 0\nmap %s\n", ModInfo().GetTrainMap() );
		}
		else
		{
			sprintf(mapcommand, "disconnect\nmaxplayers 1\ndeathmatch 0\nskill %i\nmap %s\n", m_nPlayMode, ModInfo().GetStartMap() );
		}

		engine->ClientCmd( mapcommand );
		OnClose();
	}
	else
	{
		BaseClass::OnCommand( command );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNewGameDialog::OnClose()
{
	BaseClass::OnClose();
	MarkForDeletion();
}

