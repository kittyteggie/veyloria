#include "worldgen.h"

#include "blocks.h"

#include <math.h>

// SUMMARY: Implements seeded noise, terrain layering, and tree placement.

#define WORLDGEN_SCALE 0.018f
#define WORLDGEN_BASE 28
#define WORLDGEN_AMPLITUDE 16
#define WORLDGEN_DIRT_DEPTH 4
#define WORLDGEN_OCTAVES 4
#define WORLDGEN_TREE_CELL 9
#define WORLDGEN_TREE_DENSITY 44
#define WORLDGEN_TRUNK_MIN 4
#define WORLDGEN_TRUNK_VARIANCE 3
#define WORLDGEN_LEAF_RADIUS_SQ 6

static uint32_t Hash2(uint32_t seed, int x, int y) {
  uint32_t hash = seed ^ ((uint32_t)x * 0x8da6b343u) ^ ((uint32_t)y * 0xd8163841u);
  hash = (hash ^ (hash >> 13)) * 0x9e3779b1u;
  return hash ^ (hash >> 16);
}

static float GradientCorner(uint32_t seed, int ix, int iy, float dx, float dy) {
  static const float gradientX[8] = {1.0f, -1.0f, 0.0f,     0.0f,
                                     0.7071f, -0.7071f, 0.7071f, -0.7071f};
  static const float gradientY[8] = {0.0f, 0.0f,     1.0f, -1.0f,
                                     0.7071f, 0.7071f, -0.7071f, -0.7071f};
  uint32_t corner = Hash2(seed, ix, iy) & 7u;
  return gradientX[corner] * dx + gradientY[corner] * dy;
}

static float GradientNoise2D(uint32_t seed, float x, float y) {
  int x0 = (int)floorf(x);
  int y0 = (int)floorf(y);
  float fx = x - (float)x0;
  float fy = y - (float)y0;
  float u = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
  float v = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

  float n00 = GradientCorner(seed, x0, y0, fx, fy);
  float n10 = GradientCorner(seed, x0 + 1, y0, fx - 1.0f, fy);
  float n01 = GradientCorner(seed, x0, y0 + 1, fx, fy - 1.0f);
  float n11 = GradientCorner(seed, x0 + 1, y0 + 1, fx - 1.0f, fy - 1.0f);
  float top = n00 + u * (n10 - n00);
  float bottom = n01 + u * (n11 - n01);
  return top + v * (bottom - top);
}

static float FractalNoise2D(uint32_t seed, float x, float y) {
  float sum = 0.0f;
  float amplitude = 1.0f;
  float total = 0.0f;
  float frequency = 1.0f;
  for (int octave = 0; octave < WORLDGEN_OCTAVES; octave++) {
    sum += GradientNoise2D(seed + (uint32_t)octave * 7919u, x * frequency,
                           y * frequency) *
           amplitude;
    total += amplitude;
    amplitude *= 0.5f;
    frequency *= 2.0f;
  }
  return sum / total;
}

int WorldGenHeight(uint32_t seed, int x, int z) {
  float noise = FractalNoise2D(seed, (float)x * WORLDGEN_SCALE,
                               (float)z * WORLDGEN_SCALE);
  int height =
      WORLDGEN_BASE + (int)lroundf(noise * (float)WORLDGEN_AMPLITUDE * 1.4f);
  if (height < WORLDGEN_MIN_HEIGHT)
    height = WORLDGEN_MIN_HEIGHT;
  if (height > WORLDGEN_MAX_HEIGHT)
    height = WORLDGEN_MAX_HEIGHT;
  return height;
}

static void PlaceTree(World *world, uint32_t seed, int x, int z, int surfaceY) {
  uint32_t hash = Hash2(seed ^ 0x51ab0e23u, x, z);
  int trunkHeight =
      WORLDGEN_TRUNK_MIN + (int)((hash >> 4) % WORLDGEN_TRUNK_VARIANCE);
  int top = surfaceY + trunkHeight;

  for (int y = surfaceY + 1; y <= top; y++)
    WorldSetBlock(world, x, y, z, BLOCK_WOOD);

  for (int dx = -2; dx <= 2; dx++) {
    for (int dy = -2; dy <= 2; dy++) {
      for (int dz = -2; dz <= 2; dz++) {
        if (dx * dx + dy * dy + dz * dz > WORLDGEN_LEAF_RADIUS_SQ)
          continue;
        // Leaves only fill air so neighbouring trunks are never replaced.
        if (WorldGetBlock(world, x + dx, top + dy, z + dz) == BLOCK_AIR)
          WorldSetBlock(world, x + dx, top + dy, z + dz, BLOCK_LEAVES);
      }
    }
  }
}

static void PlaceTrees(World *world, uint32_t seed) {
  for (int cellX = 0; cellX * WORLDGEN_TREE_CELL < WORLD_WIDTH; cellX++) {
    for (int cellZ = 0; cellZ * WORLDGEN_TREE_CELL < WORLD_DEPTH; cellZ++) {
      uint32_t hash = Hash2(seed ^ 0x2c1b3c6du, cellX, cellZ);
      if ((hash & 0xffu) >= WORLDGEN_TREE_DENSITY)
        continue;

      int x = cellX * WORLDGEN_TREE_CELL + (int)((hash >> 8) % WORLDGEN_TREE_CELL);
      int z = cellZ * WORLDGEN_TREE_CELL + (int)((hash >> 16) % WORLDGEN_TREE_CELL);
      // Keep the canopy clear of the world border.
      if (x < 3 || x >= WORLD_WIDTH - 3 || z < 3 || z >= WORLD_DEPTH - 3)
        continue;

      PlaceTree(world, seed, x, z, WorldGenHeight(seed, x, z));
    }
  }
}

void WorldGenerate(World *world, uint32_t seed) {
  for (int x = 0; x < WORLD_WIDTH; x++) {
    for (int z = 0; z < WORLD_DEPTH; z++) {
      int height = WorldGenHeight(seed, x, z);
      for (int y = 0; y <= height; y++) {
        BlockId id;
        if (y == height)
          id = BLOCK_GRASS;
        else if (y >= height - WORLDGEN_DIRT_DEPTH)
          id = BLOCK_DIRT;
        else
          id = BLOCK_STONE;
        WorldSetBlock(world, x, y, z, id);
      }
    }
  }
  PlaceTrees(world, seed);
}
