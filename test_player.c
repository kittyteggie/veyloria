#include "player.h"
#include "world.h"

#include <math.h>
#include <stdio.h>

// SUMMARY: Exercises block raycasting, player collision, and placement overlap.

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

static bool Near(float a, float b) { return fabsf(a - b) < 0.0001f; }

static void TestLookDirection(void) {
  Player player = {0};
  Vector3 look = LookDirection(&player);
  CHECK(Near(look.x, 0.0f) && Near(look.y, 0.0f) && Near(look.z, -1.0f));

  player.pitch = 1.5f;
  look = LookDirection(&player);
  CHECK(Near(look.x, 0.0f) && Near(look.y, sinf(1.5f)) &&
        Near(look.z, -cosf(1.5f)));

  player.pitch = 0.0f;
  player.yaw = 3.14159265f;
  look = LookDirection(&player);
  CHECK(Near(look.y, 0.0f) && look.z > 0.99f);
}

static void TestRayHitsBlockBelow(void) {
  World world;
  WorldInit(&world);
  WorldSetBlock(&world, 10, 10, 10, BLOCK_STONE);

  RayHit hit = CastBlockRay(&world, (Vector3){10.5f, 15.0f, 10.5f},
                            (Vector3){0.0f, -1.0f, 0.0f});
  CHECK(hit.hit);
  CHECK(hit.x == 10 && hit.y == 10 && hit.z == 10);
  CHECK(hit.placeX == 10 && hit.placeY == 11 && hit.placeZ == 10);
  CHECK(Near(hit.distance, 4.0f));
  WorldFree(&world);
}

static void TestRayHitsBlockSide(void) {
  World world;
  WorldInit(&world);
  WorldSetBlock(&world, 12, 10, 10, BLOCK_GRASS);

  RayHit hit = CastBlockRay(&world, (Vector3){10.5f, 10.5f, 10.5f},
                            (Vector3){1.0f, 0.0f, 0.0f});
  CHECK(hit.hit);
  CHECK(hit.x == 12 && hit.y == 10 && hit.z == 10);
  CHECK(hit.placeX == 11 && hit.placeY == 10 && hit.placeZ == 10);
  CHECK(Near(hit.distance, 1.5f));
  WorldFree(&world);
}

static void TestRayMissesSky(void) {
  World world;
  WorldInit(&world);
  WorldSetBlock(&world, 10, 10, 10, BLOCK_STONE);

  RayHit hit = CastBlockRay(&world, (Vector3){10.5f, 15.0f, 10.5f},
                            (Vector3){0.0f, 1.0f, 0.0f});
  CHECK(!hit.hit);
  WorldFree(&world);
}

static void TestRayRespectsReach(void) {
  World world;
  WorldInit(&world);

  // Just inside reach: top face at 6.0 units from the eye.
  WorldSetBlock(&world, 10, 8, 10, BLOCK_STONE);
  RayHit hit = CastBlockRay(&world, (Vector3){10.5f, 15.0f, 10.5f},
                            (Vector3){0.0f, -1.0f, 0.0f});
  CHECK(hit.hit && Near(hit.distance, 6.0f));

  // Just outside reach: top face at 7.0 units from the eye.
  WorldSetBlock(&world, 10, 8, 10, BLOCK_AIR);
  WorldSetBlock(&world, 10, 7, 10, BLOCK_STONE);
  hit = CastBlockRay(&world, (Vector3){10.5f, 15.0f, 10.5f},
                     (Vector3){0.0f, -1.0f, 0.0f});
  CHECK(!hit.hit);
  WorldFree(&world);
}

static void TestPlayerCollides(void) {
  World world;
  WorldInit(&world);
  WorldSetBlock(&world, 5, 0, 5, BLOCK_STONE);
  WorldSetBlock(&world, 5, 3, 5, BLOCK_GRASS);

  // Body in open air between the ground block and the raised block.
  CHECK(!PlayerCollides(&world, (Vector3){5.5f, 1.0f, 5.5f}));
  // Feet sunk into the ground block.
  CHECK(PlayerCollides(&world, (Vector3){5.5f, 0.5f, 5.5f}));
  // Head inside the raised block.
  CHECK(PlayerCollides(&world, (Vector3){5.5f, 2.0f, 5.5f}));
  // A distant column is unaffected.
  CHECK(!PlayerCollides(&world, (Vector3){9.5f, 1.0f, 5.5f}));
  WorldFree(&world);
}

static void TestPlayerSupport(void) {
  World world;
  WorldInit(&world);
  WorldSetBlock(&world, 5, 0, 5, BLOCK_STONE);

  // Centred on the supporting block.
  CHECK(PlayerHasSupport(&world, (Vector3){5.5f, 1.0f, 5.5f}));
  // Hanging over the edge but still above the block.
  CHECK(PlayerHasSupport(&world, (Vector3){5.9f, 1.0f, 5.5f}));
  // Just past the edge: nothing left under the footprint.
  CHECK(!PlayerHasSupport(&world, (Vector3){6.4f, 1.0f, 5.5f}));
  // High above empty ground.
  CHECK(!PlayerHasSupport(&world, (Vector3){10.5f, 5.0f, 5.5f}));
  WorldFree(&world);
}

static void TestPlacementOverlap(void) {
  Player player = {0};
  player.feet = (Vector3){5.5f, 2.0f, 5.5f};

  CHECK(PlacementOverlapsPlayer(&player, 5, 2, 5));
  CHECK(!PlacementOverlapsPlayer(&player, 10, 2, 5));
  CHECK(!PlacementOverlapsPlayer(&player, 5, 5, 5));
  CHECK(PlacementOverlapsPlayer(&player, 5, 3, 5));
}

int main(void) {
  TestLookDirection();
  TestRayHitsBlockBelow();
  TestRayHitsBlockSide();
  TestRayMissesSky();
  TestRayRespectsReach();
  TestPlayerCollides();
  TestPlayerSupport();
  TestPlacementOverlap();

  printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
