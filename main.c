#include "blocks.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define WORLD_WIDTH 256
#define WORLD_DEPTH 256
#define WORLD_HEIGHT 256
#define TERRAIN_LAYERS 3
#define PLAYER_RADIUS 0.30f
#define PLAYER_HEIGHT 1.80f
#define PLAYER_EYE_HEIGHT 1.62f
#define REACH_DISTANCE 6.0f
#define INVENTORY_CAPACITY 64
#define PI 3.14159265358979323846f
#define DEFAULT_RESOLUTION_INDEX 1
#define DEFAULT_SENSITIVITY_INDEX 2
#define RESOLUTION_COUNT 3
#define SENSITIVITY_COUNT 8
#define SETTINGS_FILE "veyloria.cfg"
#define SETTINGS_TEMP_FILE "veyloria.cfg.tmp"
#define MENU_BACKGROUND_PATH "assets/menubg.png"
#define MENU_LOGO_PATH "assets/logo.png"
#define MENU_LOGO_WIDTH 384
#define MENU_LOGO_HEIGHT 192
#define SAVE_FILE "veyloria-world.sav"
#define SAVE_TEMP_FILE "veyloria-world.sav.tmp"
#define SAVE_VERSION 2u
#define SAVE_LEGACY_VERSION 1u
#define SAVE_BUFFER_SIZE 65536

// SUMMARY: Runs the voxel game, main/config menus, and world/settings persistence.

typedef struct World {
  BlockId blocks[WORLD_WIDTH][WORLD_HEIGHT][WORLD_DEPTH];
  int highestBlockY[WORLD_WIDTH][WORLD_DEPTH];
} World;

typedef struct Player {
  Vector3 feet;
  float yaw;
  float pitch;
  float verticalVelocity;
  bool grounded;
} Player;

typedef struct RayHit {
  bool hit;
  int x;
  int y;
  int z;
  int placeX;
  int placeY;
  int placeZ;
  float distance;
} RayHit;

typedef struct MeshBuilder {
  float *vertices;
  float *normals;
  float *texcoords;
  unsigned char *colors;
  int vertexCount;
  int capacity;
} MeshBuilder;

typedef struct Resolution {
  int width;
  int height;
} Resolution;

typedef struct GameSettings {
  int resolutionIndex;
  int sensitivityIndex;
} GameSettings;

typedef enum ScreenState {
  SCREEN_MAIN_MENU,
  SCREEN_CONFIG,
  SCREEN_PLAYING
} ScreenState;

static World world;
static Model worldModels[BLOCK_COUNT];
static bool worldModelsLoaded;
static Texture2D blockTextures[BLOCK_COUNT];
static bool blockTextureLoaded[BLOCK_COUNT];
static const Resolution resolutions[RESOLUTION_COUNT] = {
    {640, 480}, {WINDOW_WIDTH, WINDOW_HEIGHT}, {1024, 768}};
static const float sensitivities[SENSITIVITY_COUNT] = {
    0.0005f, 0.0010f, 0.0015f, 0.0020f,
    0.0025f, 0.0030f, 0.0035f, 0.0040f};

static bool InWorld(int x, int y, int z) {
  return x >= 0 && x < WORLD_WIDTH && y >= 0 && y < WORLD_HEIGHT && z >= 0 &&
         z < WORLD_DEPTH;
}

static BlockId GetBlock(int x, int y, int z) {
  if (!InWorld(x, y, z))
    return BLOCK_AIR;
  return world.blocks[x][y][z];
}

static void SetBlock(int x, int y, int z, BlockId id) {
  if (!InWorld(x, y, z))
    return;

  world.blocks[x][y][z] = id;
  if (id != BLOCK_AIR && y > world.highestBlockY[x][z]) {
    world.highestBlockY[x][z] = y;
  } else if (id == BLOCK_AIR && y == world.highestBlockY[x][z]) {
    while (world.highestBlockY[x][z] >= 0 &&
           world.blocks[x][world.highestBlockY[x][z]][z] == BLOCK_AIR) {
      world.highestBlockY[x][z]--;
    }
  }
}

static void InitializeWorld(void) {
  memset(world.blocks, 0, sizeof(world.blocks));
  for (int x = 0; x < WORLD_WIDTH; x++) {
    for (int y = 0; y < TERRAIN_LAYERS; y++) {
      for (int z = 0; z < WORLD_DEPTH; z++) {
        // The surface is grass; the two layers beneath it are stone.
        world.blocks[x][y][z] =
            (y == TERRAIN_LAYERS - 1) ? BLOCK_GRASS : BLOCK_STONE;
      }
    }
    for (int z = 0; z < WORLD_DEPTH; z++) {
      world.highestBlockY[x][z] = TERRAIN_LAYERS - 1;
    }
  }
}

static void EnsureMeshCapacity(MeshBuilder *builder, int required) {
  if (required <= builder->capacity)
    return;

  int newCapacity = builder->capacity ? builder->capacity : 1024;
  while (newCapacity < required)
    newCapacity *= 2;

  builder->vertices =
      realloc(builder->vertices, (size_t)newCapacity * 3 * sizeof(float));
  builder->normals =
      realloc(builder->normals, (size_t)newCapacity * 3 * sizeof(float));
  builder->texcoords =
      realloc(builder->texcoords, (size_t)newCapacity * 2 * sizeof(float));
  builder->colors =
      realloc(builder->colors, (size_t)newCapacity * 4 * sizeof(unsigned char));
  if (!builder->vertices || !builder->normals || !builder->texcoords ||
      !builder->colors) {
    TraceLog(LOG_FATAL, "Failed to allocate world mesh");
    exit(EXIT_FAILURE);
  }
  builder->capacity = newCapacity;
}

static Color BlockFaceColor(BlockId id, int face, bool textured) {
  static const float shade[] = {0.78f, 0.54f, 1.0f, 0.58f, 0.88f, 0.70f};
  float factor = shade[face];
  if (textured) {
    unsigned char brightness = (unsigned char)(255.0f * factor);
    return (Color){brightness, brightness, brightness, 255};
  }

  Color base;
  if (id == BLOCK_GRASS) {
    base =
        (face == 2) ? (Color){101, 176, 72, 255} : (Color){105, 139, 70, 255};
  } else {
    base = (Color){132, 136, 143, 255};
  }

  return (Color){(unsigned char)(base.r * factor),
                 (unsigned char)(base.g * factor),
                 (unsigned char)(base.b * factor), 255};
}

static void AddFace(MeshBuilder *builder, int x, int y, int z, BlockId id,
                    int face, bool textured) {
  // Each face is four corners in outward-facing winding order.
  static const Vector3 corners[6][4] = {
      {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, // +X
      {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}, // -X
      {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, // +Y
      {{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}, // -Y
      {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, // +Z
      {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}  // -Z
  };
  static const Vector3 normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  static const int order[6] = {0, 1, 2, 0, 2, 3};
  Color color = BlockFaceColor(id, face, textured);

  EnsureMeshCapacity(builder, builder->vertexCount + 6);
  for (int i = 0; i < 6; i++) {
    Vector3 point = corners[face][order[i]];
    int vertex = builder->vertexCount++;
    builder->vertices[vertex * 3 + 0] = (float)x + point.x;
    builder->vertices[vertex * 3 + 1] = (float)y + point.y;
    builder->vertices[vertex * 3 + 2] = (float)z + point.z;
    builder->normals[vertex * 3 + 0] = normals[face].x;
    builder->normals[vertex * 3 + 1] = normals[face].y;
    builder->normals[vertex * 3 + 2] = normals[face].z;
    builder->texcoords[vertex * 2 + 0] =
        (i == 1 || i == 2 || i == 4) ? 1.0f : 0.0f;
    builder->texcoords[vertex * 2 + 1] =
        (i == 2 || i == 4 || i == 5) ? 1.0f : 0.0f;
    builder->colors[vertex * 4 + 0] = color.r;
    builder->colors[vertex * 4 + 1] = color.g;
    builder->colors[vertex * 4 + 2] = color.b;
    builder->colors[vertex * 4 + 3] = color.a;
  }
}

static void UnloadWorldModels(void) {
  if (!worldModelsLoaded)
    return;
  for (int id = 1; id < BLOCK_COUNT; id++) {
    if (worldModels[id].meshCount > 0)
      UnloadModel(worldModels[id]);
  }
  memset(worldModels, 0, sizeof(worldModels));
  worldModelsLoaded = false;
}

static void BuildWorldModels(void) {
  static const int neighbor[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  MeshBuilder builders[BLOCK_COUNT] = {0};

  for (int x = 0; x < WORLD_WIDTH; x++) {
    for (int z = 0; z < WORLD_DEPTH; z++) {
      for (int y = 0; y <= world.highestBlockY[x][z]; y++) {
        BlockId id = GetBlock(x, y, z);
        if (id == BLOCK_AIR)
          continue;

        for (int face = 0; face < 6; face++) {
          if (!blockDefs[GetBlock(x + neighbor[face][0], y + neighbor[face][1],
                                  z + neighbor[face][2])]
                   .solid) {
            AddFace(&builders[id], x, y, z, id, face,
                    blockTextureLoaded[id]);
          }
        }
      }
    }
  }

  Model newModels[BLOCK_COUNT] = {0};
  for (int id = 1; id < BLOCK_COUNT; id++) {
    MeshBuilder *builder = &builders[id];
    if (builder->vertexCount == 0)
      continue;

    Mesh mesh = {0};
    mesh.vertexCount = builder->vertexCount;
    mesh.triangleCount = builder->vertexCount / 3;
    mesh.vertices = builder->vertices;
    mesh.normals = builder->normals;
    mesh.texcoords = builder->texcoords;
    mesh.colors = builder->colors;
    UploadMesh(&mesh, false);
    newModels[id] = LoadModelFromMesh(mesh);
    if (blockTextureLoaded[id] && newModels[id].materialCount > 0) {
      SetMaterialTexture(&newModels[id].materials[0], MATERIAL_MAP_DIFFUSE,
                         blockTextures[id]);
    }
  }

  UnloadWorldModels();
  memcpy(worldModels, newModels, sizeof(worldModels));
  worldModelsLoaded = true;
}

static bool WriteU32(FILE *file, uint32_t value) {
  uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                      (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
  return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

static bool ReadU32(FILE *file, uint32_t *value) {
  uint8_t bytes[4];
  if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes))
    return false;

  *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
  return true;
}

static bool WriteFloat(FILE *file, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return WriteU32(file, bits);
}

static bool ReadFloat(FILE *file, float *value) {
  uint32_t bits;
  if (!ReadU32(file, &bits))
    return false;
  memcpy(value, &bits, sizeof(bits));
  return true;
}

static bool SaveGame(const Player *player, const int inventory[2],
                     int selectedSlot) {
  static const uint8_t magic[8] = {'V', 'E', 'Y', 'L', 'O', 'R', 'I', 'A'};
  size_t blockCount = (size_t)WORLD_WIDTH * WORLD_HEIGHT * WORLD_DEPTH;
  uint8_t *blockData = malloc(blockCount);
  if (!blockData)
    return false;

  bool success = true;
  size_t index = 0;
  for (int x = 0; success && x < WORLD_WIDTH; x++) {
    for (int y = 0; success && y < WORLD_HEIGHT; y++) {
      for (int z = 0; success && z < WORLD_DEPTH; z++) {
        BlockId id = world.blocks[x][y][z];
        if (id >= BLOCK_COUNT) {
          success = false;
          break;
        }
        blockData[index++] = (uint8_t)id;
      }
    }
  }

  int compressedSize = 0;
  unsigned char *compressedData = NULL;
  if (success)
    compressedData = CompressData(blockData, (int)blockCount, &compressedSize);
  free(blockData);
  if (!compressedData || compressedSize <= 0) {
    if (compressedData)
      MemFree(compressedData);
    return false;
  }

  FILE *file = fopen(SAVE_TEMP_FILE, "wb");
  if (!file) {
    MemFree(compressedData);
    return false;
  }

  success = fwrite(magic, 1, sizeof(magic), file) == sizeof(magic) &&
            WriteU32(file, SAVE_VERSION) && WriteU32(file, WORLD_WIDTH) &&
            WriteU32(file, WORLD_DEPTH) && WriteU32(file, WORLD_HEIGHT) &&
            WriteFloat(file, player->feet.x) &&
            WriteFloat(file, player->feet.y) &&
            WriteFloat(file, player->feet.z) && WriteFloat(file, player->yaw) &&
            WriteFloat(file, player->pitch) &&
            WriteU32(file, (uint32_t)inventory[0]) &&
            WriteU32(file, (uint32_t)inventory[1]) &&
            WriteU32(file, (uint32_t)selectedSlot) &&
            WriteU32(file, (uint32_t)compressedSize) &&
            fwrite(compressedData, 1, (size_t)compressedSize, file) ==
                (size_t)compressedSize;
  MemFree(compressedData);
  if (fclose(file) != 0)
    success = false;

  if (!success || rename(SAVE_TEMP_FILE, SAVE_FILE) != 0) {
    remove(SAVE_TEMP_FILE);
    return false;
  }
  return true;
}

static bool LoadGame(Player *player, int inventory[2], int *selectedSlot) {
  static const uint8_t expectedMagic[8] = {'V', 'E', 'Y', 'L', 'O', 'R', 'I', 'A'};
  FILE *file = fopen(SAVE_FILE, "rb");
  if (!file)
    return false;

  uint8_t magic[8];
  uint32_t version, width, depth, height;
  uint32_t grassCount, stoneCount, savedSelectedSlot;
  Player savedPlayer = {0};
  bool valid = fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
               memcmp(magic, expectedMagic, sizeof(magic)) == 0 &&
               ReadU32(file, &version) && ReadU32(file, &width) &&
               ReadU32(file, &depth) && ReadU32(file, &height) &&
               ReadFloat(file, &savedPlayer.feet.x) &&
               ReadFloat(file, &savedPlayer.feet.y) &&
               ReadFloat(file, &savedPlayer.feet.z) &&
               ReadFloat(file, &savedPlayer.yaw) &&
               ReadFloat(file, &savedPlayer.pitch) &&
               ReadU32(file, &grassCount) && ReadU32(file, &stoneCount) &&
               ReadU32(file, &savedSelectedSlot);

  valid = valid &&
          (version == SAVE_LEGACY_VERSION || version == SAVE_VERSION) &&
          width == WORLD_WIDTH &&
          depth == WORLD_DEPTH && height == WORLD_HEIGHT &&
          isfinite(savedPlayer.feet.x) && isfinite(savedPlayer.feet.y) &&
          isfinite(savedPlayer.feet.z) && isfinite(savedPlayer.yaw) &&
          isfinite(savedPlayer.pitch) &&
          savedPlayer.feet.x >= PLAYER_RADIUS &&
          savedPlayer.feet.x <= WORLD_WIDTH - PLAYER_RADIUS &&
          savedPlayer.feet.y >= 0.0f &&
          savedPlayer.feet.y <= WORLD_HEIGHT - PLAYER_HEIGHT &&
          savedPlayer.feet.z >= PLAYER_RADIUS &&
          savedPlayer.feet.z <= WORLD_DEPTH - PLAYER_RADIUS &&
          savedPlayer.pitch >= -1.5f && savedPlayer.pitch <= 1.5f &&
          grassCount <= INVENTORY_CAPACITY && stoneCount <= INVENTORY_CAPACITY &&
          savedSelectedSlot < 2;

  size_t blockCount = (size_t)WORLD_WIDTH * WORLD_HEIGHT * WORLD_DEPTH;
  uint8_t *blockData = NULL;
  if (valid) {
    blockData = malloc(blockCount);
    valid = blockData != NULL;
  }

  if (valid && version == SAVE_LEGACY_VERSION) {
    size_t loadedCount = 0;
    while (valid && loadedCount < blockCount) {
      size_t remaining = blockCount - loadedCount;
      size_t amount = remaining < SAVE_BUFFER_SIZE ? remaining : SAVE_BUFFER_SIZE;
      if (fread(blockData + loadedCount, 1, amount, file) != amount) {
        valid = false;
        break;
      }
      loadedCount += amount;
    }
  } else if (valid) {
    uint32_t compressedSize = 0;
    valid = ReadU32(file, &compressedSize) && compressedSize > 0 &&
            (uint64_t)compressedSize <= (uint64_t)blockCount * 2;
    unsigned char *compressedData = NULL;
    if (valid) {
      compressedData = malloc(compressedSize);
      valid = compressedData != NULL;
    }
    if (valid)
      valid = fread(compressedData, 1, compressedSize, file) == compressedSize;
    if (valid) {
      int decompressedSize = 0;
      unsigned char *decompressedData =
          DecompressData(compressedData, (int)compressedSize, &decompressedSize);
      valid = decompressedData && decompressedSize == (int)blockCount;
      if (valid)
        memcpy(blockData, decompressedData, blockCount);
      if (decompressedData)
        MemFree(decompressedData);
    }
    free(compressedData);
  }

  if (valid && (fgetc(file) != EOF || ferror(file)))
    valid = false;
  if (fclose(file) != 0)
    valid = false;

  if (valid) {
    for (size_t i = 0; i < blockCount; i++) {
      if (blockData[i] >= BLOCK_COUNT) {
        valid = false;
        break;
      }
    }
  }

  if (!valid) {
    free(blockData);
    return false;
  }

  size_t index = 0;
  for (int x = 0; x < WORLD_WIDTH; x++) {
    for (int y = 0; y < WORLD_HEIGHT; y++) {
      for (int z = 0; z < WORLD_DEPTH; z++)
        world.blocks[x][y][z] = (BlockId)blockData[index++];
    }
  }
  free(blockData);
  for (int x = 0; x < WORLD_WIDTH; x++) {
    for (int z = 0; z < WORLD_DEPTH; z++) {
      int highestY = WORLD_HEIGHT - 1;
      while (highestY >= 0 && world.blocks[x][highestY][z] == BLOCK_AIR)
        highestY--;
      world.highestBlockY[x][z] = highestY;
    }
  }

  savedPlayer.verticalVelocity = 0.0f;
  savedPlayer.grounded = false;
  *player = savedPlayer;
  inventory[0] = (int)grassCount;
  inventory[1] = (int)stoneCount;
  *selectedSlot = (int)savedSelectedSlot;
  return true;
}

static void LoadSettings(GameSettings *settings) {
  FILE *file = fopen(SETTINGS_FILE, "r");
  if (!file)
    return;

  int resolutionIndex, sensitivityIndex;
  if (fscanf(file, "%d %d", &resolutionIndex, &sensitivityIndex) == 2 &&
      resolutionIndex >= 0 && resolutionIndex < RESOLUTION_COUNT &&
      sensitivityIndex >= 0 && sensitivityIndex < SENSITIVITY_COUNT) {
    settings->resolutionIndex = resolutionIndex;
    settings->sensitivityIndex = sensitivityIndex;
  }
  fclose(file);
}

static bool SaveSettings(const GameSettings *settings) {
  FILE *file = fopen(SETTINGS_TEMP_FILE, "w");
  if (!file)
    return false;

  bool success = fprintf(file, "%d %d\n", settings->resolutionIndex,
                         settings->sensitivityIndex) > 0;
  if (fclose(file) != 0)
    success = false;
  if (!success || rename(SETTINGS_TEMP_FILE, SETTINGS_FILE) != 0) {
    remove(SETTINGS_TEMP_FILE);
    return false;
  }
  return true;
}

static Vector3 PlayerEye(const Player *player) {
  return (Vector3){player->feet.x, player->feet.y + PLAYER_EYE_HEIGHT,
                   player->feet.z};
}

static Vector3 LookDirection(const Player *player) {
  float horizontal = cosf(player->pitch);
  return (Vector3){sinf(player->yaw) * horizontal, sinf(player->pitch),
                   -cosf(player->yaw) * horizontal};
}

static bool PlayerCollides(Vector3 feet) {
  int minX = (int)floorf(feet.x - PLAYER_RADIUS);
  int maxX = (int)floorf(feet.x + PLAYER_RADIUS);
  int minY = (int)floorf(feet.y + 0.001f);
  int maxY = (int)floorf(feet.y + PLAYER_HEIGHT - 0.001f);
  int minZ = (int)floorf(feet.z - PLAYER_RADIUS);
  int maxZ = (int)floorf(feet.z + PLAYER_RADIUS);

  for (int x = minX; x <= maxX; x++) {
    for (int y = minY; y <= maxY; y++) {
      for (int z = minZ; z <= maxZ; z++) {
        if (blockDefs[GetBlock(x, y, z)].solid)
          return true;
      }
    }
  }
  return false;
}

static void MovePlayerAxis(Player *player, float amount, int axis) {
  if (amount == 0.0f)
    return;

  Vector3 next = player->feet;
  if (axis == 0)
    next.x += amount;
  else if (axis == 1)
    next.y += amount;
  else
    next.z += amount;

  if (axis == 0) {
    if (next.x < PLAYER_RADIUS)
      next.x = PLAYER_RADIUS;
    if (next.x > WORLD_WIDTH - PLAYER_RADIUS)
      next.x = WORLD_WIDTH - PLAYER_RADIUS;
  } else if (axis == 2) {
    if (next.z < PLAYER_RADIUS)
      next.z = PLAYER_RADIUS;
    if (next.z > WORLD_DEPTH - PLAYER_RADIUS)
      next.z = WORLD_DEPTH - PLAYER_RADIUS;
  } else {
    if (next.y < 0.0f) {
      next.y = 0.0f;
      player->grounded = amount < 0.0f;
      player->verticalVelocity = 0.0f;
    }
    if (next.y > WORLD_HEIGHT - PLAYER_HEIGHT) {
      next.y = WORLD_HEIGHT - PLAYER_HEIGHT;
      player->verticalVelocity = 0.0f;
    }
  }

  if (!PlayerCollides(next)) {
    player->feet = next;
  } else if (axis == 1 && amount < 0.0f) {
    player->grounded = true;
    player->verticalVelocity = 0.0f;
  } else if (axis == 1 && amount > 0.0f) {
    player->verticalVelocity = 0.0f;
  }
}

static void UpdatePlayer(Player *player, float deltaTime, bool applyMouseLook,
                         float mouseSensitivity) {
  Vector2 mouse = GetMouseDelta();
  if (applyMouseLook) {
    player->yaw += mouse.x * mouseSensitivity;
    player->pitch -= mouse.y * mouseSensitivity;
  }
  if (player->pitch > 1.50f)
    player->pitch = 1.50f;
  if (player->pitch < -1.50f)
    player->pitch = -1.50f;

  float forwardInput = (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S);
  float sideInput = (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A);
  float inputLength =
      sqrtf(forwardInput * forwardInput + sideInput * sideInput);
  if (inputLength > 1.0f) {
    forwardInput /= inputLength;
    sideInput /= inputLength;
  }

  float speed = IsKeyDown(KEY_LEFT_SHIFT) ? 7.0f : 4.5f;
  Vector3 forward = {sinf(player->yaw), 0.0f, -cosf(player->yaw)};
  Vector3 right = {cosf(player->yaw), 0.0f, sinf(player->yaw)};
  float moveX =
      (forward.x * forwardInput + right.x * sideInput) * speed * deltaTime;
  float moveZ =
      (forward.z * forwardInput + right.z * sideInput) * speed * deltaTime;
  MovePlayerAxis(player, moveX, 0);
  MovePlayerAxis(player, moveZ, 2);

  if (IsKeyPressed(KEY_SPACE) && player->grounded) {
    player->verticalVelocity = 7.0f;
    player->grounded = false;
  }

  player->grounded = false;
  player->verticalVelocity -= 20.0f * deltaTime;
  if (player->verticalVelocity < -30.0f)
    player->verticalVelocity = -30.0f;
  MovePlayerAxis(player, player->verticalVelocity * deltaTime, 1);
}

static int StepAxis(float direction) {
  if (direction > 0.0f)
    return 1;
  if (direction < 0.0f)
    return -1;
  return 0;
}

static float InitialRayBoundary(float origin, int cell, int step,
                                float direction) {
  if (step > 0)
    return ((float)cell + 1.0f - origin) / direction;
  if (step < 0)
    return (origin - (float)cell) / -direction;
  return INFINITY;
}

static RayHit CastBlockRay(Vector3 origin, Vector3 direction) {
  RayHit result = {0};
  int x = (int)floorf(origin.x);
  int y = (int)floorf(origin.y);
  int z = (int)floorf(origin.z);
  int stepX = StepAxis(direction.x);
  int stepY = StepAxis(direction.y);
  int stepZ = StepAxis(direction.z);
  float deltaX = (stepX == 0) ? INFINITY : fabsf(1.0f / direction.x);
  float deltaY = (stepY == 0) ? INFINITY : fabsf(1.0f / direction.y);
  float deltaZ = (stepZ == 0) ? INFINITY : fabsf(1.0f / direction.z);
  float maxX = InitialRayBoundary(origin.x, x, stepX, direction.x);
  float maxY = InitialRayBoundary(origin.y, y, stepY, direction.y);
  float maxZ = InitialRayBoundary(origin.z, z, stepZ, direction.z);
  int previousX = x;
  int previousY = y;
  int previousZ = z;
  float distance = 0.0f;

  while (distance <= REACH_DISTANCE) {
    if (blockDefs[GetBlock(x, y, z)].solid) {
      result.hit = true;
      result.x = x;
      result.y = y;
      result.z = z;
      result.placeX = previousX;
      result.placeY = previousY;
      result.placeZ = previousZ;
      result.distance = distance;
      return result;
    }

    previousX = x;
    previousY = y;
    previousZ = z;
    if (maxX <= maxY && maxX <= maxZ) {
      x += stepX;
      distance = maxX;
      maxX += deltaX;
    } else if (maxY <= maxZ) {
      y += stepY;
      distance = maxY;
      maxY += deltaY;
    } else {
      z += stepZ;
      distance = maxZ;
      maxZ += deltaZ;
    }
  }
  return result;
}

static bool PlacementOverlapsPlayer(const Player *player, int x, int y, int z) {
  float playerMinX = player->feet.x - PLAYER_RADIUS;
  float playerMaxX = player->feet.x + PLAYER_RADIUS;
  float playerMinY = player->feet.y;
  float playerMaxY = player->feet.y + PLAYER_HEIGHT;
  float playerMinZ = player->feet.z - PLAYER_RADIUS;
  float playerMaxZ = player->feet.z + PLAYER_RADIUS;

  return playerMinX < x + 1.0f && playerMaxX > x && playerMinY < y + 1.0f &&
         playerMaxY > y && playerMinZ < z + 1.0f && playerMaxZ > z;
}

static void DrawBlockIcon(Rectangle slot, BlockId id) {
  Color color = (id == BLOCK_GRASS) ? (Color){101, 176, 72, 255}
                                    : (Color){132, 136, 143, 255};
  int size = 24;
  int left = (int)(slot.x + (slot.width - size) * 0.5f);
  int top = (int)(slot.y + (slot.height - size) * 0.5f) - 4;
  DrawRectangle(left, top, size, size, color);
  DrawRectangle(left, top + size - 4, size, 4,
                (id == BLOCK_GRASS) ? (Color){105, 139, 70, 255}
                                    : (Color){94, 98, 104, 255});
}

static void DrawHotbar(int selectedSlot, const int inventory[2]) {
  const int slotSize = 58;
  const int gap = 6;
  int totalWidth = slotSize * 2 + gap;
  int startX = (GetScreenWidth() - totalWidth) / 2;
  int y = GetScreenHeight() - slotSize - 22;
  const BlockId slots[2] = {BLOCK_GRASS, BLOCK_STONE};

  for (int i = 0; i < 2; i++) {
    Rectangle slot = {(float)(startX + i * (slotSize + gap)), (float)y,
                      (float)slotSize, (float)slotSize};
    DrawRectangleRec(slot, (Color){21, 22, 24, 220});
    DrawRectangleLinesEx(slot, (i == selectedSlot) ? 3.0f : 1.0f,
                         (i == selectedSlot) ? RAYWHITE
                                             : (Color){120, 120, 120, 220});
    DrawBlockIcon(slot, slots[i]);

    DrawRectangle((int)slot.x + 7, y + slotSize - 8, slotSize - 14, 3,
                  (Color){45, 45, 45, 255});
    int barWidth = (slotSize - 14) * inventory[i] / INVENTORY_CAPACITY;
    if (barWidth > 0)
      DrawRectangle((int)slot.x + 7, y + slotSize - 8, barWidth, 3, RAYWHITE);
  }
}

static void DrawDebug(const Player *player, BlockId heldBlock,
                      const int inventory[2], RayHit hit) {
  DrawRectangle(12, 12, 330, 154, (Color){0, 0, 0, 170});
  Vector3 eye = PlayerEye(player);
  DrawText("VEYLORIA DEBUG", 22, 20, 18, RAYWHITE);

  char line[128];
  snprintf(line, sizeof(line), "Block in hand: %s", blockDefs[heldBlock].name);
  DrawText(line, 22, 47, 16, RAYWHITE);
  snprintf(line, sizeof(line), "Position: %.2f, %.2f, %.2f", player->feet.x,
           player->feet.y, player->feet.z);
  DrawText(line, 22, 68, 16, RAYWHITE);
  snprintf(line, sizeof(line), "Eye: %.2f, %.2f, %.2f", eye.x, eye.y, eye.z);
  DrawText(line, 22, 89, 16, RAYWHITE);
  snprintf(line, sizeof(line), "Inventory: grass %d | stone %d", inventory[0],
           inventory[1]);
  DrawText(line, 22, 110, 16, RAYWHITE);
  if (hit.hit) {
    snprintf(line, sizeof(line), "Target: %s at %d, %d, %d (%.1fm)",
             blockDefs[GetBlock(hit.x, hit.y, hit.z)].name, hit.x, hit.y, hit.z,
             hit.distance);
  } else {
    snprintf(line, sizeof(line), "Target: none");
  }
  DrawText(line, 22, 131, 16, RAYWHITE);
}

static Rectangle MainMenuButtonBounds(int index) {
  float width = 260.0f;
  float height = 52.0f;
  float x = (GetScreenWidth() - width) * 0.5f;
  float buttonsHeight = 52.0f * 3.0f + 12.0f * 2.0f;
  float contentHeight = MENU_LOGO_HEIGHT + 24.0f + buttonsHeight;
  float top = (GetScreenHeight() - contentHeight) * 0.5f;
  float y = top + MENU_LOGO_HEIGHT + 24.0f + index * 64.0f;
  return (Rectangle){x, y, width, height};
}

static float ConfigRowY(int row) {
  return GetScreenHeight() * 0.5f - 85.0f + row * 80.0f;
}

static Rectangle ConfigArrowBounds(int row, bool rightArrow) {
  float centerX = GetScreenWidth() * 0.5f;
  return (Rectangle){centerX + (rightArrow ? 165.0f : 35.0f), ConfigRowY(row),
                     42.0f, 44.0f};
}

static Rectangle ConfigBackBounds(void) {
  float width = 200.0f;
  return (Rectangle){(GetScreenWidth() - width) * 0.5f,
                     GetScreenHeight() - 82.0f, width, 48.0f};
}

static bool MenuButtonClicked(Rectangle bounds) {
  return CheckCollisionPointRec(GetMousePosition(), bounds) &&
         IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void DrawMenuButton(Rectangle bounds, const char *label) {
  bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
  Color fill = hovered ? (Color){78, 105, 125, 245}
                       : (Color){34, 43, 52, 230};
  DrawRectangleRec(bounds, fill);
  DrawRectangleLinesEx(bounds, hovered ? 3.0f : 2.0f,
                       hovered ? (Color){235, 225, 190, 255}
                               : (Color){175, 180, 182, 255});
  int fontSize = 24;
  int textWidth = MeasureText(label, fontSize);
  DrawText(label, (int)(bounds.x + (bounds.width - textWidth) * 0.5f),
           (int)(bounds.y + (bounds.height - fontSize) * 0.5f), fontSize,
           RAYWHITE);
}

static void DrawMenuBackground(Texture2D background, bool hasBackground) {
  int width = GetScreenWidth();
  int height = GetScreenHeight();
  if (hasBackground && background.width > 0 && background.height > 0) {
    Rectangle source = {0.0f, 0.0f, (float)background.width,
                        (float)background.height};
    Rectangle destination = {0.0f, 0.0f, (float)width, (float)height};
    float sourceAspect = (float)background.width / background.height;
    float destinationAspect = (float)width / height;
    // Center-crop the wider image so it fills a 4:3 window without distortion.
    if (sourceAspect > destinationAspect) {
      source.width = background.height * destinationAspect;
      source.x = (background.width - source.width) * 0.5f;
    } else if (sourceAspect < destinationAspect) {
      source.height = background.width / destinationAspect;
      source.y = (background.height - source.height) * 0.5f;
    }
    DrawTexturePro(background, source, destination, (Vector2){0.0f, 0.0f}, 0.0f,
                   WHITE);
  } else {
    DrawRectangleGradientV(0, 0, width, height, (Color){72, 111, 142, 255},
                           (Color){31, 43, 49, 255});
  }
  DrawRectangle(0, 0, width, height, (Color){0, 0, 0, 75});
}

static void DrawMenuLogo(Texture2D logo, bool hasLogo, Rectangle bounds) {
  if (hasLogo) {
    float scaleX = bounds.width / logo.width;
    float scaleY = bounds.height / logo.height;
    float scale = scaleX < scaleY ? scaleX : scaleY;
    float width = logo.width * scale;
    float height = logo.height * scale;
    Rectangle source = {0.0f, 0.0f, (float)logo.width, (float)logo.height};
    Rectangle destination = {
        bounds.x + (bounds.width - width) * 0.5f,
        bounds.y + (bounds.height - height) * 0.5f,
        width,
        height};
    DrawTexturePro(logo, source, destination, (Vector2){0.0f, 0.0f}, 0.0f,
                   WHITE);
  } else {
    const char *title = "VEYLORIA";
    int fontSize = bounds.height > 120.0f ? 38 : 26;
    int x = (int)(bounds.x + (bounds.width - MeasureText(title, fontSize)) * 0.5f);
    int y = (int)(bounds.y + (bounds.height - fontSize) * 0.5f);
    DrawText(title, x, y, fontSize, RAYWHITE);
  }
}

static void DrawMainMenu(Texture2D background, bool hasBackground, Texture2D logo,
                         bool hasLogo) {
  DrawMenuBackground(background, hasBackground);
  Rectangle logoBounds = {
      (GetScreenWidth() - MENU_LOGO_WIDTH) * 0.5f,
      (MainMenuButtonBounds(0).y - 24.0f - MENU_LOGO_HEIGHT),
      (float)MENU_LOGO_WIDTH,
      (float)MENU_LOGO_HEIGHT};
  DrawMenuLogo(logo, hasLogo, logoBounds);
  DrawMenuButton(MainMenuButtonBounds(0), "PLAY");
  DrawMenuButton(MainMenuButtonBounds(1), "CONFIG");
  DrawMenuButton(MainMenuButtonBounds(2), "EXIT");
}

static void DrawConfigMenu(Texture2D background, bool hasBackground,
                           Texture2D logo, bool hasLogo,
                           const GameSettings *settings) {
  DrawMenuBackground(background, hasBackground);
  Rectangle configLogoBounds = {
      (GetScreenWidth() - 192.0f) * 0.5f, 20.0f, 192.0f, 96.0f};
  DrawMenuLogo(logo, hasLogo, configLogoBounds);
  const char *heading = "CONFIG";
  DrawText(heading, (GetScreenWidth() - MeasureText(heading, 24)) / 2, 122, 24,
           RAYWHITE);

  int centerX = GetScreenWidth() / 2;
  float rowY = ConfigRowY(0);
  DrawText("Resolution", centerX - 225, (int)rowY + 12, 20, RAYWHITE);
  DrawMenuButton(ConfigArrowBounds(0, false), "<");
  DrawMenuButton(ConfigArrowBounds(0, true), ">");
  const Resolution resolution = resolutions[settings->resolutionIndex];
  const char *resolutionText = TextFormat("%d x %d", resolution.width,
                                          resolution.height);
  DrawText(resolutionText, centerX + 82, (int)rowY + 13, 18, RAYWHITE);

  rowY = ConfigRowY(1);
  DrawText("Mouse sensitivity", centerX - 225, (int)rowY + 12, 20, RAYWHITE);
  DrawMenuButton(ConfigArrowBounds(1, false), "<");
  DrawMenuButton(ConfigArrowBounds(1, true), ">");
  const char *sensitivityText =
      TextFormat("%.4f", sensitivities[settings->sensitivityIndex]);
  DrawText(sensitivityText, centerX + 82, (int)rowY + 13, 18, RAYWHITE);

  DrawMenuButton(ConfigBackBounds(), "BACK");
}

static bool TryLoadAssetTexture(const char *path, Texture2D *texture) {
  if (!FileExists(path))
    return false;
  *texture = LoadTexture(path);
  return texture->id > 0;
}

static bool LoadAssetTexture(const char *relativePath, Texture2D *texture) {
  if (TryLoadAssetTexture(relativePath, texture))
    return true;

  const char *applicationDirectory = GetApplicationDirectory();
  char baseDirectory[512];
  size_t directoryLength = strlen(applicationDirectory);
  snprintf(baseDirectory, sizeof(baseDirectory), "%s%s", applicationDirectory,
           directoryLength > 0 && applicationDirectory[directoryLength - 1] == '/'
               ? ""
               : "/");

  char candidate[1024];
  snprintf(candidate, sizeof(candidate), "%s%s", baseDirectory, relativePath);
  if (TryLoadAssetTexture(candidate, texture))
    return true;

  snprintf(candidate, sizeof(candidate), "%s../%s", baseDirectory,
           relativePath);
  return TryLoadAssetTexture(candidate, texture);
}

static void LoadBlockTextures(void) {
  for (int id = 1; id < BLOCK_COUNT; id++) {
    const char *name = blockDefs[id].name;
    const char *separator = strrchr(name, ':');
    const char *fileName = separator ? separator + 1 : name;
    char texturePath[256];
    snprintf(texturePath, sizeof(texturePath), "assets/%s.png", fileName);
    blockTextureLoaded[id] =
        LoadAssetTexture(texturePath, &blockTextures[id]);
    if (!blockTextureLoaded[id])
      TraceLog(LOG_INFO, "No texture at %s; using the default block color.",
               texturePath);
  }
}

int main(void) {
  GameSettings settings = {DEFAULT_RESOLUTION_INDEX,
                           DEFAULT_SENSITIVITY_INDEX};
  LoadSettings(&settings);
  Resolution initialResolution = resolutions[settings.resolutionIndex];

  SetConfigFlags(FLAG_MSAA_4X_HINT);
  InitWindow(initialResolution.width, initialResolution.height,
             "Veyloria proto-26.10.09-03");
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);
  EnableCursor();

  Texture2D menuBackground = {0};
  Texture2D menuLogo = {0};
  bool hasMenuBackground =
      LoadAssetTexture(MENU_BACKGROUND_PATH, &menuBackground);
  bool hasMenuLogo = LoadAssetTexture(MENU_LOGO_PATH, &menuLogo);
  LoadBlockTextures();

  InitializeWorld();

  Player player = {
      .feet = {WORLD_WIDTH * 0.5f, TERRAIN_LAYERS, WORLD_DEPTH * 0.5f},
      .yaw = 0.0f,
      .pitch = 0.0f,
      .verticalVelocity = 0.0f,
      .grounded = false};
  int inventory[2] = {32, 32};
  int selectedSlot = 0;
  bool debugMode = false;
  bool mouseCaptured = false;
  bool autoSaveOnExit = true;
  bool exitRequested = false;
  ScreenState screen = SCREEN_MAIN_MENU;
  const BlockId hotbarBlocks[2] = {BLOCK_GRASS, BLOCK_STONE};

  if (FileExists(SAVE_FILE) && !LoadGame(&player, inventory, &selectedSlot)) {
    TraceLog(LOG_WARNING, "Could not load %s; starting with a fresh world.",
             SAVE_FILE);
    autoSaveOnExit = false;
  }
  BuildWorldModels();

  while (!WindowShouldClose() && !exitRequested) {
    float deltaTime = GetFrameTime();
    if (deltaTime > 0.05f)
      deltaTime = 0.05f;

    bool justEnteredPlay = false;
    if (screen == SCREEN_MAIN_MENU) {
      if (IsKeyPressed(KEY_ESCAPE)) {
        exitRequested = true;
      } else if (MenuButtonClicked(MainMenuButtonBounds(0))) {
        screen = SCREEN_PLAYING;
        justEnteredPlay = true;
      } else if (MenuButtonClicked(MainMenuButtonBounds(1))) {
        screen = SCREEN_CONFIG;
      } else if (MenuButtonClicked(MainMenuButtonBounds(2))) {
        exitRequested = true;
      }
    } else if (screen == SCREEN_CONFIG) {
      if (IsKeyPressed(KEY_ESCAPE) || MenuButtonClicked(ConfigBackBounds())) {
        screen = SCREEN_MAIN_MENU;
      } else {
        bool settingsChanged = false;
        if (MenuButtonClicked(ConfigArrowBounds(0, false))) {
          settings.resolutionIndex =
              (settings.resolutionIndex + RESOLUTION_COUNT - 1) %
              RESOLUTION_COUNT;
          settingsChanged = true;
        } else if (MenuButtonClicked(ConfigArrowBounds(0, true))) {
          settings.resolutionIndex =
              (settings.resolutionIndex + 1) % RESOLUTION_COUNT;
          settingsChanged = true;
        }

        if (MenuButtonClicked(ConfigArrowBounds(1, false))) {
          if (settings.sensitivityIndex > 0)
            settings.sensitivityIndex--;
          settingsChanged = true;
        } else if (MenuButtonClicked(ConfigArrowBounds(1, true))) {
          if (settings.sensitivityIndex < SENSITIVITY_COUNT - 1)
            settings.sensitivityIndex++;
          settingsChanged = true;
        }

        if (settingsChanged) {
          Resolution resolution = resolutions[settings.resolutionIndex];
          SetWindowSize(resolution.width, resolution.height);
          if (!SaveSettings(&settings))
            TraceLog(LOG_WARNING, "Could not save %s.", SETTINGS_FILE);
        }
      }
    }

    if (screen == SCREEN_PLAYING) {
      bool windowFocused = IsWindowFocused();
      bool justRegainedFocus = windowFocused && !mouseCaptured;
      if (windowFocused != mouseCaptured) {
        if (windowFocused)
          DisableCursor();
        else
          EnableCursor();
        mouseCaptured = windowFocused;
      }

      if (IsKeyPressed(KEY_ESCAPE)) {
        screen = SCREEN_MAIN_MENU;
      } else {
        if (IsKeyPressed(KEY_ONE))
          selectedSlot = 0;
        if (IsKeyPressed(KEY_TWO))
          selectedSlot = 1;
        if (IsKeyPressed(KEY_F1))
          debugMode = !debugMode;
        if (IsKeyPressed(KEY_F5)) {
          if (SaveGame(&player, inventory, selectedSlot)) {
            autoSaveOnExit = true;
            TraceLog(LOG_INFO, "World saved to %s.", SAVE_FILE);
          } else {
            TraceLog(LOG_WARNING, "Could not save world to %s.", SAVE_FILE);
          }
        }
        if (IsKeyPressed(KEY_F9)) {
          if (LoadGame(&player, inventory, &selectedSlot)) {
            BuildWorldModels();
            autoSaveOnExit = true;
            TraceLog(LOG_INFO, "World loaded from %s.", SAVE_FILE);
          } else {
            TraceLog(LOG_WARNING, "Could not load world from %s.", SAVE_FILE);
          }
        }

        UpdatePlayer(&player, deltaTime,
                     windowFocused && !justRegainedFocus,
                     sensitivities[settings.sensitivityIndex]);

        Vector3 eye = PlayerEye(&player);
        RayHit target = CastBlockRay(eye, LookDirection(&player));
        bool changedWorld = false;
        if (!justEnteredPlay && target.hit &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
          BlockId broken = GetBlock(target.x, target.y, target.z);
          int slot = (broken == BLOCK_GRASS) ? 0
                                             : (broken == BLOCK_STONE) ? 1 : -1;
          if (slot >= 0 && inventory[slot] < INVENTORY_CAPACITY)
            inventory[slot]++;
          SetBlock(target.x, target.y, target.z, BLOCK_AIR);
          changedWorld = true;
        } else if (!justEnteredPlay && target.hit &&
                   IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
          BlockId placing = hotbarBlocks[selectedSlot];
          if (inventory[selectedSlot] > 0 &&
              InWorld(target.placeX, target.placeY, target.placeZ) &&
              GetBlock(target.placeX, target.placeY, target.placeZ) == BLOCK_AIR &&
              !PlacementOverlapsPlayer(&player, target.placeX, target.placeY,
                                       target.placeZ)) {
            SetBlock(target.placeX, target.placeY, target.placeZ, placing);
            inventory[selectedSlot]--;
            changedWorld = true;
          }
        }
        if (changedWorld)
          BuildWorldModels();
      }
    }

    if (screen != SCREEN_PLAYING && mouseCaptured) {
      EnableCursor();
      mouseCaptured = false;
    }

    BeginDrawing();
    if (screen == SCREEN_PLAYING) {
      Vector3 eye = PlayerEye(&player);
      Vector3 look = LookDirection(&player);
      Camera3D camera = {
          .position = eye,
          .target = {eye.x + look.x, eye.y + look.y, eye.z + look.z},
          .up = {0.0f, 1.0f, 0.0f},
          .fovy = 75.0f,
          .projection = CAMERA_PERSPECTIVE};
      RayHit target = CastBlockRay(eye, look);

      ClearBackground((Color){135, 190, 235, 255});
      BeginMode3D(camera);
      for (int id = 1; id < BLOCK_COUNT; id++) {
        if (worldModels[id].meshCount > 0)
          DrawModel(worldModels[id], (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
      }
      if (target.hit) {
        DrawCubeWires(
            (Vector3){target.x + 0.5f, target.y + 0.5f, target.z + 0.5f},
            1.01f, 1.01f, 1.01f, RAYWHITE);
      }
      EndMode3D();

      int centerX = GetScreenWidth() / 2;
      int centerY = GetScreenHeight() / 2;
      DrawLine(centerX - 7, centerY, centerX + 7, centerY, RAYWHITE);
      DrawLine(centerX, centerY - 7, centerX, centerY + 7, RAYWHITE);
      DrawHotbar(selectedSlot, inventory);
      if (debugMode)
        DrawDebug(&player, hotbarBlocks[selectedSlot], inventory, target);
    } else if (screen == SCREEN_MAIN_MENU) {
      DrawMainMenu(menuBackground, hasMenuBackground, menuLogo, hasMenuLogo);
    } else {
      DrawConfigMenu(menuBackground, hasMenuBackground, menuLogo, hasMenuLogo,
                     &settings);
    }
    EndDrawing();
  }

  if (autoSaveOnExit && !SaveGame(&player, inventory, selectedSlot))
    TraceLog(LOG_WARNING, "Could not save world to %s on exit.", SAVE_FILE);
  UnloadWorldModels();
  for (int id = 1; id < BLOCK_COUNT; id++) {
    if (blockTextureLoaded[id])
      UnloadTexture(blockTextures[id]);
  }
  if (hasMenuBackground)
    UnloadTexture(menuBackground);
  if (hasMenuLogo)
    UnloadTexture(menuLogo);
  CloseWindow();
  return 0;
}
