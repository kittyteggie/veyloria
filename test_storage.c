#include "save.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SUMMARY: Exercises chunk storage, block serialization, and the save file format.

static int checksRun = 0;
static int checksFailed = 0;

#define TEST_FILE "test-world.tmp"
#define TEST_TEMP_FILE "test-world.tmp.partial"

#define CHECK(condition)                                                      \
  do {                                                                        \
    checksRun++;                                                              \
    if (!(condition)) {                                                       \
      checksFailed++;                                                         \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);             \
    }                                                                         \
  } while (0)

static void FillPattern(World *world) {
  WorldSetBlock(world, 0, 0, 0, BLOCK_GRASS);
  WorldSetBlock(world, 15, 15, 15, BLOCK_STONE);
  WorldSetBlock(world, 16, 0, 16, BLOCK_GRASS);
  WorldSetBlock(world, 100, 200, 100, BLOCK_STONE);
  WorldSetBlock(world, 255, 255, 255, BLOCK_GRASS);
  WorldSetBlock(world, 33, 2, 77, BLOCK_STONE);
}

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

static bool ReadWholeFile(const char *path, uint8_t **outData, size_t *outSize) {
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return false;

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size < 0) {
    fclose(file);
    return false;
  }

  uint8_t *data = malloc((size_t)size + 1);
  if (data == NULL) {
    fclose(file);
    return false;
  }

  bool ok = fread(data, 1, (size_t)size, file) == (size_t)size;
  fclose(file);
  if (!ok) {
    free(data);
    return false;
  }

  *outData = data;
  *outSize = (size_t)size;
  return true;
}

static bool WriteWholeFile(const char *path, const uint8_t *data, size_t size) {
  FILE *file = fopen(path, "wb");
  if (file == NULL)
    return false;
  bool ok = fwrite(data, 1, size, file) == size;
  fclose(file);
  return ok;
}

static void WriteU32At(uint8_t *data, size_t offset, uint32_t value) {
  data[offset + 0] = (uint8_t)value;
  data[offset + 1] = (uint8_t)(value >> 8);
  data[offset + 2] = (uint8_t)(value >> 16);
  data[offset + 3] = (uint8_t)(value >> 24);
}

static void WriteFloatAt(uint8_t *data, size_t offset, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  WriteU32At(data, offset, bits);
}

static void TestWorldAccess(void) {
  World world;
  WorldInit(&world);
  CHECK(WorldGetBlock(&world, 5, 5, 5) == BLOCK_AIR);

  WorldSetBlock(&world, 5, 5, 5, BLOCK_GRASS);
  CHECK(WorldGetBlock(&world, 5, 5, 5) == BLOCK_GRASS);

  WorldSetBlock(&world, 5, 5, 5, BLOCK_AIR);
  CHECK(WorldGetBlock(&world, 5, 5, 5) == BLOCK_AIR);
  WorldFree(&world);
}

static void TestWorldBounds(void) {
  World world;
  WorldInit(&world);
  CHECK(WorldGetBlock(&world, -1, 0, 0) == BLOCK_AIR);
  CHECK(WorldGetBlock(&world, WORLD_WIDTH, 0, 0) == BLOCK_AIR);
  CHECK(WorldGetBlock(&world, 0, WORLD_HEIGHT, 0) == BLOCK_AIR);
  CHECK(WorldGetBlock(&world, 0, 0, WORLD_DEPTH) == BLOCK_AIR);

  WorldSetBlock(&world, -1, 0, 0, BLOCK_STONE);
  WorldSetBlock(&world, 0, WORLD_HEIGHT, 0, BLOCK_STONE);
  WorldSetBlock(&world, 0, 0, WORLD_DEPTH, BLOCK_STONE);
  CHECK(WorldCountChunks(&world) == 0);
  WorldFree(&world);
}

static void TestWorldSparseChunks(void) {
  World world;
  WorldInit(&world);
  CHECK(WorldCountChunks(&world) == 0);

  WorldSetBlock(&world, 1, 2, 3, BLOCK_AIR);
  CHECK(WorldCountChunks(&world) == 0);

  WorldSetBlock(&world, 1, 2, 3, BLOCK_GRASS);
  CHECK(WorldCountChunks(&world) == 1);

  WorldSetBlock(&world, 31, 2, 3, BLOCK_STONE);
  CHECK(WorldCountChunks(&world) == 2);

  WorldSetBlock(&world, 31, 2, 3, BLOCK_AIR);
  CHECK(WorldCountChunks(&world) == 2);
  CHECK(WorldChunkAtConst(&world, 1, 0, 0)->maxLocalY == -1);
  WorldFree(&world);
}

static void TestWorldDirtyFlags(void) {
  World world;
  WorldInit(&world);
  CHECK(!WorldChunkDirty(&world, 0, 0, 0));

  // Populate both chunks so both own meshes that must stay coherent.
  WorldSetBlock(&world, 5, 5, 5, BLOCK_GRASS);
  WorldSetBlock(&world, 20, 5, 5, BLOCK_STONE);
  WorldMarkChunkClean(&world, 0, 0, 0);
  WorldMarkChunkClean(&world, 1, 0, 0);
  CHECK(!WorldChunkDirty(&world, 0, 0, 0));
  CHECK(!WorldChunkDirty(&world, 1, 0, 0));

  // A border block must also dirty the neighbouring chunk.
  WorldSetBlock(&world, 15, 5, 5, BLOCK_STONE);
  CHECK(WorldChunkDirty(&world, 0, 0, 0));
  CHECK(WorldChunkDirty(&world, 1, 0, 0));

  WorldMarkChunkClean(&world, 0, 0, 0);
  WorldMarkChunkClean(&world, 1, 0, 0);

  // An interior block only dirties its own chunk.
  WorldSetBlock(&world, 5, 6, 5, BLOCK_GRASS);
  CHECK(WorldChunkDirty(&world, 0, 0, 0));
  CHECK(!WorldChunkDirty(&world, 1, 0, 0));
  WorldFree(&world);
}

static void TestWorldMaxLocalY(void) {
  World world;
  WorldInit(&world);
  WorldSetBlock(&world, 3, 10, 3, BLOCK_STONE);
  WorldSetBlock(&world, 3, 4, 3, BLOCK_GRASS);
  const Chunk *chunk = WorldChunkAtConst(&world, 0, 0, 0);
  CHECK(chunk != NULL && chunk->maxLocalY == 10);

  WorldSetBlock(&world, 3, 10, 3, BLOCK_AIR);
  CHECK(WorldChunkAtConst(&world, 0, 0, 0)->maxLocalY == 4);

  WorldSetBlock(&world, 3, 4, 3, BLOCK_AIR);
  CHECK(WorldChunkAtConst(&world, 0, 0, 0)->maxLocalY == -1);
  WorldFree(&world);
}

static void TestBlockSerialization(void) {
  World source;
  World target;
  WorldInit(&source);
  WorldInit(&target);
  FillPattern(&source);

  size_t size = 0;
  uint8_t *data = WorldSerializeBlocks(&source, &size);
  CHECK(data != NULL);
  CHECK(WorldCountChunks(&source) > 1);
  CHECK(size == (size_t)WorldCountChunks(&source) * CHUNK_RECORD_SIZE);

  CHECK(WorldDeserializeBlocks(&target, data, size));
  CHECK(WorldPayloadsEqual(&source, &target));
  free(data);
  WorldFree(&source);
  WorldFree(&target);
}

static void TestBlockSerializationEmpty(void) {
  World world;
  WorldInit(&world);
  size_t size = 1;
  uint8_t *data = WorldSerializeBlocks(&world, &size);
  CHECK(data != NULL);
  CHECK(size == 0);
  CHECK(WorldDeserializeBlocks(&world, data, size));
  CHECK(WorldCountChunks(&world) == 0);
  free(data);
  WorldFree(&world);
}

static void TestBlockSerializationRejectsGarbage(void) {
  World source;
  World target;
  WorldInit(&source);
  WorldInit(&target);
  FillPattern(&source);
  WorldSetBlock(&target, 9, 9, 9, BLOCK_GRASS);

  size_t size = 0;
  uint8_t *data = WorldSerializeBlocks(&source, &size);
  CHECK(data != NULL && size > 0);

  CHECK(!WorldDeserializeBlocks(&target, data, size - 1));

  uint8_t *tampered = malloc(size);
  CHECK(tampered != NULL);
  if (tampered != NULL) {
    memcpy(tampered, data, size);
    tampered[CHUNK_RECORD_HEADER] = 0xff;
    CHECK(!WorldDeserializeBlocks(&target, tampered, size));

    memcpy(tampered, data, size);
    WriteU32At(tampered, 0, 0xffffu);
    CHECK(!WorldDeserializeBlocks(&target, tampered, size));
    free(tampered);
  }

  // A rejected payload must leave the destination world untouched.
  CHECK(WorldGetBlock(&target, 9, 9, 9) == BLOCK_GRASS);
  CHECK(WorldCountChunks(&target) == 1);
  free(data);
  WorldFree(&source);
  WorldFree(&target);
}

static void TestSaveRoundTrip(void) {
  World source;
  World target;
  WorldInit(&source);
  WorldInit(&target);
  FillPattern(&source);

  Player player = {0};
  player.feet = (Vector3){10.5f, 5.25f, 20.75f};
  player.yaw = 1.25f;
  player.pitch = -0.5f;
  ItemStack slots[2] = {{BLOCK_GRASS, 40}, {BLOCK_STONE, 12}};

  CHECK(SaveGame(TEST_FILE, TEST_TEMP_FILE, &source, &player, slots, 2, 1));

  Player loadedPlayer = {0};
  ItemStack loadedSlots[2] = {{BLOCK_AIR, 0}, {BLOCK_AIR, 0}};
  int loadedSelected = -1;
  CHECK(LoadGame(TEST_FILE, &target, &loadedPlayer, loadedSlots, 2,
                 &loadedSelected));

  CHECK(loadedPlayer.feet.x == 10.5f && loadedPlayer.feet.y == 5.25f &&
        loadedPlayer.feet.z == 20.75f);
  CHECK(loadedPlayer.yaw == 1.25f && loadedPlayer.pitch == -0.5f);
  CHECK(loadedPlayer.verticalVelocity == 0.0f && !loadedPlayer.grounded);
  CHECK(loadedSlots[0].id == BLOCK_GRASS && loadedSlots[0].count == 40);
  CHECK(loadedSlots[1].id == BLOCK_STONE && loadedSlots[1].count == 12);
  CHECK(loadedSelected == 1);
  CHECK(WorldPayloadsEqual(&source, &target));

  remove(TEST_FILE);
  WorldFree(&source);
  WorldFree(&target);
}

static void TestSaveRejectsBadFiles(void) {
  World source;
  World target;
  WorldInit(&source);
  WorldInit(&target);
  FillPattern(&source);
  WorldSetBlock(&target, 9, 9, 9, BLOCK_GRASS);

  Player player = {0};
  player.feet = (Vector3){32.0f, 4.0f, 32.0f};
  ItemStack slots[2] = {{BLOCK_GRASS, 1}, {BLOCK_STONE, 2}};
  CHECK(SaveGame(TEST_FILE, TEST_TEMP_FILE, &source, &player, slots, 2, 0));

  uint8_t *data = NULL;
  size_t size = 0;
  CHECK(ReadWholeFile(TEST_FILE, &data, &size));
  if (data == NULL) {
    WorldFree(&source);
    WorldFree(&target);
    return;
  }

  Player loadedPlayer = {0};
  ItemStack loadedSlots[2] = {{BLOCK_AIR, 0}, {BLOCK_AIR, 0}};
  int loadedSelected = -1;

  // Bad magic.
  uint8_t *tampered = malloc(size);
  CHECK(tampered != NULL);
  if (tampered != NULL) {
    memcpy(tampered, data, size);
    tampered[0] = 'X';
    CHECK(WriteWholeFile(TEST_FILE, tampered, size));
    CHECK(!LoadGame(TEST_FILE, &target, &loadedPlayer, loadedSlots, 2,
                    &loadedSelected));

    // Unknown version.
    memcpy(tampered, data, size);
    WriteU32At(tampered, 8, 99u);
    CHECK(WriteWholeFile(TEST_FILE, tampered, size));
    CHECK(!LoadGame(TEST_FILE, &target, &loadedPlayer, loadedSlots, 2,
                    &loadedSelected));

    // Out-of-bounds player position.
    memcpy(tampered, data, size);
    WriteFloatAt(tampered, 24, -5.0f);
    CHECK(WriteWholeFile(TEST_FILE, tampered, size));
    CHECK(!LoadGame(TEST_FILE, &target, &loadedPlayer, loadedSlots, 2,
                    &loadedSelected));

    // Impossible pitch.
    memcpy(tampered, data, size);
    WriteFloatAt(tampered, 40, 9.0f);
    CHECK(WriteWholeFile(TEST_FILE, tampered, size));
    CHECK(!LoadGame(TEST_FILE, &target, &loadedPlayer, loadedSlots, 2,
                    &loadedSelected));
    free(tampered);
  }

  // Truncated file.
  CHECK(WriteWholeFile(TEST_FILE, data, size / 2));
  CHECK(!LoadGame(TEST_FILE, &target, &loadedPlayer, loadedSlots, 2,
                  &loadedSelected));

  // Missing file.
  remove(TEST_FILE);
  CHECK(!LoadGame(TEST_FILE, &target, &loadedPlayer, loadedSlots, 2,
                  &loadedSelected));

  // Failed loads must leave the destination world untouched.
  CHECK(WorldGetBlock(&target, 9, 9, 9) == BLOCK_GRASS);
  CHECK(WorldCountChunks(&target) == 1);

  free(data);
  WorldFree(&source);
  WorldFree(&target);
}

int main(void) {
  TestWorldAccess();
  TestWorldBounds();
  TestWorldSparseChunks();
  TestWorldDirtyFlags();
  TestWorldMaxLocalY();
  TestBlockSerialization();
  TestBlockSerializationEmpty();
  TestBlockSerializationRejectsGarbage();
  TestSaveRoundTrip();
  TestSaveRejectsBadFiles();

  remove(TEST_FILE);
  remove(TEST_TEMP_FILE);

  printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
