// Opening logo from the retail env_credits sequence, based on SE2007.
#include "cbase.h"
#include "hudelement.h"
#include "hud.h"
#include "hud_macros.h"
#include "parsemsg.h"
#include "iclientmode.h"
#include <KeyValues.h>
#include "filesystem.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Panel.h>
#include "tier0/memdbgon.h"

class CHudCreditsLogo : public CHudElement, public vgui::Panel
{
    DECLARE_CLASS_SIMPLE( CHudCreditsLogo, vgui::Panel );
public:
    CHudCreditsLogo( const char *name );
    void Init();
    void Reset();
    void LevelShutdown();
    void MsgFunc_CreditsMsg( const char *name, int size, void *data );
    void MsgFunc_LogoTimeMsg( const char *name, int size, void *data );
    virtual void Paint();
private:
    void StartLogo( float hold );
    float m_start, m_hold;
    char m_logo[256], m_logo2[256];
};
DECLARE_HUDELEMENT( CHudCreditsLogo );
DECLARE_HUD_MESSAGE( CHudCreditsLogo, CreditsMsg );
DECLARE_HUD_MESSAGE( CHudCreditsLogo, LogoTimeMsg );

CHudCreditsLogo::CHudCreditsLogo( const char *name ) : CHudElement(name), BaseClass(NULL,"HudCredits")
{
    SetParent(g_pClientMode->GetViewport());
    SetPaintBackgroundEnabled(false);
    SetPaintBorderEnabled(false);
    SetVisible(false);
}
void CHudCreditsLogo::Init()
{
    HOOK_MESSAGE(CreditsMsg);
    HOOK_MESSAGE(LogoTimeMsg);
    Reset();
}
void CHudCreditsLogo::Reset() { SetActive(false); SetVisible(false); }
void CHudCreditsLogo::LevelShutdown() { Reset(); }
void CHudCreditsLogo::StartLogo(float hold)
{
    Q_strncpy(m_logo,"HALF-LIFE'",sizeof(m_logo)); m_logo2[0]=0;
    KeyValues *credits=new KeyValues("CreditsFile");
    if (credits->LoadFromFile(filesystem,"scripts/credits.txt","GAME")) {
        KeyValues *params=credits->FindKey("CreditsParams");
        if(params) { Q_strncpy(m_logo,params->GetString("logo","HALF-LIFE'"),sizeof(m_logo));
                     Q_strncpy(m_logo2,params->GetString("logo2",""),sizeof(m_logo2)); }
    }
    credits->deleteThis();
    m_start=gpGlobals->curtime; m_hold=hold>0 ? hold : 5.0f;
    DevMsg(1,"env_credits: showing intro logo, hold %.2f seconds\n",m_hold);
    int width,height; vgui::surface()->GetScreenSize(width,height);
    SetBounds(0,0,width,height); SetActive(true); SetVisible(true);
}
void CHudCreditsLogo::MsgFunc_CreditsMsg(const char *name,int size,void *data)
{
    BEGIN_READ(data,size); const int type=READ_BYTE();
    if(type==1) StartLogo(5.0f);
    else DevMsg("env_credits: credit roll type %d is not implemented yet\n",type);
}
void CHudCreditsLogo::MsgFunc_LogoTimeMsg(const char *name,int size,void *data)
{
    BEGIN_READ(data,size); StartLogo(READ_FLOAT());
}
void CHudCreditsLogo::Paint()
{
    if(!IsActive()) return;
    const float elapsed=gpGlobals->curtime-m_start;
    if(elapsed<0 || elapsed>=5.0f+m_hold+2.0f) { Reset(); return; }
    float alpha;
    if(elapsed<5.0f) alpha=clamp(-128.0f+383.0f*elapsed/5.0f,0.0f,255.0f);
    else if(elapsed<5.0f+m_hold) alpha=255.0f;
    else alpha=255.0f*(1.0f-(elapsed-5.0f-m_hold)/2.0f);
    vgui::IScheme *scheme=vgui::scheme()->GetIScheme(GetScheme());
    if(!scheme) return;
    vgui::HFont font=scheme->GetFont("WeaponIcons",false);
    if(!font) return;
    int width,height; vgui::surface()->GetScreenSize(width,height); SetSize(width,height);
    vgui::surface()->DrawSetTextFont(font);
    vgui::surface()->DrawSetTextColor(255,255,255,(int)alpha);
    wchar_t text[256]; int textWidth,textHeight;
    vgui::localize()->ConvertANSIToUnicode(m_logo,text,sizeof(text));
    vgui::surface()->GetTextSize(font,text,textWidth,textHeight);
    const int fontHeight=vgui::surface()->GetFontTall(font);
    vgui::surface()->DrawSetTextPos((width-textWidth)/2,(height-fontHeight)/2);
    vgui::surface()->DrawPrintText(text,wcslen(text));
    if(m_logo2[0]) {
        vgui::localize()->ConvertANSIToUnicode(m_logo2,text,sizeof(text));
        vgui::surface()->GetTextSize(font,text,textWidth,textHeight);
        vgui::surface()->DrawSetTextPos((width-textWidth)/2,(height+fontHeight)/2);
        vgui::surface()->DrawPrintText(text,wcslen(text));
    }
}
