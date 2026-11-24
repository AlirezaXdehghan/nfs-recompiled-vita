#include <SDL3/SDL_main.h>
#include <lib/file.h>
#include <lib/registry.h>
#include <lib/window.h>
#include <nfs2se.h>
#include <string>

static std::string getExeDirectory(const char* argv0)
{
    std::string path(argv0);
    for (auto& c : path)
        if (c == '\\') c = '/';
    auto pos = path.rfind('/');
    if (pos != std::string::npos)
        return path.substr(0, pos + 1);
    return "./";
}

int main(int argc, char* argv[])
{
    if (argc >= 3)
    {
        win32::File::setDataDirectory(argv[1]);
        win32::File::setCdDirectory(argv[2]);
    }
    else if (argc == 2)
    {
        win32::File::setDataDirectory(argv[1]);
        win32::File::setCdDirectory(argv[1]);
    }
#if defined(__vita__)
    else
    {
        /* No useful argv on the Vita — pin the data + "CD" roots to the
         * standard app-data location (see vita/README_VITA.md). */
        win32::File::setDataDirectory("ux0:data/nfs2se/");
        win32::File::setCdDirectory("ux0:data/nfs2se/");
    }
#else
    else
    {
        std::string exeDir = getExeDirectory(argv[0]);
        win32::File::setDataDirectory(exeDir.c_str());
        win32::File::setCdDirectory(exeDir.c_str());
    }
#endif
    SDL_Init(SDL_INIT_EVENTS|SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK);
    win32::g_isNFS3 = false;
    SDL_Log("[nfs2se] SDL_Init done, constructing Application");
    {
        nfs2se::Application app(
#if defined(__vita__)
            "nfs2se.exe"
#else
            argv[0]
#endif
        );
        app.addRegistryKey(win32::HKEY_LOCAL_MACHINE, "SOFTWARE\\Electronic Arts\\Need For Speed II SE", "Install Dir", new win32::RegistryValue("ux0:data/nfs2se/"));
        app.addRegistryKey(win32::HKEY_LOCAL_MACHINE, "SOFTWARE\\Electronic Arts\\Need For Speed II SE", "Language", new win32::RegistryValue("english"));
        SDL_Log("[nfs2se] App constructed, calling execute");
        app.execute();
    }
    SDL_Quit();
    return 0;
}
