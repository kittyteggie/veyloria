#include "blocks.h"
#include "world.h"
#include "worldgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SUMMARY: Exercises terrain generation determinism, layering, and trees.

static int checksRun = 0;
static int checksFailed = 0;

#define CHECK(condition)                                                      \
  do {                                                                        \
    checksRun++;                                                              \
    if (!(condition)) {                                                       \
      checksFailed++;                                                         \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);             \
    }                                                                         \
  } while (0)

static bool WorldPayloadsEqual(const World *a, const World *b) {
  size_t sizeA = 0;
  size_t sizeB = 0;
  uint8_t *dataA = WorldSerializeBlocks(a, &sizeA);
  uint8_t *dataB = WorldSerializeBlocks(b, &sizeB);
  bool equal = dataA != NULL && dataB != NULL && sizeA == sizeB &&
               (sizeA == 0 || memcmp(dataA, dataB, sizeA) == 0);
  free(dataA);
  free(dataB);
  return equal;
}

static void TestHeightRange(void) {
  for (int x = 0; x < WORLD_WIDTH; x += 7) {
    for (int z = 0; z < WORLD_DEPTH; z += 7) {
      int height = WorldGenHeight(1234u, x, z);
      CHECK(height >= WORLDGEN_MIN_HEIGHT && height <= WORLDGEN_MAX_HEIGHT);
    }
  }
}

static void TestLayering(void) {
  World world;
  WorldInit(&world);
  WorldGenerate(&world, 1234u);

  for (int x = 0; x < WORLD_WIDTH; x += 11) {
    for (int z = 0; z < WORLD_DEPTH; z += 11) {
      int height = WorldGenHeight(1234u, x, z);
      CHECK(WorldGetBlock(&world, x, height, z) == BLOCK_GRASS);
      CHECK(WorldGetBlock(&world, x, height - 1, z) == BLOCK_DIRT);
      if (height > WORLDGEN_MIN_HEIGHT + 5) {
        CHECK(WorldGetBlock(&world, x, height - 5, z) == BLOCK_STONE);
        CHECK(WorldGetBlock(&world, x, height, z) == BLOCK_GRASS);
      }
    }
  }
  WorldFree(&world);
}

static void TestTrees(void) {
  World world;
  WorldInit(&world);
  WorldGenerate(&world, 1234u);

  int woodCount = 0;
  int leavesCount = 0;
  for (int x = 0; x < WORLD_WIDTH; x++) {
    for (int z = 0; z < WORLD_DEPTH; z++) {
      for (int y = WORLDGEN_MIN_HEIGHT; y <= WORLDGEN_MAX_HEIGHT + 12; y++) {
        BlockId id = WorldGetBlock(&world, x, y, z);
        if (id == BLOCK_WOOD)
          woodCount++;
        else if (id == BLOCK_LEAVES)
          leavesCount++;
      }
    }
  }
  CHECK(woodCount > 100);
  CHECK(leavesCount > 500);
  WorldFree(&world);
}

static void TestDeterminism(void) {
  World first;
  World second;
  WorldInit(&first);
  WorldInit(&second);
  WorldGenerate(&first, 42u);
  WorldGenerate(&second, 42u);
  CHECK(WorldPayloadsEqual(&first, &second));
  WorldFree(&second);

  WorldGenerate(&second, 43u);
  CHECK(!WorldPayloadsEqual(&first, &second));
  WorldFree(&second);
  WorldFree(&first);
}

int main(void) {
  TestHeightRange();
  TestLayering();
  TestTrees();
  TestDeterminism();

  printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
