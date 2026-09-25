#include "core/common.h"

#include <fstream>

static std::string g_exeDir;

void logInfo(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stdout, fmt, ap);
    va_end(ap);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void logError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[error] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

void setExecutableDir(const std::string& dir) { g_exeDir = dir; }

static bool fileExists(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return f.good();
}

std::string assetPath(const std::string& relative) {
    const std::string probe = "assets/fonts/RussoOne-Regular.ttf";
    static std::string root;
    if (root.empty()) {
        std::vector<std::string> candidates;
        if (!g_exeDir.empty()) {
            candidates.push_back(g_exeDir + "/");
            candidates.push_back(g_exeDir + "/../");
            candidates.push_back(g_exeDir + "/../../");
        }
        candidates.push_back("./");
        candidates.push_back("../");
#ifdef CS2P_SOURCE_DIR
        candidates.push_back(std::string(CS2P_SOURCE_DIR) + "/");
#endif
        for (auto& c : candidates)
            if (fileExists(c + probe)) { root = c; break; }
        if (root.empty()) root = "./";
    }
    return root + "assets/" + relative;
}

bool readFileBytes(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize n = f.tellg();
    f.seekg(0);
    out.resize((size_t)n);
    return (bool)f.read((char*)out.data(), n);
}
