#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "assets/models.h"
#include "assets/textures.h"
#include "audio/audio.h"
#include "core/settings.h"
#include "game/agent.h"
#include "game/game.h"
#include "game/viewmodel.h"
#include "gfx/renderer.h"
#include "platform/window.h"
#include "ui/ui.h"
#include "ui/widgets.h"
#include "world/map.h"

// Command-line driven automation used to capture screenshots without user input.
struct AutoShot {
    bool enabled = false;
    std::string path;
    std::string scene = "lobby";
    int frames = 45;
    std::string map = "dust";
    int mode = 0;
    int team = 2;
    int weapon = -1;
    bool hasPos = false, hasAng = false;
    vec3 pos;
    float pitch = 0, yaw = 0;
    float sim = 0;
    std::string anim;
    float animT = 0;
    std::string overlay;
    int width = 1600, height = 900;
    int tab = 0, settingsTab = 0;
    bool fire = false;
    bool thirdPerson = false;
    int frame = 0;
};

class App {
public:
    int run(int argc, char** argv);

    Window win;
    Renderer renderer;
    UI ui;
    Audio audio;
    Font fRegular, fBold, fTitle;
    Theme theme;
    Settings settings;
    WeaponModel weapons[W_COUNT];
    ArmsModel arms[3];
    AgentModel agents[3];
    GpuMesh shellMesh;
    GLuint albedoArr = 0, normalArr = 0;
    std::vector<std::unique_ptr<GameMap>> maps;
    std::vector<GLuint> thumbs, radars;
    std::vector<vec3> radarCenter;
    std::vector<float> radarHalf;
    GLuint iconTex = 0;
    vec4 iconUV[W_COUNT];
    float iconAspect[W_COUNT] = {};

    enum State { ST_LOBBY, ST_GAME } state = ST_LOBBY;
    Game game;
    int gameMap = 0;
    bool paused = false, buyMenuOpen = false, pauseSettings = false;
    int lobbyTab = 0, settingsTab = 0, arsenalSel = W_AK47;
    float lobbyTime = 0, fps = 60;
    float viewPitch = 0, viewYaw = 0;
    bool quit = false;

    // UI modules (lobby.cpp / hud.cpp).
    void drawLobby(float dt);
    void drawSettingsPanel(float x, float y, float w, float h);
    void drawHud(float dt);
    void drawBuyMenu();
    void drawScoreboard(float x, float y, float w);
    void drawPauseMenu();
    void drawCrosshair(float cx, float cy, float scale);
    void drawIcon(int weapon, float x, float y, float h, uint32_t tint, bool alignRight = false, float maxW = 0);
    void startMatch();
    void quitToLobby();

private:
    bool boot();
    void bootScreen(const std::string& msg, float progress);
    void ensureMap(int idx);
    void useMap(int idx);
    void renderThumbnail(int idx);
    void renderRadar(int idx);
    void buildIcons();
    void applyVideoSettings();
    void frameLobby(float dt);
    void frameGame(float dt);
    void processEvents();
    void parseArgs(int argc, char** argv);
    bool automationStep();
    void playLocalFx(float dt);

    int renderedMap = -1;
    RenderSettings appliedRs;
    bool appliedVsync = true, appliedFullscreen = false, appliedValid = false;
    AutoShot shot;
    ViewmodelState vmState;
    ViewmodelPose vmPose;
    float tickAccum = 0;
    float lastViewYaw = 0, lastViewPitch = 0;
    std::vector<std::array<mat4, AG_BONES>> boneStore;
    mat4 armBones[AB_PER_ARM * 2];
    mat4 identityBones[WB_COUNT];
    struct Scheduled {
        float at;
        int snd;
        vec3 pos;
        float vol;
        bool spatial;
    };
    std::vector<Scheduled> scheduled;
    float shake = 0;
    float lastLocalShot = -10;
    float damageFlash = 0;
    vec3 vmMuzzleWorld;
    Xform vmWeaponWorld;
    int spectate = -1;

public:
    // Exposed for the HUD.
    struct DamageMark {
        vec3 from;
        float time;
    };
    std::vector<DamageMark> damageMarks;
    float killBannerTime = -10;
    bool spotted[32] = {};
    Camera lastCam;
    int spectateTarget() const { return spectate; }
};
