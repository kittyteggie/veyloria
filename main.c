#include "blocks.h"
#include "player.h"
#include "save.h"
#include "version.h"
#include "world.h"

#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define TERRAIN_LAYERS 3
#define REACH_DISTANCE 6.0f
#define HOTBAR_SIZE 2
#define DEFAULT_RESOLUTION_INDEX 1
#define DEFAULT_SENSITIVITY_INDEX 2
#define DEFAULT_FOV 75
#define DEFAULT_VOLUME 80
#define FOV_MIN 60
#define FOV_MAX 110
#define FOV_STEP 5
#define VOLUME_STEP 10
#define RESOLUTION_COUNT 7
#define SENSITIVITY_COUNT 8
#define SETTINGS_FILE "veyloria.cfg"
#define SETTINGS_TEMP_FILE "veyloria.cfg.tmp"
#define SAVE_FILE "veyloria-world.sav"
#define SAVE_TEMP_FILE "veyloria-world.sav.tmp"
#define SAVE_BACKUP_FILE "veyloria-world.sav.bak"
#define MENU_BACKGROUND_PATH "assets/menubg.png"
#define MENU_LOGO_PATH "assets/logo.png"
#define MENU_LOGO_WIDTH 384
#define MENU_LOGO_HEIGHT 192
#define FRUSTUM_FAR 1200.0f

// SUMMARY: Runs the voxel game, chunked renderer, menus, audio, and settings.

typedef struct MeshBuilder {
  float *vertices;
  float *normals;
  float *texcoords;
  unsigned char *colors;
  int vertexCount;
  int capacity;
} MeshBuilder;

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

typedef struct ChunkRender {
  Model models[BLOCK_COUNT];
} ChunkRender;

typedef struct Frustum {
  Vector3 normal[6];
  float distance[6];
} Frustum;

typedef struct Resolution {
  int width;
  int height;
} Resolution;

typedef struct GameSettings {
  int resolutionIndex;
  int sensitivityIndex;
  int fovDegrees;
  int volume;
  bool fullscreen;
  bool vsync;
} GameSettings;

typedef struct GameAudio {
  Sound breakSound;
  Sound placeSound;
  Sound stepSound;
  bool ready;
} GameAudio;

typedef enum ScreenState {
  SCREEN_MAIN_MENU,
  SCREEN_CONFIG,
  SCREEN_PLAYING
} ScreenState;

typedef enum ConfigField {
  CFG_RESOLUTION,
  CFG_FULLSCREEN,
  CFG_FOV,
  CFG_SENSITIVITY,
  CFG_VSYNC,
  CFG_VOLUME,
  CFG_FIELD_COUNT
} ConfigField;

static World world;
static ChunkRender chunkRenders[CHUNKS_X][CHUNKS_Y][CHUNKS_Z];
static Texture2D blockTextures[BLOCK_COUNT];
static bool blockTextureLoaded[BLOCK_COUNT];
static GameAudio audio;
static const Resolution resolutions[RESOLUTION_COUNT] = {
    {640, 480},  {WINDOW_WIDTH, WINDOW_HEIGHT}, {1024, 768}, {1280, 720},
    {1280, 960}, {1600, 900},                   {1920, 1080}};
static const float sensitivities[SENSITIVITY_COUNT] = {
    0.0005f, 0.0010f, 0.0015f, 0.0020f,
    0.0025f, 0.0030f, 0.0035f, 0.0040f};
static const char *configFieldNames[CFG_FIELD_COUNT] = {
    "Resolution", "Fullscreen", "Field of view", "Mouse sensitivity", "VSync",
    "Volume"};

static bool InWorld(int x, int y, int z) { return WorldInBounds(x, y, z); }

static BlockId GetBlock(int x, int y, int z) {
  return WorldGetBlock(&world, x, y, z);
}

static void SetBlock(int x, int y, int z, BlockId id) {
  WorldSetBlock(&world, x, y, z, id);
}

static void InitializeWorld(void) {
  WorldInit(&world);
  for (int x = 0; x < WORLD_WIDTH; x++) {
    for (int z = 0; z < WORLD_DEPTH; z++) {
      for (int y = 0; y < TERRAIN_LAYERS; y++) {
        // The surface is grass; the two layers beneath it are stone.
        SetBlock(x, y, z, (y == TERRAIN_LAYERS - 1) ? BLOCK_GRASS
                                                   : BLOCK_STONE);
      }
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
      MemRealloc(builder->vertices, (unsigned int)(newCapacity * 3 * sizeof(float)));
  builder->normals =
      MemRealloc(builder->normals, (unsigned int)(newCapacity * 3 * sizeof(float)));
  builder->texcoords =
      MemRealloc(builder->texcoords, (unsigned int)(newCapacity * 2 * sizeof(float)));
  builder->colors = MemRealloc(
      builder->colors, (unsigned int)(newCapacity * 4 * sizeof(unsigned char)));
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

static void UnloadChunkModels(int cx, int cy, int cz) {
  for (int id = 1; id < BLOCK_COUNT; id++) {
    if (chunkRenders[cx][cy][cz].models[id].meshCount > 0)
      UnloadModel(chunkRenders[cx][cy][cz].models[id]);
  }
  memset(&chunkRenders[cx][cy][cz], 0, sizeof(chunkRenders[cx][cy][cz]));
}

static void UnloadAllChunkModels(void) {
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++)
        UnloadChunkModels(cx, cy, cz);
    }
  }
}

static void BuildChunkMesh(int cx, int cy, int cz) {
  static const int neighbor[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  MeshBuilder builders[BLOCK_COUNT] = {0};
  const Chunk *chunk = WorldChunkAtConst(&world, cx, cy, cz);

  if (chunk != NULL && chunk->maxLocalY >= 0) {
    int baseX = cx * CHUNK_SIZE;
    int baseY = cy * CHUNK_SIZE;
    int baseZ = cz * CHUNK_SIZE;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
      for (int lz = 0; lz < CHUNK_SIZE; lz++) {
        for (int ly = 0; ly <= chunk->maxLocalY; ly++) {
          BlockId id = chunk->blocks[lx][ly][lz];
          if (id == BLOCK_AIR)
            continue;

          for (int face = 0; face < 6; face++) {
            int nx = baseX + lx + neighbor[face][0];
            int ny = baseY + ly + neighbor[face][1];
            int nz = baseZ + lz + neighbor[face][2];
            if (!blockDefs[GetBlock(nx, ny, nz)].solid) {
              AddFace(&builders[id], baseX + lx, baseY + ly, baseZ + lz, id,
                      face, blockTextureLoaded[id]);
            }
          }
        }
      }
    }
  }

  UnloadChunkModels(cx, cy, cz);
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
    chunkRenders[cx][cy][cz].models[id] = LoadModelFromMesh(mesh);
    if (blockTextureLoaded[id] &&
        chunkRenders[cx][cy][cz].models[id].materialCount > 0) {
      SetMaterialTexture(&chunkRenders[cx][cy][cz].models[id].materials[0],
                         MATERIAL_MAP_DIFFUSE, blockTextures[id]);
    }
  }
  WorldMarkChunkClean(&world, cx, cy, cz);
}

static void RebuildDirtyChunkMeshes(void) {
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++) {
        if (WorldChunkDirty(&world, cx, cy, cz))
          BuildChunkMesh(cx, cy, cz);
      }
    }
  }
}

static void ReloadWorldMeshes(void) {
  UnloadAllChunkModels();
  WorldMarkAllChunksDirty(&world);
  RebuildDirtyChunkMeshes();
}

static Frustum MakeFrustum(Vector3 eye, Vector3 forward, Vector3 cameraUp,
                           float fovDegrees, float aspect) {
  Vector3 f = Vector3Normalize(forward);
  Vector3 r = Vector3Normalize(Vector3CrossProduct(f, cameraUp));
  Vector3 u = Vector3CrossProduct(r, f);
  float tanV = tanf(fovDegrees * DEG2RAD * 0.5f);
  float tanH = tanV * aspect;

  Vector3 leftDir = Vector3Subtract(f, Vector3Scale(r, tanH));
  Vector3 rightDir = Vector3Add(f, Vector3Scale(r, tanH));
  Vector3 upDir = Vector3Add(f, Vector3Scale(u, tanV));
  Vector3 downDir = Vector3Subtract(f, Vector3Scale(u, tanV));

  Frustum frustum;
  frustum.normal[0] = Vector3Normalize(Vector3CrossProduct(leftDir, u));
  frustum.normal[1] = Vector3Normalize(Vector3CrossProduct(u, rightDir));
  frustum.normal[2] = Vector3Normalize(Vector3CrossProduct(upDir, r));
  frustum.normal[3] = Vector3Normalize(Vector3CrossProduct(r, downDir));
  frustum.normal[4] = f;
  frustum.normal[5] = Vector3Scale(f, -1.0f);
  for (int i = 0; i < 5; i++)
    frustum.distance[i] = -Vector3DotProduct(frustum.normal[i], eye);
  frustum.distance[5] = Vector3DotProduct(f, eye) + FRUSTUM_FAR;
  return frustum;
}

static bool FrustumVisible(const Frustum *frustum, Vector3 min, Vector3 max) {
  for (int i = 0; i < 6; i++) {
    Vector3 corner = {
        frustum->normal[i].x >= 0.0f ? max.x : min.x,
        frustum->normal[i].y >= 0.0f ? max.y : min.y,
        frustum->normal[i].z >= 0.0f ? max.z : min.z,
    };
    if (Vector3DotProduct(frustum->normal[i], corner) + frustum->distance[i] <
        0.0f)
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
  if (player->pitch > MAX_PITCH)
    player->pitch = MAX_PITCH;
  if (player->pitch < -MAX_PITCH)
    player->pitch = -MAX_PITCH;

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

static const char *BlockShortName(BlockId id) {
  const char *name = blockDefs[id].name;
  const char *separator = strrchr(name, ':');
  return separator ? separator + 1 : name;
}

static void GiveBlock(ItemStack slots[], int slotCount, BlockId id) {
  if (id == BLOCK_AIR)
    return;

  for (int i = 0; i < slotCount; i++) {
    if (slots[i].id == id && slots[i].count < INVENTORY_CAPACITY) {
      slots[i].count++;
      return;
    }
  }
  for (int i = 0; i < slotCount; i++) {
    if (slots[i].id == BLOCK_AIR) {
      slots[i].id = id;
      slots[i].count = 1;
      return;
    }
  }
}

static void DrawBlockIcon(Rectangle slot, BlockId id) {
  if (id == BLOCK_AIR)
    return;

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

static void DrawHotbar(int selectedSlot, const ItemStack slots[]) {
  const int slotSize = 58;
  const int gap = 6;
  int totalWidth = slotSize * HOTBAR_SIZE + gap * (HOTBAR_SIZE - 1);
  int startX = (GetScreenWidth() - totalWidth) / 2;
  int y = GetScreenHeight() - slotSize - 22;

  for (int i = 0; i < HOTBAR_SIZE; i++) {
    Rectangle slot = {(float)(startX + i * (slotSize + gap)), (float)y,
                      (float)slotSize, (float)slotSize};
    DrawRectangleRec(slot, (Color){21, 22, 24, 220});
    DrawRectangleLinesEx(slot, (i == selectedSlot) ? 3.0f : 1.0f,
                         (i == selectedSlot) ? RAYWHITE
                                             : (Color){120, 120, 120, 220});
    DrawBlockIcon(slot, slots[i].id);

    DrawRectangle((int)slot.x + 7, y + slotSize - 8, slotSize - 14, 3,
                  (Color){45, 45, 45, 255});
    int barWidth = (slotSize - 14) * slots[i].count / INVENTORY_CAPACITY;
    if (barWidth > 0)
      DrawRectangle((int)slot.x + 7, y + slotSize - 8, barWidth, 3, RAYWHITE);
  }
}

static void DrawDebug(const Player *player, const ItemStack slots[],
                      int selectedSlot, RayHit hit, int chunksDrawn,
                      int chunksTotal) {
  DrawRectangle(12, 12, 360, 196, (Color){0, 0, 0, 170});
  Vector3 eye = PlayerEye(player);
  DrawText("VEYLORIA DEBUG", 22, 20, 18, RAYWHITE);

  char line[128];
  snprintf(line, sizeof(line), "Version: %s", VEYLORIA_VERSION);
  DrawText(line, 22, 47, 16, RAYWHITE);
  snprintf(line, sizeof(line), "Block in hand: %s",
           blockDefs[slots[selectedSlot].id].name);
  DrawText(line, 22, 68, 16, RAYWHITE);
  snprintf(line, sizeof(line), "Position: %.2f, %.2f, %.2f", player->feet.x,
           player->feet.y, player->feet.z);
  DrawText(line, 22, 89, 16, RAYWHITE);
  snprintf(line, sizeof(line), "Eye: %.2f, %.2f, %.2f", eye.x, eye.y, eye.z);
  DrawText(line, 22, 110, 16, RAYWHITE);
  snprintf(line, sizeof(line), "Hotbar: %s %d | %s %d",
           BlockShortName(slots[0].id), slots[0].count,
           BlockShortName(slots[1].id), slots[1].count);
  DrawText(line, 22, 131, 16, RAYWHITE);
  if (hit.hit) {
    snprintf(line, sizeof(line), "Target: %s at %d, %d, %d (%.1fm)",
             blockDefs[GetBlock(hit.x, hit.y, hit.z)].name, hit.x, hit.y, hit.z,
             hit.distance);
  } else {
    snprintf(line, sizeof(line), "Target: none");
  }
  DrawText(line, 22, 152, 16, RAYWHITE);
  snprintf(line, sizeof(line), "Chunks: %d drawn / %d loaded", chunksDrawn,
           chunksTotal);
  DrawText(line, 22, 173, 16, RAYWHITE);
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
  return GetScreenHeight() * 0.28f + row * (GetScreenHeight() * 0.09f);
}

static Rectangle ConfigArrowBounds(int row, bool rightArrow) {
  float centerX = GetScreenWidth() * 0.5f;
  return (Rectangle){centerX + (rightArrow ? 180.0f : 20.0f), ConfigRowY(row),
                     38.0f, 36.0f};
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

static void ConfigFieldText(const GameSettings *settings, ConfigField field,
                            char *buffer, size_t size) {
  switch (field) {
  case CFG_RESOLUTION: {
    const Resolution resolution = resolutions[settings->resolutionIndex];
    snprintf(buffer, size, "%d x %d", resolution.width, resolution.height);
    break;
  }
  case CFG_FULLSCREEN:
    snprintf(buffer, size, "%s", settings->fullscreen ? "On" : "Off");
    break;
  case CFG_FOV:
    snprintf(buffer, size, "%d", settings->fovDegrees);
    break;
  case CFG_SENSITIVITY:
    snprintf(buffer, size, "%.4f", sensitivities[settings->sensitivityIndex]);
    break;
  case CFG_VSYNC:
    snprintf(buffer, size, "%s", settings->vsync ? "On" : "Off");
    break;
  case CFG_VOLUME:
    snprintf(buffer, size, "%d%%", settings->volume);
    break;
  default:
    snprintf(buffer, size, "?");
    break;
  }
}

static void AdjustConfig(GameSettings *settings, ConfigField field, int step) {
  switch (field) {
  case CFG_RESOLUTION:
    settings->resolutionIndex =
        (settings->resolutionIndex + RESOLUTION_COUNT + step) % RESOLUTION_COUNT;
    break;
  case CFG_FULLSCREEN:
    settings->fullscreen = !settings->fullscreen;
    break;
  case CFG_FOV:
    settings->fovDegrees += step * FOV_STEP;
    if (settings->fovDegrees < FOV_MIN)
      settings->fovDegrees = FOV_MIN;
    if (settings->fovDegrees > FOV_MAX)
      settings->fovDegrees = FOV_MAX;
    break;
  case CFG_SENSITIVITY:
    settings->sensitivityIndex += step;
    if (settings->sensitivityIndex < 0)
      settings->sensitivityIndex = 0;
    if (settings->sensitivityIndex > SENSITIVITY_COUNT - 1)
      settings->sensitivityIndex = SENSITIVITY_COUNT - 1;
    break;
  case CFG_VSYNC:
    settings->vsync = !settings->vsync;
    break;
  case CFG_VOLUME:
    settings->volume += step * VOLUME_STEP;
    if (settings->volume < 0)
      settings->volume = 0;
    if (settings->volume > 100)
      settings->volume = 100;
    break;
  default:
    break;
  }
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
  char value[32];
  for (int row = 0; row < CFG_FIELD_COUNT; row++) {
    float rowY = ConfigRowY(row);
    DrawText(configFieldNames[row], centerX - 225, (int)rowY + 8, 20, RAYWHITE);
    DrawMenuButton(ConfigArrowBounds(row, false), "<");
    DrawMenuButton(ConfigArrowBounds(row, true), ">");
    ConfigFieldText(settings, (ConfigField)row, value, sizeof(value));
    DrawText(value, centerX + 66, (int)rowY + 10, 18, RAYWHITE);
  }

  DrawMenuButton(ConfigBackBounds(), "BACK");
}

static void ApplySettings(GameSettings *settings);

static void LoadSettings(GameSettings *settings) {
  FILE *file = fopen(SETTINGS_FILE, "r");
  if (!file)
    return;

  char line[64];
  while (fgets(line, sizeof(line), file)) {
    char key[32];
    int value;
    if (sscanf(line, "%31[^=]=%d", key, &value) != 2)
      continue;

    if (strcmp(key, "resolution") == 0 && value >= 0 &&
        value < RESOLUTION_COUNT)
      settings->resolutionIndex = value;
    else if (strcmp(key, "sensitivity") == 0 && value >= 0 &&
             value < SENSITIVITY_COUNT)
      settings->sensitivityIndex = value;
    else if (strcmp(key, "fov") == 0 && value >= FOV_MIN && value <= FOV_MAX)
      settings->fovDegrees = value;
    else if (strcmp(key, "volume") == 0 && value >= 0 && value <= 100)
      settings->volume = value;
    else if (strcmp(key, "fullscreen") == 0)
      settings->fullscreen = value != 0;
    else if (strcmp(key, "vsync") == 0)
      settings->vsync = value != 0;
  }
  fclose(file);
}

static bool SaveSettings(const GameSettings *settings) {
  FILE *file = fopen(SETTINGS_TEMP_FILE, "w");
  if (!file)
    return false;

  bool success =
      fprintf(file, "resolution=%d\nsensitivity=%d\nfov=%d\nfullscreen=%d\n"
                    "vsync=%d\nvolume=%d\n",
              settings->resolutionIndex, settings->sensitivityIndex,
              settings->fovDegrees, settings->fullscreen ? 1 : 0,
              settings->vsync ? 1 : 0, settings->volume) > 0;
  if (fclose(file) != 0)
    success = false;
  if (!success || rename(SETTINGS_TEMP_FILE, SETTINGS_FILE) != 0) {
    remove(SETTINGS_TEMP_FILE);
    return false;
  }
  return true;
}

static void ApplySettings(GameSettings *settings) {
  if (settings->fullscreen != IsWindowFullscreen())
    ToggleFullscreen();
  if (!settings->fullscreen) {
    Resolution resolution = resolutions[settings->resolutionIndex];
    SetWindowSize(resolution.width, resolution.height);
  }

  if (settings->vsync)
    SetWindowState(FLAG_VSYNC_HINT);
  else
    ClearWindowState(FLAG_VSYNC_HINT);
  SetTargetFPS(settings->vsync ? 0 : 60);
  SetMasterVolume((float)settings->volume / 100.0f);

  if (!SaveSettings(settings))
    TraceLog(LOG_WARNING, "Could not save %s.", SETTINGS_FILE);
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
    char texturePath[256];
    snprintf(texturePath, sizeof(texturePath), "assets/%s.png",
             BlockShortName((BlockId)id));
    blockTextureLoaded[id] =
        LoadAssetTexture(texturePath, &blockTextures[id]);
    if (blockTextureLoaded[id]) {
      // Block textures are pixel art; keep them crisp when magnified.
      SetTextureFilter(blockTextures[id], TEXTURE_FILTER_POINT);
    } else {
      TraceLog(LOG_INFO, "No texture at %s; using the default block color.",
               texturePath);
    }
  }
}

static Wave MakeSoundWave(float seconds, float frequency, float noiseAmount,
                          float decay, unsigned int *seed) {
  unsigned int sampleRate = 22050;
  unsigned int frameCount = (unsigned int)(seconds * (float)sampleRate);
  if (frameCount < 1)
    frameCount = 1;

  short *samples = (short *)MemAlloc(frameCount * sizeof(short));
  for (unsigned int i = 0; i < frameCount; i++) {
    float t = (float)i / (float)sampleRate;
    float envelope = expf(-decay * t);
    *seed = (*seed) * 1103515245u + 12345u;
    float noise = (float)((*seed >> 16) & 0x7fff) / 16384.0f - 1.0f;
    float tone = sinf(2.0f * PI * frequency * t);
    float sample = tone * (1.0f - noiseAmount) + noise * noiseAmount;
    samples[i] = (short)(sample * envelope * 22000.0f);
  }

  Wave wave = {0};
  wave.frameCount = frameCount;
  wave.sampleRate = sampleRate;
  wave.sampleSize = 16;
  wave.channels = 1;
  wave.data = samples;
  return wave;
}

static Sound MakeSound(float seconds, float frequency, float noiseAmount,
                       float decay, unsigned int *seed, float volume) {
  Wave wave = MakeSoundWave(seconds, frequency, noiseAmount, decay, seed);
  Sound sound = LoadSoundFromWave(wave);
  UnloadWave(wave);
  SetSoundVolume(sound, volume);
  return sound;
}

static void InitGameAudio(GameAudio *gameAudio) {
  memset(gameAudio, 0, sizeof(*gameAudio));
  InitAudioDevice();
  if (!IsAudioDeviceReady()) {
    TraceLog(LOG_WARNING, "Audio device unavailable; sound is disabled.");
    return;
  }

  unsigned int seed = 0x9e3779b9u;
  gameAudio->breakSound = MakeSound(0.16f, 160.0f, 0.85f, 16.0f, &seed, 0.9f);
  gameAudio->placeSound = MakeSound(0.10f, 120.0f, 0.30f, 28.0f, &seed, 0.9f);
  gameAudio->stepSound = MakeSound(0.07f, 90.0f, 0.90f, 40.0f, &seed, 0.5f);
  gameAudio->ready = true;
}

static void UnloadGameAudio(GameAudio *gameAudio) {
  if (gameAudio->ready) {
    UnloadSound(gameAudio->breakSound);
    UnloadSound(gameAudio->placeSound);
    UnloadSound(gameAudio->stepSound);
    gameAudio->ready = false;
  }
  if (IsAudioDeviceReady())
    CloseAudioDevice();
}

static void PlayGameSound(const GameAudio *gameAudio, Sound sound) {
  if (gameAudio->ready && IsSoundValid(sound))
    PlaySound(sound);
}

int main(void) {
  GameSettings settings = {DEFAULT_RESOLUTION_INDEX, DEFAULT_SENSITIVITY_INDEX,
                           DEFAULT_FOV, DEFAULT_VOLUME, false, true};
  LoadSettings(&settings);
  Resolution initialResolution = resolutions[settings.resolutionIndex];

  unsigned int windowFlags = FLAG_MSAA_4X_HINT;
  if (settings.vsync)
    windowFlags |= FLAG_VSYNC_HINT;
  if (settings.fullscreen)
    windowFlags |= FLAG_FULLSCREEN_MODE;
  SetConfigFlags(windowFlags);
  InitWindow(initialResolution.width, initialResolution.height,
             "Veyloria " VEYLORIA_VERSION);
  SetExitKey(KEY_NULL);
  SetTargetFPS(settings.vsync ? 0 : 60);
  SetMasterVolume((float)settings.volume / 100.0f);
  EnableCursor();
  InitGameAudio(&audio);

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
  ItemStack hotbar[HOTBAR_SIZE] = {{BLOCK_GRASS, 32}, {BLOCK_STONE, 32}};
  int selectedSlot = 0;
  bool debugMode = false;
  bool mouseCaptured = false;
  bool exitRequested = false;
  float stepTimer = 0.0f;
  ScreenState screen = SCREEN_MAIN_MENU;

  if (FileExists(SAVE_FILE) &&
      !LoadGame(SAVE_FILE, &world, &player, hotbar, HOTBAR_SIZE, &selectedSlot)) {
    TraceLog(LOG_WARNING,
             "Could not load %s; starting a fresh world and backing it up.",
             SAVE_FILE);
    if (rename(SAVE_FILE, SAVE_BACKUP_FILE) == 0)
      TraceLog(LOG_INFO, "Previous save moved to %s.", SAVE_BACKUP_FILE);
  }
  ReloadWorldMeshes();

  while (!WindowShouldClose() && !exitRequested) {
    float deltaTime = GetFrameTime();
    if (deltaTime > 0.05f)
      deltaTime = 0.05f;

    if (IsKeyPressed(KEY_F11)) {
      settings.fullscreen = !settings.fullscreen;
      ApplySettings(&settings);
    }

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
        for (int row = 0; row < CFG_FIELD_COUNT; row++) {
          if (MenuButtonClicked(ConfigArrowBounds(row, false))) {
            AdjustConfig(&settings, (ConfigField)row, -1);
            settingsChanged = true;
          } else if (MenuButtonClicked(ConfigArrowBounds(row, true))) {
            AdjustConfig(&settings, (ConfigField)row, 1);
            settingsChanged = true;
          }
        }
        if (settingsChanged)
          ApplySettings(&settings);
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
          if (SaveGame(SAVE_FILE, SAVE_TEMP_FILE, &world, &player, hotbar,
                       HOTBAR_SIZE, selectedSlot)) {
            TraceLog(LOG_INFO, "World saved to %s.", SAVE_FILE);
          } else {
            TraceLog(LOG_WARNING, "Could not save world to %s.", SAVE_FILE);
          }
        }
        if (IsKeyPressed(KEY_F9)) {
          if (LoadGame(SAVE_FILE, &world, &player, hotbar, HOTBAR_SIZE,
                       &selectedSlot)) {
            ReloadWorldMeshes();
            TraceLog(LOG_INFO, "World loaded from %s.", SAVE_FILE);
          } else {
            TraceLog(LOG_WARNING, "Could not load world from %s.", SAVE_FILE);
          }
        }

        UpdatePlayer(&player, deltaTime,
                     windowFocused && !justRegainedFocus,
                     sensitivities[settings.sensitivityIndex]);

        bool moving = player.grounded &&
                      (IsKeyDown(KEY_W) || IsKeyDown(KEY_S) ||
                       IsKeyDown(KEY_A) || IsKeyDown(KEY_D));
        if (moving) {
          stepTimer -= deltaTime;
          if (stepTimer <= 0.0f) {
            PlayGameSound(&audio, audio.stepSound);
            stepTimer = IsKeyDown(KEY_LEFT_SHIFT) ? 0.32f : 0.45f;
          }
        } else {
          stepTimer = 0.15f;
        }

        Vector3 eye = PlayerEye(&player);
        RayHit target = CastBlockRay(eye, LookDirection(&player));
        bool changedWorld = false;
        if (!justEnteredPlay && target.hit &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
          GiveBlock(hotbar, HOTBAR_SIZE, GetBlock(target.x, target.y, target.z));
          SetBlock(target.x, target.y, target.z, BLOCK_AIR);
          changedWorld = true;
          PlayGameSound(&audio, audio.breakSound);
        } else if (!justEnteredPlay && target.hit &&
                   IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
          ItemStack *stack = &hotbar[selectedSlot];
          if (stack->id != BLOCK_AIR && stack->count > 0 &&
              InWorld(target.placeX, target.placeY, target.placeZ) &&
              GetBlock(target.placeX, target.placeY, target.placeZ) ==
                  BLOCK_AIR &&
              !PlacementOverlapsPlayer(&player, target.placeX, target.placeY,
                                       target.placeZ)) {
            SetBlock(target.placeX, target.placeY, target.placeZ, stack->id);
            stack->count--;
            if (stack->count == 0)
              stack->id = BLOCK_AIR;
            changedWorld = true;
            PlayGameSound(&audio, audio.placeSound);
          }
        }
        if (changedWorld)
          RebuildDirtyChunkMeshes();
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
          .fovy = (float)settings.fovDegrees,
          .projection = CAMERA_PERSPECTIVE};
      RayHit target = CastBlockRay(eye, look);

      ClearBackground((Color){135, 190, 235, 255});
      Frustum frustum =
          MakeFrustum(eye, look, camera.up, camera.fovy,
                      (float)GetScreenWidth() / (float)GetScreenHeight());
      int chunksDrawn = 0;
      int chunksTotal = 0;

      BeginMode3D(camera);
      for (int cx = 0; cx < CHUNKS_X; cx++) {
        for (int cy = 0; cy < CHUNKS_Y; cy++) {
          for (int cz = 0; cz < CHUNKS_Z; cz++) {
            const Model *models = chunkRenders[cx][cy][cz].models;
            bool hasMesh = false;
            for (int id = 1; id < BLOCK_COUNT; id++) {
              if (models[id].meshCount > 0) {
                hasMesh = true;
                break;
              }
            }
            if (!hasMesh)
              continue;

            chunksTotal++;
            Vector3 min = {(float)(cx * CHUNK_SIZE), (float)(cy * CHUNK_SIZE),
                           (float)(cz * CHUNK_SIZE)};
            Vector3 max = {min.x + CHUNK_SIZE, min.y + CHUNK_SIZE,
                           min.z + CHUNK_SIZE};
            if (!FrustumVisible(&frustum, min, max))
              continue;

            chunksDrawn++;
            for (int id = 1; id < BLOCK_COUNT; id++) {
              if (models[id].meshCount > 0)
                DrawModel(models[id], (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            }
          }
        }
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
      DrawHotbar(selectedSlot, hotbar);
      if (debugMode)
        DrawDebug(&player, hotbar, selectedSlot, target, chunksDrawn,
                  chunksTotal);
    } else if (screen == SCREEN_MAIN_MENU) {
      DrawMainMenu(menuBackground, hasMenuBackground, menuLogo, hasMenuLogo);
    } else {
      DrawConfigMenu(menuBackground, hasMenuBackground, menuLogo, hasMenuLogo,
                     &settings);
    }
    EndDrawing();
  }

  if (!SaveGame(SAVE_FILE, SAVE_TEMP_FILE, &world, &player, hotbar,
                HOTBAR_SIZE, selectedSlot))
    TraceLog(LOG_WARNING, "Could not save world to %s on exit.", SAVE_FILE);

  UnloadAllChunkModels();
  WorldFree(&world);
  for (int id = 1; id < BLOCK_COUNT; id++) {
    if (blockTextureLoaded[id])
      UnloadTexture(blockTextures[id]);
  }
  if (hasMenuBackground)
    UnloadTexture(menuBackground);
  if (hasMenuLogo)
    UnloadTexture(menuLogo);
  UnloadGameAudio(&audio);
  CloseWindow();
  return 0;
}
