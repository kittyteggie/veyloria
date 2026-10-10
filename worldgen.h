#ifndef VEYLORIA_WORLDGEN_H
#define VEYLORIA_WORLDGEN_H

#include <stdint.h>

#include "world.h"

// SUMMARY: Deterministic terrain generation: noise hills, soil layers, and trees.

#define WORLDGEN_MIN_HEIGHT 8
#define WORLDGEN_MAX_HEIGHT 96

// Fills an initialised empty world with terrain and trees for the given seed.
void WorldGenerate(World *world, uint32_t seed);

// Surface height of the terrain column before trees are placed.
int WorldGenHeight(uint32_t seed, int x, int z);

#endif
