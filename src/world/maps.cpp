// Map content. Units are Hammer units (1 unit = 1 inch), Z up.
#include "assets/textures.h"
#include "world/map_builder.h"

namespace {

uint32_t rgb(float r, float g, float b) { return MapBuilder::rgb(r, g, b); }

void door(MapBuilder& b, vec3 hinge, float len, float yawDeg, float height) {
    vec3 dir(std::cos(yawDeg * kDeg), std::sin(yawDeg * kDeg), 0);
    b.obox(hinge + dir * (len * 0.5f) + vec3(0, 0, height * 0.5f), {len * 0.5f, 3.5f, height * 0.5f}, yawDeg, MAT_DOOR);
}

void barrel(MapBuilder& b, float x, float y, uint32_t tint) {
    Brush& br = b.prism(x, y, 16, 10, 0, 50, MAT_METAL);
    br.tint = tint;
}

void buildDust(GameMap& m) {
    m.info.id = "dust";
    m.info.title = "Dust II";
    m.info.subtitle = "Классика: длинный A, мид, туннели B";
    m.info.env = Environment();
    m.info.lobbyCamPos = {1480, 1260, 140};
    m.info.lobbyCamPitch = -6;
    m.info.lobbyCamYaw = 150;
    m.info.thumbPos = {1500, -880, 190};
    m.info.thumbPitch = -8;
    m.info.thumbYaw = 98;

    MapBuilder b(m, -2048, -2176, 2048, 2048, 32);
    b.wallStyle(-2048, -2176, 2048, 2048, 288, MAT_PLASTER, MAT_SANDSTONE, 80);
    b.wallStyle(-1728, -2112, -1024, -320, 320, MAT_SANDSTONE, MAT_TRIM, 40);
    b.wallStyle(1088, -2112, 1856, 400, 256, MAT_SANDSTONE, MAT_TRIM, 40);
    b.wallStyle(-640, 1216, 736, 2048, 320, MAT_PLASTER, MAT_TRIM, 56);

    // T spawn and the route to Long.
    b.carve(-640, -2048, 704, -1600, 0, MAT_SAND);
    b.carve(704, -1984, 1152, -1664, 0, MAT_SAND);
    b.carve(1152, -1984, 1600, -1056, 0, MAT_SAND);
    b.carve(1280, -1056, 1440, -992, 0, MAT_TILES);
    b.roof(1280, -1056, 1440, -992, 144, MAT_TRIM);
    // Long A and A site.
    b.carve(1152, -992, 1792, 320, 0, MAT_SAND);
    b.carve(1216, 320, 1792, 1024, 0, MAT_SAND);
    b.carve(640, 1024, 1792, 1920, 0, MAT_TILES);
    b.carve(832, 1216, 1600, 1856, 48, MAT_TILES);
    // Mid, mid doors, CT mid, CT spawn.
    b.carve(-128, -1600, 128, -1536, 0, MAT_SAND);
    b.carve(-256, -1536, 256, 832, 0, MAT_SAND);
    b.carve(-96, 832, 96, 896, 0, MAT_TILES);
    b.roof(-96, 832, 96, 896, 144, MAT_TRIM);
    b.carve(-256, 896, 256, 1280, 0, MAT_TILES);
    b.carve(-576, 1280, 448, 1920, 0, MAT_TILES);
    b.carve(448, 1280, 704, 1600, 0, MAT_TILES);
    // Catwalk / short A (raised).
    b.carve(256, -96, 320, 64, 0, MAT_TILES);
    b.carve(320, -96, 512, 832, 64, MAT_TILES);
    b.carve(512, 640, 704, 832, 64, MAT_TILES);
    b.carve(576, 832, 704, 1024, 0, MAT_TILES);
    // CT -> B with B doors.
    b.carve(-1216, 1408, -576, 1728, 0, MAT_TILES);
    b.fill(-1024, 1408, -960, 1728);
    b.carve(-1024, 1472, -960, 1664, 0, MAT_TILES);
    b.roof(-1024, 1472, -960, 1664, 160, MAT_TRIM);
    // B site with back platform.
    b.carve(-1920, 640, -1216, 1920, 0, MAT_TILES);
    b.carve(-1920, 1600, -1600, 1920, 32, MAT_TILES);
    // Tunnels.
    b.carve(-1152, -1984, -640, -1728, 0, MAT_SAND);
    b.carve(-1600, -1984, -1152, -448, 0, MAT_COBBLE);
    b.roof(-1600, -1792, -1152, -448, 176, MAT_TRIM);
    b.carve(-1600, -448, -1344, 640, 0, MAT_COBBLE);
    b.roof(-1600, -448, -1344, 320, 176, MAT_TRIM);
    b.carve(-1152, -960, -256, -768, 0, MAT_COBBLE);
    b.roof(-1152, -960, -384, -768, 144, MAT_TRIM);

    // Ramps and stairs.
    b.wedge({1600, 1344, 0}, {1728, 1600, 48}, 1, MAT_TILES);
    b.stairs(896, 1120, 1088, 1216, 0, 3, 16, 2, MAT_TRIM);
    b.stairs(192, -96, 320, 64, 0, 4, 16, 0, MAT_TRIM);
    b.wedge({576, 832, 0}, {704, 1024, 64}, 3, MAT_TILES);
    b.stairs(-1856, 1536, -1664, 1600, 0, 2, 16, 2, MAT_TRIM);

    // Doors.
    door(b, {-96, 896, 0}, 92, 90, 136);
    door(b, {96, 896, 0}, 92, 135, 136);
    door(b, {1280, -992, 0}, 78, 90, 140);
    door(b, {1440, -992, 0}, 78, 125, 140);
    door(b, {-1024, 1472, 0}, 92, 200, 150);
    door(b, {-1024, 1664, 0}, 92, 160, 150);

    // Cover.
    b.crate(-560, -1690, 0, 64);
    b.crate(-496, -1690, 0, 64);
    b.crate(-528, -1690, 64, 56, 14);
    b.crate(420, -1760, 0, 64, 12);
    b.crate(0, -712, 0, 64);
    b.crate(-60, -690, 0, 48, 20);
    b.crate(-200, 740, 0, 64);
    b.crate(1240, -560, 0, 64);
    b.crate(1240, -496, 0, 64);
    b.crate(1240, -528, 64, 64);
    b.crate(1700, -300, 0, 64, 20);
    b.crate(1728, 240, 0, 64);
    b.crate(1728, 176, 0, 64);
    b.crate(1150, 1500, 48, 64);
    b.crate(1214, 1500, 48, 64);
    b.crate(1182, 1500, 112, 52, 15);
    b.crate(1540, 1790, 48, 64);
    b.crate(1476, 1790, 48, 64);
    b.crate(330, 1856, 0, 64);
    b.crate(394, 1856, 0, 64);
    b.crate(362, 1856, 64, 64);
    b.crate(-400, 1480, 0, 64, 30);
    b.crate(-1500, 1150, 0, 64);
    b.crate(-1436, 1150, 0, 64);
    b.crate(-1468, 1150, 64, 64);
    b.crate(-1330, 1560, 0, 64, 20);
    b.crate(-1780, 880, 0, 64);
    b.crate(-1420, -1260, 0, 64);
    b.box({-1700, 1250, 0}, {-1604, 1346, 80}, MAT_TRIM);
    barrel(b, 720, 1150, rgb(0.55f, 0.18f, 0.12f));
    barrel(b, 756, 1186, rgb(0.25f, 0.33f, 0.45f));
    barrel(b, -1260, 760, rgb(0.55f, 0.18f, 0.12f));
    barrel(b, 1660, -760, rgb(0.30f, 0.40f, 0.28f));
    // A car.
    uint32_t carTint = rgb(0.62f, 0.24f, 0.16f);
    b.box({1300, 640, 14}, {1460, 720, 50}, MAT_METAL, carTint);
    b.box({1336, 646, 50}, {1420, 714, 80}, MAT_METAL, carTint);
    for (float wx : {1320.0f, 1440.0f})
        for (float wy : {640.0f, 720.0f}) b.box({wx - 12, wy - 6, 0}, {wx + 12, wy + 6, 24}, MAT_METAL, rgb(0.12f, 0.12f, 0.12f));

    for (int i = 0; i < 10; i++) {
        b.spawn(TEAM_T, -256.0f + (i % 5) * 128.0f, -1920.0f + (i / 5) * 120.0f, 0, 90);
        b.spawn(TEAM_CT, -384.0f + (i % 5) * 128.0f, 1640.0f + (i / 5) * 120.0f, 0, -90);
    }
    b.site('A', 832, 1216, 1600, 1856);
    b.site('B', -1856, 704, -1280, 1856);
    b.finish();
}

void buildArena(GameMap& m) {
    m.info.id = "arena";
    m.info.title = "Arena";
    m.info.subtitle = "Контейнерный двор на закате";
    Environment e;
    e.sunDir = normalize(vec3(-0.62f, 0.35f, 0.34f));
    e.sunColor = vec3(1.0f, 0.72f, 0.48f) * 3.4f;
    e.skyZenith = {0.16f, 0.26f, 0.50f};
    e.skyHorizon = {0.90f, 0.62f, 0.44f};
    e.groundColor = {0.30f, 0.25f, 0.22f};
    e.fogColor = {0.78f, 0.60f, 0.50f};
    e.fogDensity = 0.00008f;
    e.grade = {1.04f, 0.99f, 0.94f};
    e.cloudiness = 0.55f;
    m.info.env = e;
    m.info.lobbyCamPos = {-900, -700, 150};
    m.info.lobbyCamPitch = -5;
    m.info.lobbyCamYaw = 40;
    m.info.thumbPos = {-1000, -1000, 260};
    m.info.thumbPitch = -10;
    m.info.thumbYaw = 45;

    MapBuilder b(m, -1344, -1344, 1344, 1344, 32);
    b.wallStyle(-1344, -1344, 1344, 1344, 256, MAT_CONCRETE, MAT_BRICK, 96);
    b.carve(-1152, -1152, 1152, 1152, 0, MAT_CONCRETE);
    b.carve(-256, -256, 256, 256, 64, MAT_CONCRETE);
    b.wedge({-96, 256, 0}, {96, 448, 64}, 3, MAT_CONCRETE);
    b.wedge({-96, -448, 0}, {96, -256, 64}, 2, MAT_CONCRETE);
    b.stairs(256, -64, 384, 64, 0, 4, 16, 1, MAT_TRIM);
    b.stairs(-384, -64, -256, 64, 0, 4, 16, 0, MAT_TRIM);

    auto container = [&](float x, float y, bool alongY, uint32_t tint, float z) {
        vec3 h = alongY ? vec3(60, 150, 64) : vec3(150, 60, 64);
        b.box({x - h.x, y - h.y, z}, {x + h.x, y + h.y, z + 128}, MAT_CORRUGATED, tint);
    };
    uint32_t red = rgb(0.62f, 0.17f, 0.12f), blue = rgb(0.16f, 0.33f, 0.58f), green = rgb(0.22f, 0.44f, 0.27f);
    uint32_t orange = rgb(0.85f, 0.47f, 0.14f), grey = rgb(0.58f, 0.59f, 0.60f);
    container(-640, 360, true, red, 0);
    container(640, -360, true, red, 0);
    container(640, 360, true, blue, 0);
    container(-640, -360, true, blue, 0);
    container(-880, 880, false, green, 0);
    container(-880, 880, false, orange, 128);
    container(880, -880, false, green, 0);
    container(880, -880, false, orange, 128);
    container(880, 880, false, grey, 0);
    container(-880, -880, false, grey, 0);
    container(0, 780, false, blue, 0);
    container(0, -780, false, red, 0);
    b.crate(-420, 0, 0, 64);
    b.crate(420, 0, 0, 64, 25);
    b.crate(-300, 620, 0, 64, 10);
    b.crate(300, -620, 0, 64, -10);
    b.crate(900, 300, 0, 64);
    b.crate(900, 364, 0, 64);
    b.crate(-900, -300, 0, 64);
    b.crate(-900, -364, 0, 64);
    b.crate(-160, 160, 64, 48, 30);
    b.crate(160, -160, 64, 48, 60);
    barrel(b, -1080, 520, rgb(0.20f, 0.30f, 0.50f));
    barrel(b, 1080, -520, rgb(0.55f, 0.18f, 0.12f));

    for (int i = 0; i < 10; i++) {
        b.spawn(TEAM_T, -320.0f + (i % 5) * 160.0f, -1060.0f + (i / 5) * 70.0f, 0, 90);
        b.spawn(TEAM_CT, -320.0f + (i % 5) * 160.0f, 1060.0f - (i / 5) * 70.0f, 0, -90);
    }
    b.site('A', 500, -300, 1100, 300);
    b.site('B', -1100, -300, -500, 300);
    b.finish();
}

}  // namespace

const std::vector<MapListEntry>& mapList() {
    static const std::vector<MapListEntry> list = {
        {"dust", "Dust II", "Классика: длинный A, мид, туннели B"},
        {"arena", "Arena", "Контейнерный двор на закате"},
    };
    return list;
}

bool buildMapById(const std::string& id, GameMap& m) {
    if (id == "dust") buildDust(m);
    else if (id == "arena") buildArena(m);
    else return false;
    return true;
}
