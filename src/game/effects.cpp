#include "game/effects.h"

#include "assets/textures.h"

float SmokeCloud::radius() const {
    float grow = saturate(time / 1.5f);
    float fade = 1.0f - saturate((time - (duration - 2.0f)) / 2.0f);
    return 150.0f * (0.35f + 0.65f * grow) * (0.5f + 0.5f * fade);
}

void pushBillboard(std::vector<SpriteVertex>& out, const Camera& cam, vec3 pos, float size, uint32_t color, vec4 params) {
    vec3 r = -cam.left * size, u = cam.up * size;
    out.push_back({pos - r - u, {0, 0}, color, params});
    out.push_back({pos + r - u, {1, 0}, color, params});
    out.push_back({pos + r + u, {1, 1}, color, params});
    out.push_back({pos - r + u, {0, 1}, color, params});
}

void Effects::clear() {
    particles.clear();
    decals.clear();
    tracers.clear();
    shells.clear();
    flashes.clear();
    smokes.clear();
}

void Effects::update(float dt, const CollisionWorld& w) {
    for (auto& p : particles) {
        p.life += dt;
        p.vel *= std::exp(-p.drag * dt);
        p.vel.z -= p.gravity * dt;
        p.pos += p.vel * dt;
        p.size += p.grow * dt;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life >= p.maxLife; }), particles.end());
    for (auto& t : tracers) t.life += dt;
    tracers.erase(std::remove_if(tracers.begin(), tracers.end(), [](const Tracer& t) { return t.life >= t.maxLife; }), tracers.end());
    for (auto& f : flashes) f.life += dt;
    flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [](const Flash& f) { return f.life >= f.maxLife; }), flashes.end());
    for (auto& s : smokes) s.time += dt;
    smokes.erase(std::remove_if(smokes.begin(), smokes.end(), [](const SmokeCloud& s) { return s.time >= s.duration; }), smokes.end());
    // Shell casings: simple rigid bodies bouncing on the brush world.
    for (auto& s : shells) {
        s.life += dt;
        if (s.resting) continue;
        s.vel.z -= 800.0f * dt;
        vec3 np = s.pos + s.vel * dt;
        TraceResult tr = w.trace(s.pos, np, vec3(-0.4f), vec3(0.4f), MASK_SHOT);
        if (tr.fraction < 1.0f && !tr.startSolid) {
            s.pos = tr.endpos;
            vec3 vn = tr.normal * dot(s.vel, tr.normal);
            vec3 vt = s.vel - vn;
            s.vel = vt * 0.55f - vn * 0.35f;
            s.angVel *= 0.6f;
            if (length(s.vel) < 20.0f && tr.normal.z > 0.7f) {
                s.resting = true;
                // Lay the casing on its side.
                vec3 ax = rotate(s.rot, vec3(1, 0, 0));
                ax.z = 0;
                s.rot = xformFromBasis(vec3(0), length2(ax) > 1e-4f ? ax : vec3(1, 0, 0), vec3(0, 0, 1)).q;
            }
        } else {
            s.pos = np;
        }
        float av = length(s.angVel);
        if (av > 1e-3f) s.rot = qnormalize(qaxis(s.angVel / av, av * dt) * s.rot);
    }
    shells.erase(std::remove_if(shells.begin(), shells.end(), [](const Shell& s) { return s.life > 12.0f; }), shells.end());
    if (shells.size() > 96) shells.erase(shells.begin(), shells.begin() + (long)(shells.size() - 96));
}

void Effects::muzzleFlash(vec3 pos, vec3 dir, bool silenced) {
    if (silenced) {
        Particle p;
        p.pos = pos;
        p.vel = dir * 30.0f;
        p.maxLife = 0.5f;
        p.size = 2.0f;
        p.grow = 10.0f;
        p.color = {0.7f, 0.7f, 0.7f};
        p.alpha = 0.25f;
        p.shape = 0;
        p.lit = true;
        p.seed = m_rng.f01();
        particles.push_back(p);
        return;
    }
    for (int i = 0; i < 3; i++) {
        Particle p;
        p.pos = pos + dir * (1.5f + i * 2.2f);
        p.maxLife = 0.05f;
        p.size = 3.2f - i * 0.6f;
        p.color = vec3(1.0f, 0.62f, 0.25f) * 6.0f;
        p.shape = 2;
        p.additive = true;
        p.seed = m_rng.f01();
        particles.push_back(p);
    }
    for (int i = 0; i < 3; i++) {
        Particle p;
        p.pos = pos + dir * 2.0f;
        p.vel = dir * m_rng.range(40, 90) + m_rng.unitVec() * 12.0f;
        p.maxLife = m_rng.range(0.5f, 0.9f);
        p.size = 2.0f;
        p.grow = 14.0f;
        p.color = {0.75f, 0.73f, 0.7f};
        p.alpha = 0.22f;
        p.drag = 3.0f;
        p.gravity = -12.0f;
        p.shape = 0;
        p.lit = true;
        p.seed = m_rng.f01();
        particles.push_back(p);
    }
    flashes.push_back({pos + dir * 4.0f, vec3(1.0f, 0.65f, 0.3f) * 6.0f, 260.0f, 0, 0.06f});
}

void Effects::impact(vec3 pos, vec3 n, int surface, bool decal) {
    vec3 dustCol = surface == SURF_WOOD ? vec3(0.45f, 0.33f, 0.2f) : surface == SURF_METAL ? vec3(0.5f, 0.5f, 0.5f)
                 : surface == SURF_SAND ? vec3(0.78f, 0.66f, 0.48f) : vec3(0.72f, 0.66f, 0.56f);
    int puffs = surface == SURF_METAL ? 2 : 5;
    for (int i = 0; i < puffs; i++) {
        Particle p;
        p.pos = pos + n * 1.5f;
        p.vel = (n * m_rng.range(40, 120) + m_rng.unitVec() * 35.0f);
        p.maxLife = m_rng.range(0.6f, 1.3f);
        p.size = m_rng.range(2.0f, 4.0f);
        p.grow = m_rng.range(10, 22);
        p.color = dustCol;
        p.alpha = 0.55f;
        p.drag = 3.5f;
        p.gravity = 40.0f;
        p.lit = true;
        p.seed = m_rng.f01();
        particles.push_back(p);
    }
    if (surface == SURF_METAL || surface == SURF_STONE || surface == SURF_CONCRETE) {
        int sparks = surface == SURF_METAL ? 10 : 4;
        for (int i = 0; i < sparks; i++) {
            Particle p;
            p.pos = pos + n * 0.5f;
            p.vel = normalize(n + m_rng.unitVec() * 0.8f) * m_rng.range(150, 420);
            p.maxLife = m_rng.range(0.12f, 0.35f);
            p.size = 0.5f;
            p.color = vec3(1.0f, 0.75f, 0.35f) * 5.0f;
            p.shape = 1;
            p.additive = true;
            p.gravity = 600.0f;
            p.stretch = true;
            particles.push_back(p);
        }
    }
    if (surface == SURF_WOOD) {
        for (int i = 0; i < 5; i++) {
            Particle p;
            p.pos = pos + n;
            p.vel = normalize(n + m_rng.unitVec() * 0.9f) * m_rng.range(80, 200);
            p.maxLife = m_rng.range(0.4f, 0.8f);
            p.size = 0.7f;
            p.color = {0.35f, 0.24f, 0.13f};
            p.shape = 8;
            p.gravity = 700.0f;
            p.lit = true;
            particles.push_back(p);
        }
    }
    if (decal) addDecal(pos, n, m_rng.range(2.2f, 2.9f), 4, surface == SURF_WOOD ? vec3(0.2f, 0.13f, 0.08f) : vec3(0.18f, 0.16f, 0.14f));
}

void Effects::blood(vec3 pos, vec3 dir, bool headshot, const CollisionWorld& w) {
    int n = headshot ? 10 : 6;
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = pos;
        p.vel = normalize(dir + m_rng.unitVec() * 0.6f) * m_rng.range(60, 180);
        p.maxLife = m_rng.range(0.3f, 0.6f);
        p.size = m_rng.range(1.5f, 3.0f);
        p.grow = 8;
        p.color = {0.45f, 0.02f, 0.02f};
        p.alpha = 0.9f;
        p.shape = 5;
        p.gravity = 500;
        p.drag = 2;
        p.lit = true;
        p.seed = m_rng.f01();
        particles.push_back(p);
    }
    // Splatter on the wall behind the victim.
    TraceResult tr = w.traceRay(pos, pos + normalize(dir) * 120.0f, MASK_SHOT);
    if (tr.fraction < 1.0f) addDecal(tr.endpos, tr.normal, m_rng.range(10, 18), 5, {0.32f, 0.02f, 0.02f});
    TraceResult tf = w.traceRay(pos, pos + vec3(0, 0, -100), MASK_SHOT);
    if (tf.fraction < 1.0f) addDecal(tf.endpos, tf.normal, m_rng.range(8, 14), 5, {0.30f, 0.02f, 0.02f});
}

void Effects::tracer(vec3 a, vec3 b) {
    if (length(b - a) < 80.0f) return;
    tracers.push_back({a, b, 0, std::min(0.12f, length(b - a) / 9000.0f + 0.04f)});
}

void Effects::shell(vec3 pos, vec3 vel, float scale) {
    Shell s;
    s.pos = pos;
    s.vel = vel;
    s.rot = qaxis(m_rng.unitVec(), m_rng.range(0, kTwoPi));
    s.angVel = m_rng.unitVec() * m_rng.range(10, 30);
    s.life = 0;
    s.scale = scale;
    shells.push_back(s);
}

void Effects::explosion(vec3 pos) {
    for (int i = 0; i < 26; i++) {
        Particle p;
        p.pos = pos + m_rng.unitVec() * 10.0f;
        p.vel = m_rng.unitVec() * m_rng.range(120, 420) + vec3(0, 0, 120);
        p.maxLife = m_rng.range(0.25f, 0.6f);
        p.size = m_rng.range(18, 36);
        p.grow = 60;
        p.color = vec3(1.0f, 0.55f, 0.2f) * 5.0f;
        p.shape = 0;
        p.additive = true;
        p.drag = 4;
        p.seed = m_rng.f01();
        particles.push_back(p);
    }
    for (int i = 0; i < 22; i++) {
        Particle p;
        p.pos = pos + m_rng.unitVec() * 20.0f;
        p.vel = m_rng.unitVec() * m_rng.range(60, 260) + vec3(0, 0, 90);
        p.maxLife = m_rng.range(1.5f, 3.2f);
        p.size = m_rng.range(20, 40);
        p.grow = 45;
        p.color = {0.22f, 0.2f, 0.18f};
        p.alpha = 0.7f;
        p.drag = 2.2f;
        p.gravity = -25;
        p.lit = true;
        p.seed = m_rng.f01();
        particles.push_back(p);
    }
    for (int i = 0; i < 30; i++) {
        Particle p;
        p.pos = pos;
        p.vel = m_rng.unitVec() * m_rng.range(300, 900);
        p.maxLife = m_rng.range(0.3f, 0.9f);
        p.size = 0.9f;
        p.color = vec3(1.0f, 0.7f, 0.3f) * 6.0f;
        p.shape = 1;
        p.additive = true;
        p.gravity = 500;
        p.stretch = true;
        particles.push_back(p);
    }
    flashes.push_back({pos + vec3(0, 0, 20), vec3(1.0f, 0.6f, 0.3f) * 14.0f, 700.0f, 0, 0.35f});
    addDecal(pos + vec3(0, 0, 1), {0, 0, 1}, 48, 5, {0.06f, 0.05f, 0.05f});
}

void Effects::smoke(vec3 pos) {
    SmokeCloud c;
    c.pos = pos;
    smokes.push_back(c);
}

void Effects::flashbang(vec3 pos) {
    flashes.push_back({pos, vec3(1.0f) * 40.0f, 900.0f, 0, 0.25f});
    Particle p;
    p.pos = pos;
    p.maxLife = 0.2f;
    p.size = 60;
    p.color = vec3(1.0f) * 10.0f;
    p.shape = 1;
    p.additive = true;
    particles.push_back(p);
}

void Effects::dust(vec3 pos, float amount) {
    int n = (int)(amount * 4);
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = pos + vec3(m_rng.range(-12, 12), m_rng.range(-12, 12), 2);
        p.vel = vec3(m_rng.range(-30, 30), m_rng.range(-30, 30), m_rng.range(10, 40));
        p.maxLife = m_rng.range(0.6f, 1.2f);
        p.size = m_rng.range(4, 7);
        p.grow = 12;
        p.color = {0.78f, 0.67f, 0.5f};
        p.alpha = 0.35f;
        p.drag = 3;
        p.lit = true;
        p.seed = m_rng.f01();
        particles.push_back(p);
    }
}

void Effects::addDecal(vec3 pos, vec3 n, float size, int shape, vec3 color) {
    decals.push_back({pos + n * 0.15f, n, size, shape, color, m_rng.f01()});
    if (decals.size() > 400) decals.erase(decals.begin());
}

void Effects::buildSprites(const Camera& cam, std::vector<SpriteVertex>& out, std::vector<SpriteVertex>& dout, float time) const {
    // Smoke grenades: many large lit puffs, back-to-front.
    struct Puff { vec3 p; float s; float a; float seed; float d; };
    std::vector<Puff> puffs;
    for (size_t si = 0; si < smokes.size(); si++) {
        const SmokeCloud& c = smokes[si];
        float r = c.radius();
        float fade = 1.0f - saturate((c.time - (c.duration - 2.0f)) / 2.0f);
        Rng rr(1000 + si * 7919);
        for (int i = 0; i < 44; i++) {
            vec3 off = rr.unitVec() * (rr.f01() * 0.8f);
            off.z = std::fabs(off.z) * 0.8f;
            vec3 p = c.pos + off * r + vec3(0, 0, 20) + vec3(std::sin(time * 0.3f + i) * 6.0f, std::cos(time * 0.27f + i) * 6.0f, 0);
            puffs.push_back({p, r * rr.range(0.45f, 0.7f), 0.8f * fade, rr.f01(), length2(p - cam.pos)});
        }
    }
    std::sort(puffs.begin(), puffs.end(), [](const Puff& a, const Puff& b) { return a.d > b.d; });
    for (auto& p : puffs) pushBillboard(out, cam, p.p, p.s, packRGBA(0.62f, 0.63f, 0.65f, p.a), vec4(6, p.seed, 0, 1));

    std::vector<const Particle*> sorted;
    sorted.reserve(particles.size());
    for (auto& p : particles) sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(), [&](const Particle* a, const Particle* b) { return length2(a->pos - cam.pos) > length2(b->pos - cam.pos); });
    for (const Particle* pp : sorted) {
        const Particle& p = *pp;
        float t = p.life / p.maxLife;
        float a = p.alpha * (1.0f - t) * (p.shape == 0 ? smooth01(t * 6.0f) : 1.0f);
        vec3 c = p.color;
        // HDR colors are compressed into 8 bits: brightness above 1 is carried by the sprite shape shader.
        float mx = std::max(c.x, std::max(c.y, c.z));
        float boost = mx > 1.0f ? mx : 1.0f;
        uint32_t col = packRGBA(c.x / boost, c.y / boost, c.z / boost, a);
        vec4 params((float)p.shape, p.seed, p.additive ? 1.0f : 0.0f, p.lit ? 1.0f : 0.0f);
        if (p.stretch) {
            vec3 d = normalize(p.vel);
            vec3 side = normalize(cross(d, cam.pos - p.pos)) * p.size;
            vec3 tail = p.pos - p.vel * 0.02f;
            out.push_back({tail - side, {0, 0}, col, vec4(1, 0, 1, 0)});
            out.push_back({p.pos - side, {1, 0}, col, vec4(1, 0, 1, 0)});
            out.push_back({p.pos + side, {1, 1}, col, vec4(1, 0, 1, 0)});
            out.push_back({tail + side, {0, 1}, col, vec4(1, 0, 1, 0)});
            continue;
        }
        int reps = boost > 1.5f ? std::min(4, (int)boost) : 1;
        for (int r = 0; r < reps; r++) pushBillboard(out, cam, p.pos, p.size, col, params);
    }
    for (const Tracer& t : tracers) {
        float k = t.life / t.maxLife;
        vec3 a = lerp(t.a, t.b, k * 0.7f), b = lerp(t.a, t.b, std::min(1.0f, k * 0.7f + 0.3f));
        vec3 d = normalize(b - a);
        vec3 side = normalize(cross(d, cam.pos - a)) * 0.6f;
        uint32_t col = packRGBA(1.0f, 0.85f, 0.55f, 0.9f);
        vec4 prm(3, 0, 1, 0);
        for (int r = 0; r < 2; r++) {
            out.push_back({a - side, {0, 0}, col, prm});
            out.push_back({b - side, {1, 0}, col, prm});
            out.push_back({b + side, {1, 1}, col, prm});
            out.push_back({a + side, {0, 1}, col, prm});
        }
    }
    for (const Decal& d : decals) {
        vec3 t = anyPerp(d.normal), b = cross(d.normal, t);
        float ang = d.seed * kTwoPi;
        vec3 tt = t * std::cos(ang) + b * std::sin(ang), bb = cross(d.normal, tt);
        tt *= d.size;
        bb *= d.size;
        uint32_t col = packRGBA(d.color.x, d.color.y, d.color.z, 0.95f);
        vec4 prm((float)d.shape, d.seed, 0, 0);
        dout.push_back({d.pos - tt - bb, {0, 0}, col, prm});
        dout.push_back({d.pos + tt - bb, {1, 0}, col, prm});
        dout.push_back({d.pos + tt + bb, {1, 1}, col, prm});
        dout.push_back({d.pos - tt + bb, {0, 1}, col, prm});
    }
}

void Effects::collectLights(std::vector<PointLight>& out) const {
    for (const Flash& f : flashes) {
        float k = 1.0f - f.life / f.maxLife;
        out.push_back({f.pos, f.radius, f.color * k});
    }
}

bool Effects::smokeBlocks(vec3 a, vec3 b) const {
    for (const SmokeCloud& c : smokes) {
        if (c.time < 1.0f || c.time > c.duration - 1.5f) continue;
        vec3 center = c.pos + vec3(0, 0, 48);
        float r = c.radius() * 0.85f;
        vec3 d = b - a;
        float len = length(d);
        if (len < 1e-3f) continue;
        d /= len;
        float t = clampf(dot(center - a, d), 0.0f, len);
        if (length2(a + d * t - center) < r * r) return true;
    }
    return false;
}
