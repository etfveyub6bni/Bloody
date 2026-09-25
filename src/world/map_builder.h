// Authoring helper: paint walkable areas on a coarse grid, walls/floors/ceilings are generated.
#pragma once
#include "world/map.h"

class MapBuilder {
public:
    MapBuilder(GameMap& map, float x0, float y0, float x1, float y1, float cell = 32.0f);

    void carve(float x0, float y0, float x1, float y1, float floorZ, int mat);
    void fill(float x0, float y0, float x1, float y1);
    void roof(float x0, float y0, float x1, float y1, float ceilZ, int mat);
    void wallStyle(float x0, float y0, float x1, float y1, float top, int mat, int lowerMat = -1, float band = 0);

    Brush& box(vec3 mn, vec3 mx, int mat, uint32_t tint = 0xFFFFFFFFu);
    Brush& crate(float x, float y, float z, float size, float yaw = 0);
    Brush& wedge(vec3 mn, vec3 mx, int riseDir, int mat);
    void stairs(float x0, float y0, float x1, float y1, float z0, int steps, float rise, int riseDir, int mat);
    Brush& prism(float x, float y, float r, int sides, float z0, float z1, int mat, float rot = 0);
    Brush& obox(vec3 c, vec3 half, float yaw, int mat, uint32_t tint = 0xFFFFFFFFu);
    void clipBox(vec3 mn, vec3 mx);
    void spawn(int team, float x, float y, float z, float yaw);
    void site(char letter, float x0, float y0, float x1, float y1);
    void finish();

    static uint32_t rgb(float r, float g, float b);

private:
    struct Cell {
        bool walk = false;
        float floorZ = 0;
        int floorMat = 0;
        bool roofed = false;
        float ceilZ = 0;
        int ceilMat = 0;
        float top = 288;
        int wallMat = 0;
        int lowerMat = -1;
        float band = 0;
    };
    Brush& add(Brush b, int mat, uint32_t tint);
    uint32_t jitterTint(uint32_t tint);
    template <class Elig, class Same, class Emit>
    void greedy(Elig elig, Same same, Emit emit);
    void trims();

    GameMap& m_map;
    float m_x0, m_y0, m_cell;
    int m_w, m_h;
    std::vector<Cell> m_cells;
    Rng m_rng{1234};
};
