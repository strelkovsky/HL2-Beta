#include "winquake.h"
#include <vfw.h>
#include <sstream>
#include <string>

#include "filesystem.h"
#include "tier0/dbg.h"
#include "vstdlib/ICommandLine.h"

namespace
{
enum class VideoResult
{
    Continue,
    SkipPlaylist,
    Quit
};

std::string GetVideoPath( std::string line )
{
    const size_t comment = line.find( "//" );
    if ( comment != std::string::npos )
        line.erase( comment );

    const size_t first = line.find_first_not_of( " \t\r" );
    if ( first == std::string::npos )
        return {};

    const size_t last = line.find_last_not_of( " \t\r" );
    if ( line[first] == '"' && last > first && line[last] == '"' )
        return line.substr( first + 1, last - first - 1 );

    return line.substr( first, last - first + 1 );
}

VideoResult WaitForVideo( HWND gameWindow, HWND video )
{
    MSG message;
    while ( IsWindow( gameWindow ) && IsWindow( video ) )
    {
        while ( PeekMessage( &message, nullptr, 0, 0, PM_REMOVE ) )
        {
            if ( message.message == WM_QUIT ||
                 ( message.hwnd == gameWindow &&
                   ( message.message == WM_CLOSE ||
                     ( message.message == WM_SYSCOMMAND && ( message.wParam & 0xfff0 ) == SC_CLOSE ) ) ) )
                return VideoResult::Quit;

            if ( ( message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN ) &&
                 message.wParam == VK_ESCAPE )
                return VideoResult::SkipPlaylist;

            TranslateMessage( &message );
            DispatchMessage( &message );
        }

        if ( !IsWindow( video ) || MCIWndGetMode( video, nullptr, 0 ) != MCI_MODE_PLAY )
            break;

        MsgWaitForMultipleObjectsEx( 0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE );
    }
    return IsWindow( gameWindow ) ? VideoResult::Continue : VideoResult::Quit;
}

VideoResult PlayVideo( HWND gameWindow, const char *path )
{
    const DWORD style = WS_CHILD | MCIWNDF_NOPLAYBAR | MCIWNDF_NOMENU |
                        MCIWNDF_NOAUTOSIZEWINDOW | MCIWNDF_NOERRORDLG;
    HWND video = MCIWndCreateA( gameWindow, GetModuleHandle( nullptr ), style, nullptr );
    if ( !video )
    {
        Warning( "Unable to create the startup video window.\n" );
        return VideoResult::SkipPlaylist;
    }

    LONG error = MCIWndOpen( video, path, 0 );
    if ( error )
    {
        Warning( "Unable to open startup video '%s' (MCI error %ld).\n", path, error );
        MCIWndDestroy( video );
        return VideoResult::Continue;
    }

    RECT client = {};
    RECT source = {};
    GetClientRect( gameWindow, &client );
    MCIWndGetSource( video, &source );
    const int sourceWidth = source.right - source.left;
    const int sourceHeight = source.bottom - source.top;
    if ( client.right <= 0 || client.bottom <= 0 || sourceWidth <= 0 || sourceHeight <= 0 )
    {
        MCIWndDestroy( video );
        return VideoResult::Continue;
    }

    int width = client.right;
    int height = MulDiv( width, sourceHeight, sourceWidth );
    if ( height > client.bottom )
    {
        height = client.bottom;
        width = MulDiv( height, sourceWidth, sourceHeight );
    }

    ShowWindow( gameWindow, SW_SHOW );
    if ( HDC dc = GetDC( gameWindow ) )
    {
        FillRect( dc, &client, static_cast<HBRUSH>( GetStockObject( BLACK_BRUSH ) ) );
        ReleaseDC( gameWindow, dc );
    }
    SetWindowPos( video, HWND_TOP, (client.right - width) / 2, (client.bottom - height) / 2,
                  width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW );

    VideoResult result = VideoResult::Continue;
    error = MCIWndPlay( video );
    if ( error )
        Warning( "Unable to play startup video '%s' (MCI error %ld).\n", path, error );
    else
        result = WaitForVideo( gameWindow, video );

    if ( IsWindow( video ) )
        MCIWndDestroy( video );
    return result;
}
}

bool PlayStartupVideos( HWND gameWindow, IFileSystem &fileSystem )
{
    if ( CommandLine()->CheckParm( "-novid" ) )
        return true;

    FileHandle_t playlistFile = fileSystem.Open( "media/StartupVids.txt", "rb", "GAME" );
    if ( playlistFile == FILESYSTEM_INVALID_HANDLE )
        return true;

    const unsigned int size = fileSystem.Size( playlistFile );
    if ( size == 0 || size > 64 * 1024 )
    {
        fileSystem.Close( playlistFile );
        return true;
    }

    std::string playlist( size, '\0' );
    const int bytesRead = fileSystem.Read( &playlist[0], size, playlistFile );
    fileSystem.Close( playlistFile );
    if ( bytesRead != static_cast<int>( size ) )
        return true;

    if ( playlist.compare( 0, 3, "\xef\xbb\xbf" ) == 0 )
        playlist.erase( 0, 3 );

    std::istringstream lines( playlist );
    std::string line;
    while ( std::getline( lines, line ) )
    {
        const std::string path = GetVideoPath( line );
        const size_t extension = path.find_last_of( '.' );
        if ( extension == std::string::npos || _stricmp( path.c_str() + extension, ".avi" ) != 0 )
            continue;

        char localPath[MAX_PATH];
        const int pathLength = fileSystem.GetLocalPathLen( path.c_str() );
        if ( pathLength <= 0 || pathLength >= MAX_PATH || !fileSystem.GetLocalPath( path.c_str(), localPath ) )
        {
            Warning( "Startup video '%s' is unavailable.\n", path.c_str() );
            continue;
        }

        const VideoResult result = PlayVideo( gameWindow, localPath );
        if ( result != VideoResult::Continue )
            return result == VideoResult::SkipPlaylist;
    }

    return true;
}
