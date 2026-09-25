// Procedurally generated world materials (albedo+roughness, normal+cavity+metalness).
#pragma once
#include <cstdint>
#include <vector>

#include "core/math.h"

enum WorldMaterial {
    MAT_SAND = 0,
    MAT_SANDSTONE,
    MAT_PLASTER,
    MAT_TILES,
    MAT_CRATE,
    MAT_DOOR,
    MAT_METAL,
    MAT_CONCRETE,
    MAT_TRIM,
    MAT_BRICK,
    MAT_CORRUGATED,
    MAT_COBBLE,
    MAT_COUNT
};

enum SurfaceType { SURF_STONE = 0, SURF_SAND, SURF_WOOD, SURF_METAL, SURF_CONCRETE, SURF_FLESH };

struct MaterialInfo {
    const char* name;
    int surface;
    float bump;
    vec3 avgAlbedo;  // linear, filled after generation (used by the light baker)
};

MaterialInfo& materialInfo(int m);

struct WorldTextureSet {
    int size = 0;
    std::vector<std::vector<uint8_t>> albedo;  // RGBA8: sRGB color + roughness
    std::vector<std::vector<uint8_t>> normal;  // RGBA8: normal xy, cavity, metalness
};

void generateWorldTextures(WorldTextureSet& out, int size);
