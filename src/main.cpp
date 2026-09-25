#include <string>

#include "app.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    for (int i = 1; i + 1 < argc; i++)
        if (std::string(argv[i]) == "--dump-sounds") return Audio::dumpSounds(argv[i + 1]) ? 0 : 1;
    std::string exe = argc > 0 ? argv[0] : "";
    size_t slash = exe.find_last_of("/\\");
    setExecutableDir(slash == std::string::npos ? "." : exe.substr(0, slash));
    static App app;
    return app.run(argc, argv);
}
