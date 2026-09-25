#pragma once
#include <string>

struct Settings {
    std::string name = "Игрок";
    // Mouse.
    float sensitivity = 1.6f;
    bool invertY = false;
    float zoomSensitivity = 1.0f;
    bool rawInput = true;  // unaccelerated OS mouse motion when available
    // Viewmodel (CS2 semantics: offsets in game units, x = right, y = forward, z = up).
    int vmPreset = 0;  // 0 desktop, 1 couch, 2 classic; changing it rewrites fov and offsets
    float vmFov = 68.0f;
    float vmOffsetX = 0.0f;  // -2 .. 2.5
    float vmOffsetY = 0.0f;  // -2 .. 2
    float vmOffsetZ = 0.0f;  // -2 .. 2
    float vmBob = 1.0f;      // 0 .. 1 walk bob amount
    // HUD.
    bool showFps = true;
    float hudScale = 1.0f;   // 0.8 .. 1.2
    int hudColor = 0;        // index into kHudColors
    float radarZoom = 1.0f;  // 0.6 .. 1.6
    bool radarRotate = true;
    // Audio (0..1). Categories multiply the master volume.
    float volume = 0.75f;
    float volumeWeapons = 1.0f;  // gunshots, reloads, grenades, knife
    float volumeWorld = 1.0f;    // footsteps, impacts, bomb, player sounds
    float volumeUi = 0.8f;       // menus, buy, kill confirm
    float volumeMusic = 0.7f;    // round start / win / lose stingers
    bool spatialAudio = true;    // headphone 3D (interaural delay + head shadow)
    // Video. `quality` is a preset that writes the fields below; 4 = custom.
    int quality = 3;           // 0 low, 1 medium, 2 high, 3 max, 4 custom
    int msaa = 8;              // 0, 2, 4, 8
    int shadows = 3;           // 0 off, 1 medium, 2 high, 3 max (cascades + soft)
    int ssao = 2;              // 0 off, 1 normal, 2 high
    bool sunShafts = true;     // volumetric light shafts
    bool fxaa = true;          // post-process anti-aliasing on top of MSAA
    int textureQuality = 2;    // 0 low, 1 medium, 2 high (procedural texture resolution)
    int anisotropy = 16;       // 1, 2, 4, 8, 16
    bool dynamicLights = true; // muzzle flashes and explosions light the scene
    bool lensFlare = true;
    bool bloom = true;
    float renderScale = 1.0f;  // 0.5 .. 1.0 (not touched by presets)
    // Post-processing taste (not touched by presets).
    float brightness = 0.0f;   // -1 .. 1, exposure offset
    float saturation = 1.0f;   // 0.5 .. 1.5
    float sharpen = 0.35f;     // 0 .. 1
    bool filmGrain = true;
    bool chromatic = false;    // chromatic aberration at the screen edges
    bool vignette = true;
    bool vsync = true;
    int fpsMax = 0;
    bool fullscreen = false;
    // Crosshair (CS2 classic).
    int chStyle = 0;           // 0 static, 1 dynamic (gap grows with movement and firing)
    bool chTStyle = false;     // no top line
    bool chFollowRecoil = false;
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
    void applyQualityPreset(int q);    // q in 0..3; sets quality = q
    int detectQualityPreset() const;   // matching preset or 4 (custom)
    void applyViewmodelPreset(int p);  // p in 0..2; sets vmPreset, vmFov and offsets
};

struct CrosshairColor {
    const char* name;
    int r, g, b;
};
extern const CrosshairColor kCrosshairColors[6];
extern const CrosshairColor kHudColors[5];
