#pragma once
#include <string>

struct Settings {
    std::string name = "Игрок";
    float sensitivity = 1.6f;
    bool invertY = false;
    float zoomSensitivity = 1.0f;
    int vmPreset = 0;  // 0 desktop, 1 couch, 2 classic
    float vmFov = 68.0f;
    float volume = 0.75f;
    int msaa = 4;
    int shadows = 2;  // 0 off, 1 2048, 2 4096
    float renderScale = 1.0f;
    bool bloom = true;
    bool vsync = true;
    int fpsMax = 0;
    bool fullscreen = false;
    bool showFps = true;
    // Crosshair (CS2 classic static).
    float chSize = 2.5f;
    float chGap = -1.0f;
    float chThickness = 1.0f;
    bool chOutline = true;
    bool chDot = false;
    int chColor = 1;  // palette index
    float chAlpha = 1.0f;
    // Last lobby selection.
    int mode = 0;
    int map = 0;
    int team = 2;
    int difficulty = 1;
    int teamSize = 5;
    int longMatch = 0;

    void load(const std::string& path);
    void save(const std::string& path) const;
};

struct CrosshairColor {
    const char* name;
    int r, g, b;
};
extern const CrosshairColor kCrosshairColors[6];
