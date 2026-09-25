// Viewmodel lab: renders first-person arms and weapon without loading a map, from debug cameras.
// Used through automation only: --scene vmlab --vmview main|side|right|top|front|close.
#include <cstdio>
#include <map>

#include "app.h"

void App::buildViewmodelDraws(int team, const ViewmodelParams& vp, const Xform& eye, vec3 ambUp, vec3 ambDown,
                              std::vector<ModelDraw>& out) {
    const WeaponModel& wm = weapons[vp.weapon];
    animateViewmodel(wm, vp, vmState, vmPose);
    buildArmBones(arms[team], wm, vmPose, eye, armBones);
    vmWeaponWorld = eye * vmPose.weapon;
    vmMuzzleWorld = apply(vmWeaponWorld, wm.muzzle);
    ModelDraw a;
    a.mesh = &arms[team].mesh;
    a.bones = armBones;
    a.boneCount = AB_PER_ARM * 2;
    a.patternA = arms[team].patternA;
    a.patternB = arms[team].patternB;
    a.ambUp = ambUp;
    a.ambDown = ambDown;
    a.castShadow = false;
    out.push_back(a);
    if (vmPose.weaponVisible) {
        ModelDraw w = a;
        w.mesh = &wm.mesh;
        w.model = toMat4(vmWeaponWorld);
        w.bones = vmPose.weaponBones;
        w.boneCount = WB_COUNT;
        out.push_back(w);
    }
}

void App::frameVmLab(float dt) {
    static bool envReady = false;
    if (!envReady) {
        GameMap env;
        buildMapById("dust", env);
        renderer.setEnvironment(env.info.env);
        envReady = true;
    }
    applyVideoSettings();
    int W = renderer.outWidth(), H = renderer.outHeight();
    int team = shot.team == TEAM_T ? TEAM_T : TEAM_CT;
    int weapon = shot.weapon >= 0 ? shot.weapon : W_AK47;
    float t = shot.frame / 60.0f;

    Camera cam;
    cam.aspect = (float)W / H;
    cam.fovY = Camera::vfovFrom43(90.0f);
    vec3 eyePos(0, 0, 64);
    cam.setAngles(eyePos, 0, 0);
    Camera vmCam = cam;
    vmCam.fovY = Camera::vfovFrom43(settings.vmFov);
    vmCam.znear = 1.0f;
    vmCam.update();

    if (!shot.vmHold.empty()) {
        float h[6] = {};
        if (std::sscanf(shot.vmHold.c_str(), "%f,%f,%f,%f,%f,%f", &h[0], &h[1], &h[2], &h[3], &h[4], &h[5]) == 6)
            weapons[weapon].viewHold = Xform(vec3(h[0], h[1], h[2]), quatFromEuler(h[3], h[4], h[5]));
    }
    if (!shot.vmRig.empty()) {
        // Grip overrides keep the stored hand-local contact point and replace position/orientation.
        WeaponModel& wm = weapons[weapon];
        std::map<std::string, vec3> kv;
        size_t pos = 0;
        while (pos < shot.vmRig.size()) {
            size_t end = shot.vmRig.find(';', pos);
            std::string item = shot.vmRig.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            size_t eq = item.find('=');
            vec3 v;
            if (eq != std::string::npos && std::sscanf(item.c_str() + eq + 1, "%f,%f,%f", &v.x, &v.y, &v.z) == 3) kv[item.substr(0, eq)] = v;
            if (end == std::string::npos) break;
            pos = end + 1;
        }
        auto regrip = [&](Xform& g, const char* pk, const char* fk, const char* dk) {
            if (!kv.count(fk) || !kv.count(dk)) return;
            vec3 contact = kv.count(pk) ? kv[pk] : vec3(0);
            Xform h = xformFromBasis(vec3(0), normalize(kv[fk]), normalize(kv[dk]));
            vec3 localPt = kv.count(std::string(pk) + "_local") ? kv[std::string(pk) + "_local"] : vec3(2.35f, 0.1f, -1.38f);
            h.p = contact - rotate(h.q, localPt);
            g = h;
        };
        regrip(wm.leftGrip, "lgrip", "lfinger", "ldorsal");
        regrip(wm.rightGrip, "rgrip", "rfinger", "rdorsal");
        auto thumb = [&](FingerPose& fp, const char* k, const char* k2) {
            if (kv.count(k)) { fp.thumbTwist = kv[k].x; fp.spread[4] = kv[k].y; fp.curl[4][0] = kv[k].z; }
            if (kv.count(k2)) { fp.curl[4][1] = kv[k2].x; fp.curl[4][2] = kv[k2].y; }
        };
        thumb(wm.leftPose, "lthumb", "lthumb2");
        thumb(wm.rightPose, "rthumb", "rthumb2");
        auto curls = [&](FingerPose& fp, const char* k) {
            if (!kv.count(k)) return;
            for (int f = 0; f < 4; f++) { fp.curl[f][0] = kv[k].x; fp.curl[f][1] = kv[k].y; fp.curl[f][2] = kv[k].z; }
        };
        curls(wm.leftPose, "lcurl");
        curls(wm.rightPose, "rcurl");
        if (kv.count("lsh")) wm.shoulderL = kv["lsh"];
        if (kv.count("rsh")) wm.shoulderR = kv["rsh"];
        if (kv.count("lpole")) wm.poleL = kv["lpole"];
        if (kv.count("rpole")) wm.poleR = kv["rpole"];
    }
    const WeaponDef& d = weaponDef(weapon);
    ViewmodelParams vp;
    vp.weapon = weapon;
    vp.time = t;
    vp.dt = dt;
    vp.deployT = 10;
    vp.deployDur = d.deployTime;
    vp.reloadDur = d.reloadTime;
    vp.userOffset = vec3(settings.vmOffsetY, -settings.vmOffsetX, settings.vmOffsetZ);
    vp.bob = settings.vmBob;
    float at = shot.animT;
    if (shot.anim == "reload") vp.reloadT = at * d.reloadTime;
    else if (shot.anim == "inspect") vp.inspectT = at;
    else if (shot.anim == "fire") { vp.fireT = at; vp.shots = 3; }
    else if (shot.anim == "deploy") vp.deployT = at;
    else if (shot.anim == "slash") { vp.attackT = at; vp.attackKind = 0; }
    else if (shot.anim == "stab") { vp.attackT = at; vp.attackKind = 2; }
    else if (shot.anim == "bolt") vp.boltT = at;
    else if (shot.anim == "throw") vp.throwT = at;
    Xform eye(eyePos, quatFromEuler(0, 0, 0));
    std::vector<ModelDraw> draws;
    vmState = ViewmodelState();
    buildViewmodelDraws(team, vp, eye, vec3(0.55f, 0.52f, 0.5f), vec3(0.32f, 0.28f, 0.24f), draws);
    if (shot.vmClay && !draws.empty()) draws[0].tint = vec4(3.5f, 3.5f, 3.5f, 1.0f);  // brighten arms to judge shape

    // Report screen positions of key rig points (main view) for numeric tuning.
    if (shot.frame + 1 == shot.frames) {
        auto scr = [&](vec3 p, const char* name) {
            vec4 c = vmCam.viewProj * vec4(p, 1.0f);
            if (c.w <= 0) { std::printf("vmlab %-10s behind camera\n", name); return; }
            std::printf("vmlab %-10s screen %.3f %.3f  depth %.1f\n", name, 0.5f + 0.5f * c.x / c.w, 0.5f - 0.5f * c.y / c.w, c.w);
        };
        scr(vmMuzzleWorld, "muzzle");
        scr(apply(vmWeaponWorld, vec3(0)), "origin");
        scr(apply(eye, vmPose.rightHand.p), "r.wrist");
        scr(apply(eye, vmPose.leftHand.p), "l.wrist");
        scr(xformPoint(armBones[AB_FORE], vec3(0)), "r.elbow");
        scr(xformPoint(armBones[AB_PER_ARM + AB_FORE], vec3(0)), "l.elbow");
        std::fflush(stdout);
    }

    std::string view = shot.vmView;
    FrameInput fi;
    fi.drawWorld = false;
    fi.time = t;
    std::vector<ModelDraw> none;
    std::vector<SpriteVertex> noSprites;
    std::vector<PointLight> noLights;
    fi.sprites = &noSprites;
    fi.vmSprites = &noSprites;
    fi.decals = &noSprites;
    fi.lights = &noLights;
    fi.spriteLight = vec3(1);
    if (view == "main" || view.empty() || view == "close") {
        if (view == "close") {
            vmCam.fovY = Camera::vfovFrom43(settings.vmFov * 0.5f);
            vmCam.lookAtPoint(eyePos, apply(eye, vmPose.leftHand.p) + vec3(3, 0, 0));
            vmCam.znear = 1.0f;
            vmCam.update();
            cam = vmCam;
        }
        fi.cam = cam;
        fi.vmCam = vmCam;
        fi.models = &none;
        fi.vmModels = &draws;
        fi.drawViewmodel = true;
    } else if (view.rfind("hand", 0) == 0) {
        // hand<L|R>:<yaw>:<pitch> orbits the palm center at close range.
        char side = view.size() > 4 ? view[4] : 'L';
        float oy = 0, op = 20;
        std::sscanf(view.c_str() + std::min<size_t>(view.size(), 6), "%f:%f", &oy, &op);
        const Xform& hx = side == 'R' ? vmPose.rightHand : vmPose.leftHand;
        vec3 focus = apply(eye, apply(hx, vec3(2.0f, 0, 0)));
        Camera dc;
        dc.aspect = cam.aspect;
        dc.fovY = 32.0f * kDeg;
        dc.znear = 0.5f;
        dc.lookAtPoint(focus + angleForward(op, oy) * 14.0f, focus);
        fi.cam = dc;
        fi.vmCam = dc;
        fi.models = &draws;
        fi.vmModels = &none;
        fi.drawViewmodel = false;
    } else {
        vec3 focus = eyePos + vec3(18, -5, -7);
        Camera dc;
        dc.aspect = cam.aspect;
        dc.fovY = 40.0f * kDeg;
        dc.znear = 1.0f;
        vec3 from = view == "side" ? focus + vec3(0, 60, 4) : view == "right" ? focus + vec3(0, -60, 4)
                  : view == "top" ? focus + vec3(-0.01f, 0, 70) : focus + vec3(60, 0, 6);
        dc.lookAtPoint(from, focus);
        fi.cam = dc;
        fi.vmCam = dc;
        fi.models = &draws;
        fi.vmModels = &none;
        fi.drawViewmodel = false;
    }
    renderer.renderFrame(fi);
    renderer.presentToScreen(win.width(), win.height());
}
