// Software mixer with procedurally synthesized sounds (miniaudio is used only as the output device).
#pragma once
#include <memory>

#include "core/math.h"

enum SoundId {
    SND_AK, SND_M4, SND_AWP, SND_DEAGLE, SND_GLOCK, SND_USP,
    SND_KNIFE_SWING, SND_KNIFE_HIT, SND_KNIFE_STAB, SND_EMPTY, SND_RELOAD_OUT, SND_RELOAD_IN, SND_BOLT, SND_DEPLOY, SND_ZOOM,
    SND_STEP0, SND_STEP1, SND_STEP2, SND_STEP3, SND_JUMP, SND_LAND,
    SND_HIT_BODY, SND_HIT_HEAD, SND_HELMET,
    SND_IMPACT_STONE, SND_IMPACT_WOOD, SND_IMPACT_METAL, SND_IMPACT_SAND,
    SND_EXPLOSION, SND_SMOKE, SND_FLASH, SND_BOUNCE, SND_THROW,
    SND_BEEP, SND_PLANTED, SND_DEFUSED,
    SND_ROUND_START, SND_ROUND_WIN, SND_ROUND_LOSE,
    SND_UI_HOVER, SND_UI_CLICK, SND_BUY, SND_PICKUP, SND_SHELL, SND_KILL,
    SND_COUNT
};

class Audio {
public:
    Audio();
    ~Audio();
    bool init();
    void shutdown();
    bool available() const;
    void play(int id, float volume = 1.0f, float pitch = 1.0f);
    void play3D(int id, vec3 pos, float volume = 1.0f, float pitch = 1.0f, float refDist = 180.0f);
    void setListener(vec3 pos, vec3 left);
    void setVolume(float master);
    void stopAll();

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};
