#include "core/settings.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

const CrosshairColor kCrosshairColors[6] = {
    {"Красный", 250, 50, 50}, {"Зелёный", 60, 250, 60}, {"Жёлтый", 250, 245, 60},
    {"Синий", 60, 120, 250},  {"Голубой", 60, 240, 250}, {"Белый", 255, 255, 255},
};

const CrosshairColor kHudColors[5] = {
    {"Белый", 236, 238, 242}, {"Золотой", 232, 190, 96}, {"Голубой", 110, 190, 240},
    {"Зелёный", 120, 220, 110}, {"Розовый", 240, 130, 190},
};

namespace {

enum FieldType { F_FLOAT, F_INT, F_BOOL, F_STRING };
struct Field {
    const char* key;
    FieldType type;
    void* ptr;
};

std::vector<Field> fields(Settings& s) {
    return {
        {"name", F_STRING, &s.name},
        {"sensitivity", F_FLOAT, &s.sensitivity},
        {"invert_y", F_BOOL, &s.invertY},
        {"zoom_sensitivity", F_FLOAT, &s.zoomSensitivity},
        {"raw_input", F_BOOL, &s.rawInput},
        {"vm_preset", F_INT, &s.vmPreset},
        {"vm_fov", F_FLOAT, &s.vmFov},
        {"vm_offset_x", F_FLOAT, &s.vmOffsetX},
        {"vm_offset_y", F_FLOAT, &s.vmOffsetY},
        {"vm_offset_z", F_FLOAT, &s.vmOffsetZ},
        {"vm_bob", F_FLOAT, &s.vmBob},
        {"show_fps", F_BOOL, &s.showFps},
        {"hud_scale", F_FLOAT, &s.hudScale},
        {"hud_color", F_INT, &s.hudColor},
        {"radar_zoom", F_FLOAT, &s.radarZoom},
        {"radar_rotate", F_BOOL, &s.radarRotate},
        {"volume", F_FLOAT, &s.volume},
        {"volume_weapons", F_FLOAT, &s.volumeWeapons},
        {"volume_world", F_FLOAT, &s.volumeWorld},
        {"volume_ui", F_FLOAT, &s.volumeUi},
        {"volume_music", F_FLOAT, &s.volumeMusic},
        {"spatial_audio", F_BOOL, &s.spatialAudio},
        {"quality", F_INT, &s.quality},
        {"msaa", F_INT, &s.msaa},
        {"shadows", F_INT, &s.shadows},
        {"ssao", F_INT, &s.ssao},
        {"sun_shafts", F_BOOL, &s.sunShafts},
        {"fxaa", F_BOOL, &s.fxaa},
        {"texture_quality", F_INT, &s.textureQuality},
        {"anisotropy", F_INT, &s.anisotropy},
        {"dynamic_lights", F_BOOL, &s.dynamicLights},
        {"lens_flare", F_BOOL, &s.lensFlare},
        {"bloom", F_BOOL, &s.bloom},
        {"render_scale", F_FLOAT, &s.renderScale},
        {"brightness", F_FLOAT, &s.brightness},
        {"saturation", F_FLOAT, &s.saturation},
        {"sharpen", F_FLOAT, &s.sharpen},
        {"film_grain", F_BOOL, &s.filmGrain},
        {"chromatic", F_BOOL, &s.chromatic},
        {"vignette", F_BOOL, &s.vignette},
        {"vsync", F_BOOL, &s.vsync},
        {"fps_max", F_INT, &s.fpsMax},
        {"fullscreen", F_BOOL, &s.fullscreen},
        {"ch_style", F_INT, &s.chStyle},
        {"ch_t_style", F_BOOL, &s.chTStyle},
        {"ch_follow_recoil", F_BOOL, &s.chFollowRecoil},
        {"ch_size", F_FLOAT, &s.chSize},
        {"ch_gap", F_FLOAT, &s.chGap},
        {"ch_thickness", F_FLOAT, &s.chThickness},
        {"ch_outline", F_BOOL, &s.chOutline},
        {"ch_dot", F_BOOL, &s.chDot},
        {"ch_color", F_INT, &s.chColor},
        {"ch_alpha", F_FLOAT, &s.chAlpha},
        {"mode", F_INT, &s.mode},
        {"map", F_INT, &s.map},
        {"team", F_INT, &s.team},
        {"difficulty", F_INT, &s.difficulty},
        {"team_size", F_INT, &s.teamSize},
        {"long_match", F_INT, &s.longMatch},
    };
}

struct Preset {
    int msaa, shadows, ssao;
    bool sunShafts, fxaa;
    int textureQuality, anisotropy;
    bool dynamicLights, lensFlare, bloom;
};
const Preset kPresets[4] = {
    {0, 1, 0, false, true, 0, 2, true, false, true},   // low
    {2, 2, 1, false, true, 1, 4, true, false, true},   // medium
    {4, 2, 1, true, false, 2, 8, true, true, true},    // high
    {8, 3, 2, true, true, 2, 16, true, true, true},    // max
};

}  // namespace

void Settings::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::vector<Field> fs = fields(*this);
    bool hasQuality = false;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        for (auto& fd : fs) {
            if (k != fd.key) continue;
            switch (fd.type) {
                case F_FLOAT: *(float*)fd.ptr = (float)std::atof(v.c_str()); break;
                case F_INT: *(int*)fd.ptr = std::atoi(v.c_str()); break;
                case F_BOOL: *(bool*)fd.ptr = std::atoi(v.c_str()) != 0; break;
                case F_STRING: *(std::string*)fd.ptr = v; break;
            }
            if (k == "quality") hasQuality = true;
            break;
        }
    }
    // Configs from older versions predate presets: start them on max quality.
    if (!hasQuality) applyQualityPreset(3);
}

void Settings::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return;
    for (auto& fd : fields(const_cast<Settings&>(*this))) {
        f << fd.key << '=';
        switch (fd.type) {
            case F_FLOAT: f << *(const float*)fd.ptr; break;
            case F_INT: f << *(const int*)fd.ptr; break;
            case F_BOOL: f << (*(const bool*)fd.ptr ? 1 : 0); break;
            case F_STRING: f << *(const std::string*)fd.ptr; break;
        }
        f << '\n';
    }
}

void Settings::applyQualityPreset(int q) {
    if (q < 0 || q > 3) return;
    const Preset& p = kPresets[q];
    quality = q;
    msaa = p.msaa;
    shadows = p.shadows;
    ssao = p.ssao;
    sunShafts = p.sunShafts;
    fxaa = p.fxaa;
    textureQuality = p.textureQuality;
    anisotropy = p.anisotropy;
    dynamicLights = p.dynamicLights;
    lensFlare = p.lensFlare;
    bloom = p.bloom;
}

int Settings::detectQualityPreset() const {
    for (int q = 0; q < 4; q++) {
        const Preset& p = kPresets[q];
        if (msaa == p.msaa && shadows == p.shadows && ssao == p.ssao && sunShafts == p.sunShafts && fxaa == p.fxaa &&
            textureQuality == p.textureQuality && anisotropy == p.anisotropy && dynamicLights == p.dynamicLights &&
            lensFlare == p.lensFlare && bloom == p.bloom)
            return q;
    }
    return 4;
}

void Settings::applyViewmodelPreset(int p) {
    if (p < 0 || p > 2) return;
    vmPreset = p;
    // CS2 viewmodel_presetpos: desktop, couch, classic.
    const float fov[3] = {68.0f, 54.0f, 68.0f};
    const float ox[3] = {0.0f, 0.0f, 1.0f}, oy[3] = {0.0f, -1.0f, 1.0f}, oz[3] = {0.0f, -1.0f, -1.0f};
    vmFov = fov[p];
    vmOffsetX = ox[p];
    vmOffsetY = oy[p];
    vmOffsetZ = oz[p];
}
