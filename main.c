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
#define MOUSE_SENSITIVITY 0.0015f
#define REACH_DISTANCE 6.0f
#define INVENTORY_CAPACITY 64
#define PI 3.14159265358979323846f

// SUMMARY: Runs a 256-cubed first-person voxel world with a three-layer base,
// block editing, and a hotbar.

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

static World world;
static Model worldModel = {0};
static bool worldModelLoaded;

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

static Color BlockFaceColor(BlockId id, int face) {
  Color base;
  if (id == BLOCK_GRASS) {
    base =
        (face == 2) ? (Color){101, 176, 72, 255} : (Color){105, 139, 70, 255};
  } else {
    base = (Color){132, 136, 143, 255};
  }

  static const float shade[] = {0.78f, 0.54f, 1.0f, 0.58f, 0.88f, 0.70f};
  float factor = shade[face];
  return (Color){(unsigned char)(base.r * factor),
                 (unsigned char)(base.g * factor),
                 (unsigned char)(base.b * factor), 255};
}

static void AddFace(MeshBuilder *builder, int x, int y, int z, BlockId id,
                    int face) {
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
  Color color = BlockFaceColor(id, face);

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

static Model BuildWorldModel(void) {
  static const int neighbor[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  MeshBuilder builder = {0};

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
            AddFace(&builder, x, y, z, id, face);
          }
        }
      }
    }
  }

  Mesh mesh = {0};
  mesh.vertexCount = builder.vertexCount;
  mesh.triangleCount = builder.vertexCount / 3;
  mesh.vertices = builder.vertices;
  mesh.normals = builder.normals;
  mesh.texcoords = builder.texcoords;
  mesh.colors = builder.colors;
  UploadMesh(&mesh, false);

  Model newModel = LoadModelFromMesh(mesh);
  if (worldModelLoaded)
    UnloadModel(worldModel);
  worldModelLoaded = true;
  return newModel;
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

static void UpdatePlayer(Player *player, float deltaTime, bool applyMouseLook) {
  Vector2 mouse = GetMouseDelta();
  if (applyMouseLook) {
    player->yaw += mouse.x * MOUSE_SENSITIVITY;
    player->pitch -= mouse.y * MOUSE_SENSITIVITY;
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

int main(void) {
  SetConfigFlags(FLAG_MSAA_4X_HINT);
  InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Veyloria proto-26.10.09-01");
  SetTargetFPS(60);
  DisableCursor();

  InitializeWorld();
  worldModel = BuildWorldModel();

  Player player = {
      .feet = {WORLD_WIDTH * 0.5f, TERRAIN_LAYERS, WORLD_DEPTH * 0.5f},
      .yaw = 0.0f,
      .pitch = 0.0f,
      .verticalVelocity = 0.0f,
      .grounded = false};
  int inventory[2] = {32, 32};
  int selectedSlot = 0;
  bool debugMode = false;
  bool mouseCaptured = true;
  const BlockId hotbarBlocks[2] = {BLOCK_GRASS, BLOCK_STONE};

  while (!WindowShouldClose()) {
    float deltaTime = GetFrameTime();
    if (deltaTime > 0.05f)
      deltaTime = 0.05f;

    bool windowFocused = IsWindowFocused();
    bool justRegainedFocus = windowFocused && !mouseCaptured;
    if (windowFocused != mouseCaptured) {
      if (windowFocused)
        DisableCursor();
      else
        EnableCursor();
      mouseCaptured = windowFocused;
    }

    if (IsKeyPressed(KEY_ONE))
      selectedSlot = 0;
    if (IsKeyPressed(KEY_TWO))
      selectedSlot = 1;
    if (IsKeyPressed(KEY_F1))
      debugMode = !debugMode;

    UpdatePlayer(&player, deltaTime, windowFocused && !justRegainedFocus);

    Vector3 eye = PlayerEye(&player);
    RayHit target = CastBlockRay(eye, LookDirection(&player));
    bool changedWorld = false;
    if (target.hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      BlockId broken = GetBlock(target.x, target.y, target.z);
      int slot = (broken == BLOCK_GRASS) ? 0 : (broken == BLOCK_STONE) ? 1 : -1;
      if (slot >= 0 && inventory[slot] < INVENTORY_CAPACITY) {
        inventory[slot]++;
      }
      SetBlock(target.x, target.y, target.z, BLOCK_AIR);
      changedWorld = true;
    } else if (target.hit && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
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
      worldModel = BuildWorldModel();

    Vector3 look = LookDirection(&player);
    Camera3D camera = {
        .position = eye,
        .target = {eye.x + look.x, eye.y + look.y, eye.z + look.z},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 75.0f,
        .projection = CAMERA_PERSPECTIVE};

    BeginDrawing();
    ClearBackground((Color){135, 190, 235, 255});
    BeginMode3D(camera);
    DrawModel(worldModel, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
    if (target.hit) {
      DrawCubeWires(
          (Vector3){target.x + 0.5f, target.y + 0.5f, target.z + 0.5f}, 1.01f,
          1.01f, 1.01f, RAYWHITE);
    }
    EndMode3D();

    int centerX = GetScreenWidth() / 2;
    int centerY = GetScreenHeight() / 2;
    DrawLine(centerX - 7, centerY, centerX + 7, centerY, RAYWHITE);
    DrawLine(centerX, centerY - 7, centerX, centerY + 7, RAYWHITE);
    DrawHotbar(selectedSlot, inventory);
    if (debugMode)
      DrawDebug(&player, hotbarBlocks[selectedSlot], inventory, target);
    EndDrawing();
  }

  if (worldModelLoaded)
    UnloadModel(worldModel);
  CloseWindow();
  return 0;
}
