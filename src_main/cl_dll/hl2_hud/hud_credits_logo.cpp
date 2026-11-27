// Opening logo and train-ride credits, based on the SE2007 HUD.
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

struct IntroCreditLine
{
    char name[256], font[256];
    float x, y, start, end, extra;
    int slot;
    bool active;
};

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
    void StartIntroCredits();
    void DrawIntroCredits();
    float m_start, m_hold;
    enum { MODE_NONE, MODE_LOGO, MODE_INTRO } m_mode;
    CUtlVector<IntroCreditLine> m_lines;
    float m_fadeIn, m_fadeOut, m_fadeHold, m_nextStart, m_wavePause, m_logoTime;
    Color m_creditColor;
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
void CHudCreditsLogo::Reset() { m_mode=MODE_NONE; m_lines.RemoveAll(); SetActive(false); SetVisible(false); }
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
    m_mode=MODE_LOGO; m_lines.RemoveAll();
    m_start=gpGlobals->curtime; m_hold=hold>0 ? hold : 5.0f;
    DevMsg(1,"env_credits: showing intro logo, hold %.2f seconds\n",m_hold);
    int width,height; vgui::surface()->GetScreenSize(width,height);
    SetBounds(0,0,width,height); SetActive(true); SetVisible(true);
}
void CHudCreditsLogo::MsgFunc_CreditsMsg(const char *name,int size,void *data)
{
    BEGIN_READ(data,size); const int type=READ_BYTE();
    if(type==1)
    {
        // The 2004 opening map starts revealing the first scene ten seconds
        // after ShowLogo. Finish the two-second fade before that reveal.
        const char *level=engine->GetLevelName();
        const bool opening=level && (!Q_stricmp(level,"maps/d1_trainstation_01.bsp") || !Q_stricmp(level,"d1_trainstation_01"));
        StartLogo(opening ? 3.0f : 5.0f);
    }
    else if(type==2) StartIntroCredits();
    else DevMsg("env_credits: credit roll type %d is not implemented yet\n",type);
}
void CHudCreditsLogo::MsgFunc_LogoTimeMsg(const char *name,int size,void *data)
{
    BEGIN_READ(data,size); StartLogo(READ_FLOAT());
}
void CHudCreditsLogo::Paint()
{
    if(!IsActive()) return;
    if(m_mode==MODE_INTRO) { DrawIntroCredits(); return; }
    if(m_mode!=MODE_LOGO) return;
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


// The release's first three entries form the Valve logo and two blank slots.
// Later entries reuse those three slots with the staggered timing in SE2007.
void CHudCreditsLogo::StartIntroCredits()
{
    Reset();
    KeyValues *credits=new KeyValues("CreditsFile");
    if(!credits->LoadFromFile(filesystem,"scripts/credits.txt","GAME")) {
        Warning("env_credits: cannot load scripts/credits.txt\n"); credits->deleteThis(); return;
    }
    KeyValues *params=credits->FindKey("CreditsParams");
    KeyValues *names=credits->FindKey("IntroCreditsNames");
    if(!params || !names) {
        Warning("env_credits: missing intro names or parameters in scripts/credits.txt\n");
        credits->deleteThis(); return;
    }
    m_fadeIn=params->GetFloat("fadeintime",1);
    m_fadeOut=params->GetFloat("fadeouttime",2);
    m_fadeHold=params->GetFloat("fadeholdtime",3);
    m_nextStart=params->GetFloat("nextfadetime",2);
    m_wavePause=params->GetFloat("pausebetweenwaves",2);
    const float logoDelay=params->GetFloat("logotime",2);
    const float x=params->GetFloat("posx",2), y=params->GetFloat("posy",2);
    m_creditColor=params->GetColor("color");
    vgui::IScheme *scheme=vgui::scheme()->GetIScheme(GetScheme());
    if(!scheme) { credits->deleteThis(); return; }
    int slot=0;
    for(KeyValues *entry=names->GetFirstSubKey();entry;entry=entry->GetNextKey()) {
        IntroCreditLine line={};
        Q_strncpy(line.name,entry->GetName(),sizeof(line.name));
        Q_strncpy(line.font,entry->GetString(NULL,"Default"),sizeof(line.font));
        const vgui::HFont font=scheme->GetFont(line.font,false);
        line.x=x; line.y=y+slot*vgui::surface()->GetFontTall(font); line.slot=slot;
        if(m_lines.Count()<3) {
            line.active=true; line.start=gpGlobals->curtime;
            line.end=line.start+m_fadeIn+m_fadeOut+m_fadeHold;
            m_logoTime=line.end+logoDelay;
        }
        m_lines.AddToTail(line); slot=(slot+1)%3;
    }
    credits->deleteThis();
    if(!m_lines.Count()) return;
    int width,height; vgui::surface()->GetScreenSize(width,height);
    SetBounds(0,0,width,height); m_mode=MODE_INTRO; SetActive(true); SetVisible(true);
    DevMsg(1,"env_credits: starting %d train-ride credits\n",m_lines.Count());
}

void CHudCreditsLogo::DrawIntroCredits()
{
    vgui::IScheme *scheme=vgui::scheme()->GetIScheme(GetScheme());
    if(!scheme) return;
    int width,height; vgui::surface()->GetScreenSize(width,height); SetSize(width,height);
    for(int i=0;i<m_lines.Count();++i) {
        IntroCreditLine &line=m_lines[i]; if(!line.active) continue;
        const vgui::HFont font=scheme->GetFont(line.font,false);
        const float local=gpGlobals->curtime-line.start;
        float alpha=0;
        if(local>=0) {
            if(m_fadeIn>0 && local<m_fadeIn) alpha=local/m_fadeIn;
            else if(local>m_fadeIn+m_fadeHold+line.extra)
                alpha=m_fadeOut>0 ? 1-(local-m_fadeIn-m_fadeHold-line.extra)/m_fadeOut : 0;
            else alpha=1;
        }
        alpha=clamp(alpha,0.0f,1.0f);
        wchar_t text[256]; vgui::localize()->ConvertANSIToUnicode(line.name,text,sizeof(text));
        vgui::surface()->DrawSetTextFont(font);
        vgui::surface()->DrawSetTextColor(m_creditColor[0],m_creditColor[1],m_creditColor[2],(int)(alpha*m_creditColor[3]));
        vgui::surface()->DrawSetTextPos((int)XRES(line.x),(int)YRES(line.y));
        vgui::surface()->DrawPrintText(text,wcslen(text));
        if(gpGlobals->curtime < m_logoTime) continue;
        if(line.end-m_nextStart<=gpGlobals->curtime && m_lines.IsValidIndex(i+3)) {
            IntroCreditLine &next=m_lines[i+3];
            if(next.end==0) {
                next.active=true; next.extra=i<3 ? (float)(i+1) : m_wavePause;
                next.start=gpGlobals->curtime;
                next.end=next.start+m_fadeIn+m_fadeOut+m_fadeHold+next.extra;
                next.slot=line.slot;
                DevMsg(1,"env_credits: %.2f slot %d: %s\n",gpGlobals->curtime,next.slot,next.name);
            }
        }
        if(line.end<=gpGlobals->curtime) {
            line.active=false;
            if(i==m_lines.Count()-1) { Reset(); return; }
        }
    }
}
