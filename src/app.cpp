#include "app.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace {

constexpr float kTick = 1.0f / 128.0f;

const char* kIconVS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)GLSL";
const char* kIconFS = R"GLSL(#version 330 core
out vec4 oColor;
void main() { oColor = vec4(1.0); }
)GLSL";

int weaponSound(int w) {
    switch (w) {
        case W_AK47: return SND_AK;
        case W_M4A4: return SND_M4;
        case W_AWP: return SND_AWP;
        case W_DEAGLE: return SND_DEAGLE;
        case W_GLOCK: return SND_GLOCK;
        case W_USP: return SND_USP;
        default: return SND_KNIFE_SWING;
    }
}

bool parseVec3(const char* s, vec3& out) { return std::sscanf(s, "%f,%f,%f", &out.x, &out.y, &out.z) == 3; }

vec2 g_autoMouse(-1000, -1000);  // --mouse x,y: fixed UI cursor for hover screenshots

}  // namespace

void App::parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--shot") { shot.enabled = true; shot.path = next(); }
        else if (a == "--scene") shot.scene = next();
        else if (a == "--frames") shot.frames = std::atoi(next());
        else if (a == "--map") shot.map = next();
        else if (a == "--mode") { std::string m = next(); shot.mode = m == "dm" ? MODE_DEATHMATCH : m == "practice" ? MODE_PRACTICE : MODE_COMPETITIVE; }
        else if (a == "--team") shot.team = std::string(next()) == "t" ? TEAM_T : TEAM_CT;
        else if (a == "--weapon") shot.weapon = weaponFromKey(next());
        else if (a == "--pos") shot.hasPos = parseVec3(next(), shot.pos);
        else if (a == "--ang") { shot.hasAng = std::sscanf(next(), "%f,%f", &shot.pitch, &shot.yaw) == 2; }
        else if (a == "--sim") shot.sim = (float)std::atof(next());
        else if (a == "--anim") {
            std::string v = next();
            auto at = v.find('@');
            shot.anim = v.substr(0, at);
            shot.animT = at == std::string::npos ? 0.0f : (float)std::atof(v.substr(at + 1).c_str());
        }
        else if (a == "--overlay") shot.overlay = next();
        else if (a == "--size") std::sscanf(next(), "%dx%d", &shot.width, &shot.height);
        else if (a == "--tab") shot.tab = std::atoi(next());
        else if (a == "--settings-tab") shot.settingsTab = std::atoi(next());
        else if (a == "--fire") shot.fire = true;
        else if (a == "--thirdperson") shot.thirdPerson = true;
        else if (a == "--mouse") std::sscanf(next(), "%f,%f", &g_autoMouse.x, &g_autoMouse.y);
        else if (a == "--vmview") shot.vmView = next();
        else if (a == "--vmhold") shot.vmHold = next();
        else if (a == "--vmrig") shot.vmRig = next();
        else if (a == "--quality") settings.applyQualityPreset(std::atoi(next()));
        else if (a == "--vmclay") shot.vmClay = true;
    }
}

int App::run(int argc, char** argv) {
    parseArgs(argc, argv);
    if (!shot.enabled) settings.load("settings.cfg");
    int w = shot.enabled ? shot.width : 1600, h = shot.enabled ? shot.height : 900;
    if (!win.create(w, h, "CS2 Prototype", !shot.enabled && settings.fullscreen, true)) {
        logError("Could not create an OpenGL 3.3 window");
        return 1;
    }
    if (!boot()) return 1;
    double last = win.time();
    float fpsAcc = 0;
    int fpsFrames = 0;
    while (!win.shouldClose() && !quit) {
        win.beginFrame();
        double now = win.time();
        float dt = (float)std::min(0.1, now - last);
        last = now;
        if (shot.enabled) dt = 1.0f / 60.0f;
        fpsAcc += dt;
        fpsFrames++;
        if (fpsAcc >= 0.5f) {
            fps = fpsFrames / fpsAcc;
            fpsAcc = 0;
            fpsFrames = 0;
        }
        theme.dt = dt;
        if (shot.enabled && shot.scene == "vmlab") frameVmLab(dt);
        else if (state == ST_LOBBY) frameLobby(dt);
        else frameGame(dt);
        if (shot.enabled && automationStep()) break;
        win.swap();
        if (!shot.enabled && settings.fpsMax > 0 && !settings.vsync) {
            double target = 1.0 / settings.fpsMax, spent = win.time() - now;
            if (spent < target) std::this_thread::sleep_for(std::chrono::duration<double>(target - spent));
        }
    }
    if (!shot.enabled) settings.save("settings.cfg");
    audio.shutdown();
    win.destroy();
    return 0;
}

void App::bootScreen(const std::string& msg, float p) {
    int W = win.width(), H = win.height();
    bindDefaultFramebuffer(W, H);
    glClearColor(0.03f, 0.035f, 0.045f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    float s = std::min(W / 1920.0f, H / 1080.0f);
    theme.s = s;
    ui.begin(W, H, 0, s);
    ui.rectGrad(0, 0, (float)W, (float)H, rgba(22, 26, 34), rgba(6, 7, 10));
    ui.text(fTitle, "CS2", W * 0.5f, H * 0.36f, 110 * s, colors::text, ALIGN_CENTER, 8 * s);
    ui.text(fBold, "PROTOTYPE", W * 0.5f, H * 0.36f + 118 * s, 26 * s, colors::accent, ALIGN_CENTER, 12 * s);
    float bw = 520 * s, bx = (W - bw) * 0.5f, by = H * 0.64f;
    ui.rect(bx, by, bw, 4 * s, rgba(255, 255, 255, 30), 2 * s);
    ui.rect(bx, by, bw * saturate(p), 4 * s, colors::accent, 2 * s);
    ui.text(fRegular, msg, W * 0.5f, by + 18 * s, 21 * s, colors::dim, ALIGN_CENTER);
    ui.end();
    win.swap();
    glfwPollEvents();
}

bool App::boot() {
    if (!renderer.init() || !ui.init()) return false;
    bool fontsOk = fRegular.load(assetPath("fonts/FiraSansCondensed-Regular.ttf")) &&
                   fBold.load(assetPath("fonts/FiraSansCondensed-SemiBold.ttf")) && fTitle.load(assetPath("fonts/RussoOne-Regular.ttf"));
    if (!fontsOk) {
        logError("Fonts are missing: the 'assets' folder must sit next to the executable");
        return false;
    }
    theme.regular = &fRegular;
    theme.bold = &fBold;
    theme.title = &fTitle;
    bootScreen("Звук", 0.02f);
    audio.init();
    // World textures are generated by the renderer itself (applyVideoSettings), at the chosen quality.
    bootScreen("Генерация текстур материалов", 0.05f);
    applyVideoSettings();
    bootScreen("Модели оружия, рук и агентов", 0.22f);
    for (int w = 0; w < W_COUNT; w++) buildWeaponModel(w, renderer, weapons[w]);
    buildArmsModel(TEAM_T, renderer, arms[TEAM_T]);
    buildArmsModel(TEAM_CT, renderer, arms[TEAM_CT]);
    arms[0] = arms[TEAM_CT];
    buildAgentModel(TEAM_T, renderer, agents[TEAM_T]);
    buildAgentModel(TEAM_CT, renderer, agents[TEAM_CT]);
    agents[0] = agents[TEAM_CT];
    {
        MeshBuilder m;
        m.mat = {{0.80f, 0.60f, 0.28f}, 0.3f, 1.0f, 0};
        m.cylinder({-0.55f, 0, 0}, {0.55f, 0, 0}, 0.22f, 0.2f, 8, true);
        shellMesh = m.upload(renderer);
    }
    buildIcons();
    size_t nm = mapList().size();
    maps.resize(nm);
    thumbs.assign(nm, 0);
    radars.assign(nm, 0);
    radarCenter.assign(nm, vec3(0));
    radarHalf.assign(nm, 1000.0f);
    for (size_t i = 0; i < nm && shot.scene != "vmlab"; i++) {
        bootScreen(std::string("Карта ") + mapList()[i].title + ": геометрия, запекание света, навигация", 0.3f + 0.65f * i / nm);
        ensureMap((int)i);
        renderThumbnail((int)i);
        renderRadar((int)i);
    }
    bootScreen("Готово", 1.0f);
    for (auto& m : identityBones) m = mat4();
    if (shot.enabled) {
        int mi = 0;
        for (size_t i = 0; i < nm; i++)
            if (shot.map == mapList()[i].id) mi = (int)i;
        settings.map = mi;
        settings.mode = shot.mode;
        settings.team = shot.team;
        lobbyTab = shot.tab;
        settingsTab = shot.settingsTab;
        if (shot.weapon >= 0) arsenalSel = shot.weapon;
        if (shot.scene == "game") startMatch();
    }
    return true;
}

void App::ensureMap(int idx) {
    if (maps[idx]) return;
    auto m = std::make_unique<GameMap>();
    buildMapById(mapList()[idx].id, *m);
    m->buildRenderGeometry();
    m->bakeLighting(48);
    m->buildNavigation();
    m->bakeProbes();
    logInfo("Map %s: %zu brushes, %zu verts, %d nav cells", mapList()[idx].id, m->world.brushes.size(), m->verts.size(), m->nav.countWalkable());
    maps[idx] = std::move(m);
}

void App::useMap(int idx) {
    if (renderedMap == idx) return;
    GameMap& m = *maps[idx];
    renderer.setWorldGeometry(m.verts, m.indices, m.chunks, m.bounds);
    renderer.setEnvironment(m.info.env);
    renderedMap = idx;
}

void App::renderThumbnail(int idx) {
    useMap(idx);
    GameMap& m = *maps[idx];
    FrameInput in;
    in.time = 5;
    in.cam.aspect = (float)renderer.outWidth() / renderer.outHeight();
    in.cam.fovY = Camera::vfovFrom43(90);
    in.cam.setAngles(m.info.thumbPos, m.info.thumbPitch, m.info.thumbYaw);
    renderer.renderFrame(in);
    thumbs[idx] = renderer.captureLdr(0, 0, renderer.outWidth(), renderer.outHeight(), true);
}

void App::renderRadar(int idx) {
    useMap(idx);
    GameMap& m = *maps[idx];
    Environment env = m.info.env;
    env.fogDensity = 0;
    renderer.setEnvironment(env);
    vec3 c = m.bounds.center(), sz = m.bounds.size();
    float half = std::max(sz.x, sz.y) * 0.51f;
    FrameInput in;
    in.drawSky = false;
    in.time = 5;
    Camera& cam = in.cam;
    cam.ortho = true;
    cam.orthoHalf = half;
    cam.aspect = (float)renderer.outWidth() / renderer.outHeight();
    cam.znear = 10;
    cam.zfar = 20000;
    cam.pos = vec3(c.x, c.y, m.bounds.mx.z + 2000);
    cam.fwd = {0, 0, -1};
    cam.up = {0, 1, 0};
    cam.left = {-1, 0, 0};
    cam.update();
    renderer.renderFrame(in);
    int H = renderer.outHeight(), W = renderer.outWidth();
    radars[idx] = renderer.captureLdr((W - H) / 2, 0, H, H, true);
    radarCenter[idx] = vec3(c.x, c.y, 0);
    radarHalf[idx] = half;
    renderer.setEnvironment(m.info.env);
}

void App::buildIcons() {
    const int CW = 256, CH = 112, COLS = 4, ROWS = 3, AW = CW * COLS, AH = CH * ROWS;
    static RenderTarget rt;
    rt.create(AW, AH, GL_RGBA8, true);
    Shader sh;
    sh.build(kIconVS, kIconFS, "icon");
    rt.bind();
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    sh.use();
    for (int w = 0; w < W_COUNT; w++) {
        int cx = w % COLS, cy = w / COLS;
        const AABB& b = weapons[w].bounds;
        float bw = std::max(b.size().x, 0.1f), bh = std::max(b.size().z, 0.1f);
        float scale = std::min((CW - 16.0f) / bw, (CH - 16.0f) / bh);
        int pw = (int)(bw * scale), ph = (int)(bh * scale);
        int vx = cx * CW + (CW - pw) / 2, vy = cy * CH + (CH - ph) / 2;
        glViewport(vx, vy, pw, ph);
        mat4 m;
        m(1, 1) = 0;
        m(2, 2) = 0;
        m(0, 0) = 2.0f / bw;
        m(0, 3) = -1.0f - 2.0f * b.mn.x / bw;
        m(1, 2) = 2.0f / bh;
        m(1, 3) = -1.0f - 2.0f * b.mn.z / bh;
        m(2, 1) = 0.001f;
        sh.set("uMVP", m);
        weapons[w].mesh.draw();
        iconUV[w] = vec4((float)vx / AW, (float)vy / AH, (float)(vx + pw) / AW, (float)(vy + ph) / AH);
        iconAspect[w] = (float)pw / (float)ph;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, rt.color);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    iconTex = rt.color;
}

void App::applyVideoSettings() {
    RenderSettings rs;
    rs.msaa = settings.msaa;
    rs.shadows = settings.shadows;
    rs.ssao = settings.ssao;
    rs.sunShafts = settings.sunShafts;
    rs.fxaa = settings.fxaa;
    rs.textureQuality = settings.textureQuality;
    rs.anisotropy = settings.anisotropy;
    rs.lensFlare = settings.lensFlare;
    rs.renderScale = clampf(settings.renderScale, 0.5f, 1.0f);
    rs.bloom = settings.bloom;
    rs.brightness = settings.brightness;
    rs.saturation = settings.saturation;
    rs.sharpen = settings.sharpen;
    rs.filmGrain = settings.filmGrain;
    rs.chromatic = settings.chromatic;
    rs.vignette = settings.vignette;
    setLightBakeQuality(settings.shadows);  // maps are baked once at boot
    renderer.configure(win.width(), win.height(), rs);
    if (!appliedValid || settings.vsync != appliedVsync) {
        win.setVsync(settings.vsync && !shot.enabled);
        appliedVsync = settings.vsync;
    }
    if (!shot.enabled && appliedValid && settings.fullscreen != appliedFullscreen) {
        win.setFullscreen(settings.fullscreen);
        appliedFullscreen = settings.fullscreen;
    }
    if (!appliedValid) appliedFullscreen = win.fullscreen();
    appliedValid = true;
    audio.setVolume(settings.volume);
    audio.setCategoryVolume(SC_WEAPONS, settings.volumeWeapons);
    audio.setCategoryVolume(SC_WORLD, settings.volumeWorld);
    audio.setCategoryVolume(SC_UI, settings.volumeUi);
    audio.setCategoryVolume(SC_MUSIC, settings.volumeMusic);
    audio.setSpatial(settings.spatialAudio);
    win.setRawMouse(settings.rawInput);
}

static void uiInput(UI& ui, const Window& win, bool automation) {
    const Input& in = win.input;
    ui.mouse = automation ? g_autoMouse : in.mousePos;
    ui.mouseDown = in.mouseDown(0);
    ui.mousePressed = in.mousePressed(0);
    ui.mouseReleased = in.mouseReleased(0);
    ui.scroll = in.scroll;
}

void App::frameLobby(float dt) {
    lobbyTime += dt;
    applyVideoSettings();
    win.setCursorLocked(false);
    int mi = std::max(0, std::min(settings.map, (int)maps.size() - 1));
    useMap(mi);
    GameMap& m = *maps[mi];
    int W = renderer.outWidth(), H = renderer.outHeight();
    Camera cam;
    cam.aspect = (float)W / H;
    cam.fovY = Camera::vfovFrom43(78);
    float camYaw = m.info.lobbyCamYaw + std::sin(lobbyTime * 0.13f) * 2.0f;
    cam.setAngles(m.info.lobbyCamPos + vec3(0, 0, std::sin(lobbyTime * 0.3f) * 1.5f), m.info.lobbyCamPitch, camYaw);
    std::vector<ModelDraw> models;
    boneStore.resize(std::max<size_t>(boneStore.size(), 1));
    if (lobbyTab == 1) {
        const WeaponModel& wm = weapons[arsenalSel];
        vec3 center = cam.pos + cam.fwd * 44.0f - cam.left * 13.0f - cam.up * 1.0f;
        float len = std::max(wm.bounds.size().x, 4.0f);
        float sc = clampf(26.0f / len, 0.6f, 2.6f);
        mat4 model = translate(center) * rotZ((camYaw + 90.0f) * kDeg + lobbyTime * 0.45f) * rotX(0.12f) * scale(vec3(sc)) * translate(-wm.bounds.center());
        ModelDraw d;
        d.mesh = &wm.mesh;
        d.model = model;
        d.bones = identityBones;
        d.boneCount = WB_COUNT;
        m.sampleAmbient(center, d.ambUp, d.ambDown);
        d.ambUp *= 1.4f;
        d.ambDown *= 1.4f;
        models.push_back(d);
    } else {
        int team = settings.team == 1 ? TEAM_T : TEAM_CT;
        vec3 base = cam.pos + angleForward(0, camYaw) * 165.0f - yawLeft(camYaw) * 52.0f;
        TraceResult tr = m.world.traceRay(base + vec3(0, 0, 40), base - vec3(0, 0, 400), MASK_SHOT);
        base.z = tr.endpos.z;
        AgentAnimInput ai;
        ai.time = lobbyTime;
        ai.aimPitch = -6.0f + std::sin(lobbyTime * 0.7f) * 2.0f;
        ai.wm = &weapons[team == TEAM_T ? W_AK47 : W_M4A4];
        ai.weaponClass = WC_RIFLE;
        AgentPose pose;
        animateAgent(ai, pose);
        float yaw = camYaw + 180.0f - 30.0f + std::sin(lobbyTime * 0.4f) * 3.0f;
        Xform world(base, qaxis({0, 0, 1}, yaw * kDeg));
        auto& bs = boneStore[0];
        for (int b = 0; b < AG_BONES; b++) bs[b] = toMat4(world * pose.bones[b]);
        ModelDraw a;
        a.mesh = &agents[team].mesh;
        a.bones = bs.data();
        a.boneCount = AG_BONES;
        a.patternA = agents[team].patternA;
        a.patternB = agents[team].patternB;
        m.sampleAmbient(base + vec3(0, 0, 40), a.ambUp, a.ambDown);
        models.push_back(a);
        ModelDraw w = a;
        w.mesh = &weapons[team == TEAM_T ? W_AK47 : W_M4A4].mesh;
        w.model = toMat4(world * pose.weapon);
        w.bones = identityBones;
        w.boneCount = WB_COUNT;
        w.patternA = w.patternB = vec3(0.3f);
        models.push_back(w);
    }
    FrameInput in;
    in.cam = cam;
    in.models = &models;
    in.time = lobbyTime;
    renderer.renderFrame(in);
    renderer.updateBlur();
    renderer.ldr().bind();
    uiInput(ui, win, shot.enabled);
    theme.s = std::min(W / 1920.0f, H / 1080.0f);
    int hs = ui.hotSound, cs = ui.clickSound;
    ui.begin(W, H, renderer.blurTexture(), theme.s);
    drawLobby(dt);
    ui.end();
    if (ui.hotSound != hs) audio.play(SND_UI_HOVER, 0.25f);
    if (ui.clickSound != cs) audio.play(SND_UI_CLICK, 0.5f);
    renderer.presentToScreen(win.width(), win.height());
}

void App::startMatch() {
    MatchConfig mc;
    gameMap = std::max(0, std::min(settings.map, (int)maps.size() - 1));
    mc.mapId = mapList()[gameMap].id;
    mc.mode = settings.mode;
    mc.playerTeam = settings.team == 1 ? TEAM_T : TEAM_CT;
    mc.teamSize = settings.teamSize;
    mc.difficulty = settings.difficulty;
    mc.maxRounds = settings.longMatch ? 24 : 16;
    mc.playerName = settings.name;
    ensureMap(gameMap);
    useMap(gameMap);
    game.start(mc, maps[gameMap].get(), weapons);
    state = ST_GAME;
    paused = buyMenuOpen = pauseSettings = false;
    tickAccum = 0;
    viewYaw = lastViewYaw = game.localPlayer().yaw;
    viewPitch = lastViewPitch = 0;
    vmState = ViewmodelState();
    damageMarks.clear();
    scheduled.clear();
    spectate = -1;
    audio.stopAll();
}

void App::quitToLobby() {
    state = ST_LOBBY;
    paused = buyMenuOpen = false;
    win.setCursorLocked(false);
    audio.stopAll();
}

void App::processEvents() {
    Player& me = game.localPlayer();
    for (const GameEvent& e : game.events) {
        bool loc = e.player == game.local;
        float pitch = 0.96f + (float)((e.player * 7 + (int)(game.time * 97)) % 9) * 0.01f;
        switch (e.type) {
            case EV_SHOT: {
                int snd = weaponSound(e.weapon);
                if (loc) {
                    audio.play(snd, 0.75f, pitch);
                    lastLocalShot = game.time;
                    const WeaponDef& d = weaponDef(e.weapon);
                    if (d.cls == WC_RIFLE || d.cls == WC_PISTOL) {
                        const WeaponModel& wm = weapons[e.weapon];
                        vec3 ep = apply(vmWeaponWorld, wm.eject);
                        vec3 ed = rotate(vmWeaponWorld.q, wm.ejectDir);
                        game.fx.shell(ep, ed * 130.0f + me.mv.velocity + vec3(0, 0, 50), d.cls == WC_PISTOL ? 0.8f : 1.0f);
                    }
                    shake = std::max(shake, d.cls == WC_SNIPER ? 0.9f : e.weapon == W_DEAGLE ? 0.45f : 0.12f);
                    // View punch: a spring-damped kick per shot on top of the pattern-following recoil.
                    float k = d.cls == WC_SNIPER ? 3.4f : e.weapon == W_DEAGLE ? 2.7f : d.cls == WC_PISTOL ? 1.35f : e.weapon == W_AK47 ? 1.3f : 1.0f;
                    float side = std::sin(game.time * 97.31f + me.shotsFired * 1.7f);
                    viewPunchVel += vec3(k * 42.0f, side * k * 14.0f, side * k * 22.0f);
                } else {
                    audio.play3D(snd, e.pos, 1.0f, pitch, 380.0f);
                }
                break;
            }
            case EV_EMPTY: if (loc) audio.play(SND_EMPTY, 0.6f); break;
            case EV_RELOAD:
                if (loc) {
                    audio.play(SND_RELOAD_OUT, 0.55f);
                    scheduled.push_back({game.time + weaponDef(e.weapon).reloadTime * 0.62f, SND_RELOAD_IN, e.pos, 0.55f, false});
                } else {
                    audio.play3D(SND_RELOAD_OUT, e.pos, 0.6f);
                }
                break;
            case EV_DEPLOY: if (loc) audio.play(SND_DEPLOY, 0.35f); break;
            case EV_ZOOM: if (loc) audio.play(SND_ZOOM, 0.5f); break;
            case EV_BOLT: scheduled.push_back({game.time + 0.4f, SND_BOLT, e.pos, loc ? 0.6f : 0.8f, !loc}); break;
            case EV_HIT_BODY:
            case EV_HIT_HEAD:
            case EV_HIT_HELMET: {
                int snd = e.type == EV_HIT_BODY ? SND_HIT_BODY : e.type == EV_HIT_HEAD ? SND_HIT_HEAD : SND_HELMET;
                audio.play3D(snd, e.pos, 1.0f, pitch, 300.0f);
                if (loc) {
                    damageFlash = 1.0f;
                    damageMarks.push_back({me.lastHurtFrom, game.time});
                }
                break;
            }
            case EV_LOCAL_HIT: audio.play(e.value > 0.5f ? SND_HIT_HEAD : SND_HIT_BODY, e.value > 0.5f ? 0.35f : 0.18f); break;
            case EV_LOCAL_KILL:
                audio.play(SND_KILL, 0.3f);
                killBannerTime = game.time;
                break;
            case EV_LOCAL_DEATH: spectate = -1; break;
            case EV_IMPACT: {
                if (length(e.pos - lastCam.pos) > 1600.0f) break;
                int snd = e.surface == SURF_WOOD ? SND_IMPACT_WOOD : e.surface == SURF_METAL ? SND_IMPACT_METAL : e.surface == SURF_SAND ? SND_IMPACT_SAND : SND_IMPACT_STONE;
                audio.play3D(snd, e.pos, 0.45f, pitch, 150.0f);
                break;
            }
            case EV_FOOTSTEP: audio.play3D(SND_STEP0 + (int)(game.time * 1000) % 4, e.pos, loc ? 0.3f : 0.85f, pitch, 220.0f); break;
            case EV_JUMP: if (loc) audio.play(SND_JUMP, 0.2f); break;
            case EV_LAND: audio.play3D(SND_LAND, e.pos, clampf(e.value / 600.0f, 0.25f, 1.0f) * (loc ? 0.5f : 1.0f), pitch, 220.0f); break;
            case EV_KNIFE_SWING: audio.play3D(SND_KNIFE_SWING, e.pos, loc ? 0.5f : 0.7f, pitch); break;
            case EV_KNIFE_HIT: audio.play3D(e.value > 0.5f ? SND_KNIFE_STAB : SND_KNIFE_HIT, e.pos, 0.8f, pitch); break;
            case EV_GRENADE_THROW: audio.play3D(SND_THROW, e.pos, 0.6f); break;
            case EV_GRENADE_BOUNCE: audio.play3D(SND_BOUNCE, e.pos, 0.6f, pitch); break;
            case EV_EXPLOSION: {
                audio.play3D(SND_EXPLOSION, e.pos, 1.0f, 1.0f, 700.0f);
                float dist = length(e.pos - lastCam.pos);
                shake = std::max(shake, saturate(1.0f - dist / 1300.0f) * 3.0f);
                break;
            }
            case EV_SMOKE: audio.play3D(SND_SMOKE, e.pos, 0.7f); break;
            case EV_FLASH: audio.play3D(SND_FLASH, e.pos, 1.0f, 1.0f, 600.0f); break;
            case EV_BOMB_PLANTED: audio.play(SND_PLANTED, 0.6f); break;
            case EV_BOMB_BEEP: audio.play3D(SND_BEEP, e.pos, 0.9f, 1.0f, 600.0f); break;
            case EV_BOMB_DEFUSED: audio.play(SND_DEFUSED, 0.7f); break;
            case EV_BOMB_EXPLODED:
                audio.play(SND_EXPLOSION, 1.0f, 0.8f);
                shake = 4.0f;
                break;
            case EV_ROUND_START: audio.play(SND_ROUND_START, 0.45f); break;
            case EV_ROUND_END: audio.play((int)e.value == me.team ? SND_ROUND_WIN : SND_ROUND_LOSE, 0.55f); break;
            case EV_BUY: if (loc) audio.play(SND_BUY, 0.45f); break;
            case EV_PICKUP: if (loc) audio.play(SND_PICKUP, 0.5f); break;
            default: break;
        }
    }
    game.events.clear();
}

void App::frameGame(float dt) {
    applyVideoSettings();
    useMap(gameMap);
    GameMap& map = *maps[gameMap];
    Input& in = win.input;
    int W = renderer.outWidth(), H = renderer.outHeight();
    Player& me = game.localPlayer();
    if (!shot.enabled) {
        if (in.pressed(GLFW_KEY_ESCAPE)) {
            if (buyMenuOpen) buyMenuOpen = false;
            else {
                paused = !paused;
                pauseSettings = false;
            }
        }
        if (in.pressed(GLFW_KEY_B) && !paused && game.canBuy(me)) buyMenuOpen = !buyMenuOpen;
    }
    if (buyMenuOpen && !game.canBuy(me) && !shot.enabled) buyMenuOpen = false;
    bool menu = paused || buyMenuOpen;
    win.setCursorLocked(!menu && !shot.enabled && win.focused);

    float zoomFov = me.zoom == 1 ? 40.0f : me.zoom == 2 ? 15.0f : 90.0f;
    if (!menu && !shot.enabled) {
        float sens = settings.sensitivity * 0.022f * (me.zoom ? zoomFov / 90.0f * settings.zoomSensitivity : 1.0f);
        viewYaw -= in.mouseDelta.x * sens;
        viewPitch -= in.mouseDelta.y * sens * (settings.invertY ? -1.0f : 1.0f);
    }
    viewPitch = clampf(viewPitch, -89.0f, 89.0f);
    viewYaw = wrapAngle(viewYaw);

    UserCmd c;
    c.pitch = viewPitch;
    c.yaw = viewYaw;
    if (!paused && !shot.enabled) {
        c.forward = (in.down(GLFW_KEY_W) ? 1.0f : 0.0f) - (in.down(GLFW_KEY_S) ? 1.0f : 0.0f);
        c.side = (in.down(GLFW_KEY_D) ? 1.0f : 0.0f) - (in.down(GLFW_KEY_A) ? 1.0f : 0.0f);
        c.jump = in.down(GLFW_KEY_SPACE);
        c.duck = in.down(GLFW_KEY_LEFT_CONTROL);
        c.walk = in.down(GLFW_KEY_LEFT_SHIFT);
        c.use = in.down(GLFW_KEY_E);
        if (!buyMenuOpen) {
            c.attack = in.mouseDown(GLFW_MOUSE_BUTTON_LEFT);
            c.attack2 = in.mouseDown(GLFW_MOUSE_BUTTON_RIGHT);
        }
        c.reload = in.pressed(GLFW_KEY_R);
        c.inspect = in.pressed(GLFW_KEY_F);
        c.drop = in.pressed(GLFW_KEY_G);
        for (int k = 0; k < 5; k++)
            if (in.pressed(GLFW_KEY_1 + k)) c.slot = k;
        if (in.pressed(GLFW_KEY_Q)) c.slot = 12;
        if (!menu && in.scroll > 0) c.slot = 11;
        else if (!menu && in.scroll < 0) c.slot = 10;
    }
    if (shot.enabled) {
        if (shot.frame == 1 && shot.sim > 0) {
            int n = (int)(shot.sim / kTick);
            for (int i = 0; i < n; i++) {
                me.cmd = UserCmd{};
                me.cmd.yaw = viewYaw;
                game.tick(kTick);
                game.events.clear();
            }
        }
        if (shot.hasPos && me.alive) {
            me.mv.origin = me.prevOrigin = shot.pos;
            me.mv.velocity = vec3(0);
        }
        if (shot.hasAng) {
            viewPitch = shot.pitch;
            viewYaw = shot.yaw;
            c.pitch = viewPitch;
            c.yaw = viewYaw;
        }
        if (shot.weapon >= 0 && me.alive) {
            const WeaponDef& d = weaponDef(shot.weapon);
            if (d.cls == WC_GRENADE) me.grenades[grenadeIndex(shot.weapon)] = 1;
            if (shot.weapon == W_C4) me.hasBomb = true;
            if (me.active != shot.weapon) {
                me.slots[d.slot] = shot.weapon;
                me.clip[shot.weapon] = d.magSize;
                me.reserve[shot.weapon] = d.reserve;
                me.active = shot.weapon;
            }
            if (shot.anim != "deploy") me.deployAt = game.time - 5;
        }
        if (shot.fire && shot.frame > 20) c.attack = true;
        if (shot.overlay == "buy") buyMenuOpen = true;
        if (shot.overlay == "pause") paused = true;
    }

    game.localFirstPerson = true;
    game.localMuzzle = vmMuzzleWorld;
    if (!paused) tickAccum += dt;
    int ticks = 0;
    while (tickAccum >= kTick && ticks < 20) {
        me.cmd = c;
        game.tick(kTick);
        tickAccum -= kTick;
        ticks++;
        c.slot = -1;
        c.reload = c.inspect = c.drop = false;
        processEvents();
    }
    if (ticks >= 20) tickAccum = 0;
    if (shot.enabled && !shot.anim.empty() && me.alive) {
        float t = shot.animT;
        const WeaponDef& d = weaponDef(me.active);
        if (shot.anim == "reload") me.reloadStart = game.time - t * d.reloadTime;
        else if (shot.anim == "inspect") me.inspectStart = game.time - t;
        else if (shot.anim == "fire") me.lastShot = game.time - t;
        else if (shot.anim == "deploy") me.deployAt = game.time - t;
        else if (shot.anim == "slash") { me.attackStart = game.time - t; me.attackKind = 0; }
        else if (shot.anim == "stab") { me.attackStart = game.time - t; me.attackKind = 2; }
        else if (shot.anim == "bolt") me.boltStart = game.time - t;
        else if (shot.anim == "throw") { me.throwStart = game.time - t; me.thrown = t > 0.38f; }
        else if (shot.anim == "zoom") me.zoom = (int)t;
    }
    if (game.phase == PH_MATCH_END && game.time > game.phaseEnd && !shot.enabled) {
        quitToLobby();
        return;
    }
    float alpha = saturate(tickAccum / kTick);

    // Camera.
    zoomFov = me.zoom == 1 ? 40.0f : me.zoom == 2 ? 15.0f : 90.0f;
    Camera cam;
    cam.aspect = (float)W / H;
    cam.fovY = Camera::vfovFrom43(zoomFov);
    float sx = 0, sy = 0;
    if (shake > 0) {
        sx = std::sin(game.time * 71.0f) * shake * 0.5f;
        sy = std::cos(game.time * 53.0f) * shake * 0.5f;
        shake = std::max(0.0f, shake - dt * 5.0f);
    }
    viewPunchVel += (-viewPunch * 260.0f - viewPunchVel * 25.0f) * std::min(dt, 0.033f);
    viewPunch += viewPunchVel * std::min(dt, 0.033f);
    if (!me.alive) viewPunch = viewPunchVel = vec3(0);
    float cp = viewPitch, cy = viewYaw;
    bool firstPerson = me.alive && !shot.thirdPerson;
    if (me.alive) {
        vec3 eye = lerp(me.prevOrigin, me.mv.origin, alpha) + vec3(0, 0, eyeHeight(me.mv));
        cp = clampf(viewPitch + me.recoil.y + sy + viewPunch.x, -89, 89);
        cy = viewYaw - me.recoil.x + sx;
        cy += viewPunch.y;
        cam.setAngles(eye, cp, cy, viewPunch.z);
        if (shot.thirdPerson) {
            vec3 front = eye + angleForward(0, viewYaw) * 110.0f + vec3(0, 0, -6);
            cam.lookAtPoint(front, eye - vec3(0, 0, 18));
        }
    } else {
        float since = game.time - me.deathTime;
        if (game.cfg.mode == MODE_COMPETITIVE && since > 3.0f) {
            bool valid = spectate >= 0 && game.players[spectate].alive;
            if (!valid || (in.mousePressed(0) && !menu)) {
                int start = valid ? spectate : -1;
                spectate = -1;
                for (int k = 1; k <= (int)game.players.size(); k++) {
                    int id = (start + k + (int)game.players.size()) % (int)game.players.size();
                    if (game.players[id].alive && game.players[id].team == me.team) { spectate = id; break; }
                }
                if (spectate < 0)
                    for (auto& p : game.players)
                        if (p.alive) { spectate = p.id; break; }
            }
        } else {
            spectate = -1;
        }
        if (spectate >= 0) {
            Player& t = game.players[spectate];
            vec3 te = lerp(t.prevOrigin, t.mv.origin, alpha) + vec3(0, 0, eyeHeight(t.mv));
            vec3 back = te - angleForward(cp, cy) * 110.0f + vec3(0, 0, 14);
            TraceResult tr = map.world.trace(te, back, vec3(-4), vec3(4), MASK_SHOT);
            cam.setAngles(tr.endpos, cp, cy);
        } else {
            vec3 target = me.ragdoll.active ? me.ragdoll.p[0] : me.mv.origin + vec3(0, 0, 20);
            vec3 back = target - angleForward(-30.0f + clampf(viewPitch, -40, 20), viewYaw + since * 8.0f) * 150.0f;
            TraceResult tr = map.world.trace(target + vec3(0, 0, 10), back, vec3(-4), vec3(4), MASK_SHOT);
            cam.lookAtPoint(tr.endpos, target);
        }
    }
    lastCam = cam;
    Camera vmCam = cam;
    vmCam.fovY = Camera::vfovFrom43(settings.vmFov);
    vmCam.znear = 1.0f;
    vmCam.update();

    // Viewmodel.
    std::vector<ModelDraw> vmDraws;
    bool showVm = firstPerson && me.zoom == 0 && me.active != W_NONE;
    if (firstPerson && me.active != W_NONE) {
        const WeaponDef& d = weaponDef(me.active);
        ViewmodelParams vp;
        vp.weapon = me.active;
        vp.time = game.time;
        vp.dt = dt;
        vp.deployT = game.time - me.deployAt;
        vp.deployDur = d.deployTime;
        vp.fireT = game.time - me.lastShot;
        vp.shots = me.shotsFired;
        vp.reloadT = me.reloadStart >= 0 ? game.time - me.reloadStart : -1;
        vp.reloadDur = d.reloadTime;
        vp.inspectT = me.inspectStart >= 0 ? game.time - me.inspectStart : -1;
        vp.attackT = game.time - me.attackStart < 1.0f ? game.time - me.attackStart : -1;
        vp.attackKind = me.attackKind;
        vp.boltT = (me.active == W_AWP && game.time - me.boltStart < 1.5f) ? game.time - me.boltStart : -1;
        vp.throwT = me.throwStart >= 0 ? game.time - me.throwStart : -1;
        vp.slideLocked = d.cls == WC_PISTOL && me.clip[me.active] == 0;
        vp.speed = length(vec2(me.mv.velocity.x, me.mv.velocity.y));
        vp.onGround = me.mv.onGround;
        vp.crouch = me.mv.duckAmount;
        float fdt = std::max(dt, 1e-3f);
        vp.yawDelta = wrapAngle(viewYaw - lastViewYaw) / fdt / 60.0f;
        vp.pitchDelta = (viewPitch - lastViewPitch) / fdt / 60.0f;
        vp.userOffset = vec3(settings.vmOffsetY, -settings.vmOffsetX, settings.vmOffsetZ);
        vp.bob = settings.vmBob;
        Xform eyeX(cam.pos, quatFromEuler(cp, cy, viewPunch.z));
        vec3 au, ad;
        map.sampleAmbient(cam.pos, au, ad);
        buildViewmodelDraws(me.team, vp, eyeX, au, ad, vmDraws);
    }
    lastViewYaw = viewYaw;
    lastViewPitch = viewPitch;

    // World models.
    std::vector<ModelDraw> models;
    boneStore.resize(game.players.size() + 1);
    for (auto& p : game.players) {
        if (p.id == game.local && firstPerson) continue;
        auto& bs = boneStore[p.id];
        ModelDraw d;
        d.mesh = &agents[p.team].mesh;
        d.bones = bs.data();
        d.boneCount = AG_BONES;
        d.patternA = agents[p.team].patternA;
        d.patternB = agents[p.team].patternB;
        if (p.alive) {
            vec3 org = lerp(p.prevOrigin, p.mv.origin, alpha);
            Xform world(org, qaxis({0, 0, 1}, p.yaw * kDeg));
            for (int b = 0; b < AG_BONES; b++) bs[b] = toMat4(world * p.pose.bones[b]);
            map.sampleAmbient(org + vec3(0, 0, 40), d.ambUp, d.ambDown);
            models.push_back(d);
            if (p.pose.hasWeapon && p.active != W_NONE) {
                ModelDraw w = d;
                w.mesh = &weapons[p.active].mesh;
                w.model = toMat4(world * p.pose.weapon);
                w.bones = identityBones;
                w.boneCount = WB_COUNT;
                models.push_back(w);
            }
        } else if (p.ragdoll.active) {
            AgentPose rp;
            p.ragdoll.toPose(rp);
            for (int b = 0; b < AG_BONES; b++) bs[b] = toMat4(rp.bones[b]);
            map.sampleAmbient(p.ragdoll.p[0] + vec3(0, 0, 20), d.ambUp, d.ambDown);
            models.push_back(d);
        }
    }
    auto propDraw = [&](const GpuMesh* mesh, const mat4& model, vec3 pos, bool shadow) {
        ModelDraw d;
        d.mesh = mesh;
        d.model = model;
        d.bones = identityBones;
        d.boneCount = WB_COUNT;
        d.castShadow = shadow;
        map.sampleAmbient(pos + vec3(0, 0, 30), d.ambUp, d.ambDown);
        models.push_back(d);
    };
    for (auto& it : game.items) {
        quat q = it.weapon == W_C4 ? it.rot : it.rot * qaxis({1, 0, 0}, kPi * 0.5f);
        propDraw(&weapons[it.weapon].mesh, toMat4(Xform(it.pos, q)), it.pos, true);
    }
    for (auto& g : game.grenades) propDraw(&weapons[g.type].mesh, toMat4(Xform(g.pos, g.rot)), g.pos, true);
    if (game.bombPlanted && !game.bombExploded) propDraw(&weapons[W_C4].mesh, toMat4(Xform(game.bombPos + vec3(0, 0, 1), qaxis({0, 0, 1}, 0.5f))), game.bombPos, true);
    for (auto& s : game.fx.shells) propDraw(&shellMesh, toMat4(Xform(s.pos, s.rot)) * scale(vec3(s.scale)), s.pos, false);

    std::vector<SpriteVertex> sprites, decals, vmSprites;
    game.fx.buildSprites(cam, sprites, decals, game.time);
    std::vector<PointLight> lights;
    game.fx.collectLights(lights);
    if (game.bombPlanted && !game.bombExploded && !game.bombDefused && std::fmod(game.time, 1.0f) < 0.15f) {
        lights.push_back({game.bombPos + vec3(0, 0, 8), 120.0f, vec3(2.0f, 0.1f, 0.05f)});
        pushBillboard(sprites, cam, game.bombPos + vec3(-0.5f, 0, 2.5f), 3.0f, packRGBA(1, 0.1f, 0.05f, 1), vec4(1, 0, 1, 0));
    }
    if (showVm && game.time - me.lastShot < 0.05f) {
        const WeaponDef& d = weaponDef(me.active);
        if (!d.silenced && d.cls != WC_KNIFE && d.cls != WC_GRENADE && d.cls != WC_BOMB) {
            vec3 dir = rotate(vmWeaponWorld.q, vec3(1, 0, 0));
            float seed = me.shotsFired * 0.37f;
            for (int i = 0; i < 3; i++)
                for (int r = 0; r < 3; r++)
                    pushBillboard(vmSprites, vmCam, vmMuzzleWorld + dir * (1.2f + i * 2.2f), (d.cls == WC_SNIPER ? 4.5f : 3.4f) - i * 0.8f,
                                  packRGBA(1.0f, 0.62f, 0.28f, 1.0f), vec4(2, seed + i * 0.31f, 1, 0));
            lights.push_back({vmMuzzleWorld + dir * 8.0f, 320.0f, vec3(1.0f, 0.62f, 0.3f) * 5.0f});
        }
    }
    for (size_t i = 0; i < scheduled.size();) {
        if (game.time >= scheduled[i].at) {
            if (scheduled[i].spatial) audio.play3D(scheduled[i].snd, scheduled[i].pos, scheduled[i].vol);
            else audio.play(scheduled[i].snd, scheduled[i].vol);
            scheduled.erase(scheduled.begin() + (long)i);
        } else {
            i++;
        }
    }
    audio.setListener(cam.pos, cam.left);

    FrameInput fi;
    fi.cam = cam;
    fi.vmCam = vmCam;
    fi.models = &models;
    fi.vmModels = &vmDraws;
    fi.drawViewmodel = showVm;
    fi.sprites = &sprites;
    fi.vmSprites = &vmSprites;
    fi.decals = &decals;
    fi.lights = settings.dynamicLights ? &lights : nullptr;
    fi.time = game.time;
    vec3 au, ad;
    map.sampleAmbient(cam.pos, au, ad);
    fi.spriteLight = (au + ad) * 0.6f + map.info.env.sunColor * 0.12f;
    if (me.flashUntil > game.time) {
        float k = saturate((me.flashUntil - game.time) / std::min(1.8f, me.flashDur));
        fi.fx.flash = vec4(1, 1, 1, k);
    }
    damageFlash = std::max(0.0f, damageFlash - dt * 2.5f);
    fi.fx.damage = damageFlash * 0.55f;
    fi.fx.desaturate = me.alive ? 0.0f : 0.55f;
    renderer.renderFrame(fi);
    renderer.updateBlur();

    // Spotted enemies for the radar: visible to the local player or any teammate.
    for (auto& p : game.players) {
        spotted[p.id % 32] = false;
        if (!p.alive || !game.isEnemy(me, p)) continue;
        for (auto& q : game.players) {
            if (!q.alive || game.isEnemy(me, q) || !(q.id == me.id || game.cfg.mode == MODE_COMPETITIVE)) continue;
            if (q.bot && q.brain.target == p.id) { spotted[p.id % 32] = true; break; }
            if (q.id == me.id) {
                vec3 to = p.mv.origin + vec3(0, 0, 50) - cam.pos;
                if (dot(normalize(to), cam.fwd) > 0.5f && map.world.traceRay(cam.pos, p.mv.origin + vec3(0, 0, 50), MASK_SHOT).fraction >= 1.0f) {
                    spotted[p.id % 32] = true;
                    break;
                }
            }
        }
    }

    renderer.ldr().bind();
    uiInput(ui, win, shot.enabled);
    theme.s = std::min(W / 1920.0f, H / 1080.0f);
    int hs = ui.hotSound, cs = ui.clickSound;
    ui.begin(W, H, renderer.blurTexture(), theme.s);
    drawHud(dt);
    if (buyMenuOpen) drawBuyMenu();
    if (paused) drawPauseMenu();
    ui.end();
    if (ui.hotSound != hs && menu) audio.play(SND_UI_HOVER, 0.25f);
    if (ui.clickSound != cs) audio.play(SND_UI_CLICK, 0.5f);
    renderer.presentToScreen(win.width(), win.height());
}

bool App::automationStep() {
    shot.frame++;
    if (shot.frame < shot.frames) return false;
    savePNGFromFramebuffer(shot.path, renderer.ldr().fbo, renderer.outWidth(), renderer.outHeight());
    return true;
}
