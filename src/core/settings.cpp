#include "core/settings.h"

#include <fstream>
#include <sstream>

const CrosshairColor kCrosshairColors[6] = {
    {"Красный", 250, 50, 50}, {"Зелёный", 60, 250, 60}, {"Жёлтый", 250, 245, 60},
    {"Синий", 60, 120, 250},  {"Голубой", 60, 240, 250}, {"Белый", 255, 255, 255},
};

void Settings::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto fl = [&](float& dst) { dst = (float)std::atof(v.c_str()); };
        auto in = [&](int& dst) { dst = std::atoi(v.c_str()); };
        auto bo = [&](bool& dst) { dst = std::atoi(v.c_str()) != 0; };
        if (k == "name") name = v;
        else if (k == "sensitivity") fl(sensitivity);
        else if (k == "invert_y") bo(invertY);
        else if (k == "zoom_sensitivity") fl(zoomSensitivity);
        else if (k == "vm_preset") in(vmPreset);
        else if (k == "vm_fov") fl(vmFov);
        else if (k == "volume") fl(volume);
        else if (k == "msaa") in(msaa);
        else if (k == "shadows") in(shadows);
        else if (k == "render_scale") fl(renderScale);
        else if (k == "bloom") bo(bloom);
        else if (k == "vsync") bo(vsync);
        else if (k == "fps_max") in(fpsMax);
        else if (k == "fullscreen") bo(fullscreen);
        else if (k == "show_fps") bo(showFps);
        else if (k == "ch_size") fl(chSize);
        else if (k == "ch_gap") fl(chGap);
        else if (k == "ch_thickness") fl(chThickness);
        else if (k == "ch_outline") bo(chOutline);
        else if (k == "ch_dot") bo(chDot);
        else if (k == "ch_color") in(chColor);
        else if (k == "ch_alpha") fl(chAlpha);
        else if (k == "mode") in(mode);
        else if (k == "map") in(map);
        else if (k == "team") in(team);
        else if (k == "difficulty") in(difficulty);
        else if (k == "team_size") in(teamSize);
        else if (k == "long_match") in(longMatch);
    }
}

void Settings::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return;
    f << "name=" << name << "\nsensitivity=" << sensitivity << "\ninvert_y=" << invertY << "\nzoom_sensitivity=" << zoomSensitivity
      << "\nvm_preset=" << vmPreset << "\nvm_fov=" << vmFov << "\nvolume=" << volume << "\nmsaa=" << msaa << "\nshadows=" << shadows
      << "\nrender_scale=" << renderScale << "\nbloom=" << bloom << "\nvsync=" << vsync << "\nfps_max=" << fpsMax
      << "\nfullscreen=" << fullscreen << "\nshow_fps=" << showFps << "\nch_size=" << chSize << "\nch_gap=" << chGap
      << "\nch_thickness=" << chThickness << "\nch_outline=" << chOutline << "\nch_dot=" << chDot << "\nch_color=" << chColor
      << "\nch_alpha=" << chAlpha << "\nmode=" << mode << "\nmap=" << map << "\nteam=" << team << "\ndifficulty=" << difficulty
      << "\nteam_size=" << teamSize << "\nlong_match=" << longMatch << "\n";
}
