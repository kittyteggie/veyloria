#include "blocks.h"

#include <stdio.h>
#include <string.h>

// SUMMARY: Exercises per-face texture resolution and hotbar stacking.

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

static bool NameIs(BlockId id, BlockFace face, const char *expected) {
  return strcmp(BlockTextureName(id, face), expected) == 0;
}

static void TestFaceMapping(void) {
  CHECK(BlockFaceOf(0) == BLOCK_FACE_SIDE);
  CHECK(BlockFaceOf(1) == BLOCK_FACE_SIDE);
  CHECK(BlockFaceOf(2) == BLOCK_FACE_TOP);
  CHECK(BlockFaceOf(3) == BLOCK_FACE_BOTTOM);
  CHECK(BlockFaceOf(4) == BLOCK_FACE_SIDE);
  CHECK(BlockFaceOf(5) == BLOCK_FACE_SIDE);
}

static void TestTextureResolution(void) {
  // Grass: green top, dirt sides, and an explicit dirt underside.
  CHECK(NameIs(BLOCK_GRASS, BLOCK_FACE_TOP, "grass_top"));
  CHECK(NameIs(BLOCK_GRASS, BLOCK_FACE_SIDE, "grass_side"));
  CHECK(NameIs(BLOCK_GRASS, BLOCK_FACE_BOTTOM, "dirt"));

  // Wood: log ends on top and bottom, bark on the sides.
  CHECK(NameIs(BLOCK_WOOD, BLOCK_FACE_TOP, "wood_top"));
  CHECK(NameIs(BLOCK_WOOD, BLOCK_FACE_SIDE, "wood_side"));
  CHECK(NameIs(BLOCK_WOOD, BLOCK_FACE_BOTTOM, "wood_top"));

  // Blocks without overrides use the base texture on every face.
  for (int face = 0; face < BLOCK_FACE_COUNT; face++) {
    CHECK(NameIs(BLOCK_STONE, (BlockFace)face, "stone"));
    CHECK(NameIs(BLOCK_DIRT, (BlockFace)face, "dirt"));
    CHECK(NameIs(BLOCK_LEAVES, (BlockFace)face, "leaves"));
  }
}

static void TestItemStackGive(void) {
  ItemStack slots[2] = {{BLOCK_GRASS, 2}, {BLOCK_AIR, 0}};

  // Matches the existing grass stack.
  ItemStackGive(slots, 2, BLOCK_GRASS);
  CHECK(slots[0].id == BLOCK_GRASS && slots[0].count == 3);

  // A new block type goes to the empty slot.
  ItemStackGive(slots, 2, BLOCK_WOOD);
  CHECK(slots[1].id == BLOCK_WOOD && slots[1].count == 1);

  // Fills the matching stack again now that both slots are taken.
  ItemStackGive(slots, 2, BLOCK_WOOD);
  CHECK(slots[1].id == BLOCK_WOOD && slots[1].count == 2);

  // Air is never stored and a full inventory drops the item.
  ItemStackGive(slots, 2, BLOCK_AIR);
  CHECK(slots[0].count == 3 && slots[1].count == 2);
  slots[0].count = INVENTORY_CAPACITY;
  slots[1].count = INVENTORY_CAPACITY;
  ItemStackGive(slots, 2, BLOCK_LEAVES);
  CHECK(slots[0].id == BLOCK_GRASS && slots[1].id == BLOCK_WOOD);
  CHECK(slots[0].count == INVENTORY_CAPACITY && slots[1].count == INVENTORY_CAPACITY);
}

int main(void) {
  TestFaceMapping();
  TestTextureResolution();
  TestItemStackGive();

  printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
