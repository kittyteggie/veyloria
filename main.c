#include "audio.h"
#include "blocks.h"
#include "mesher.h"
#include "player.h"
#include "save.h"
#include "settings.h"
#include "ui.h"
#include "version.h"
#include "world.h"
#include "worldgen.h"

#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MINING_TIME_SCALE 1.5f
#define SAVE_FILE "veyloria-world.sav"
#define SAVE_TEMP_FILE "veyloria-world.sav.tmp"
#define SAVE_BACKUP_FILE "veyloria-world.sav.bak"
#define MENU_BACKGROUND_PATH "assets/menubg.png"
#define MENU_LOGO_PATH "assets/logo.png"

// SUMMARY: Runs the voxel game loop and wires the world, renderer, and UI together.

typedef enum ScreenState {
  SCREEN_MAIN_MENU,
  SCREEN_CONFIG,
  SCREEN_PLAYING
} ScreenState;

static World world;
static BlockTextures blockTextures;
static GameAudio audio;

static bool InWorld(int x, int y, int z) { return WorldInBounds(x, y, z); }

static BlockId GetBlock(int x, int y, int z) {
  return WorldGetBlock(&world, x, y, z);
}

static void SetBlock(int x, int y, int z, BlockId id) {
  WorldSetBlock(&world, x, y, z, id);
}

// Finds the ground under a column, ignoring leaves so trees are not spawn points.
static float FindSurfaceY(int x, int z) {
  for (int y = WORLD_HEIGHT - 1; y >= 0; y--) {
    BlockId id = GetBlock(x, y, z);
    if (id != BLOCK_AIR && id != BLOCK_LEAVES)
      return (float)y + 1.0f;
  }
  return 0.0f;
}

static bool ResolveAssetPath(const char *relativePath, char *resolved,
                             size_t size) {
  if (FileExists(relativePath)) {
    snprintf(resolved, size, "%s", relativePath);
    return true;
  }

  const char *applicationDirectory = GetApplicationDirectory();
  char baseDirectory[512];
  size_t directoryLength = strlen(applicationDirectory);
  snprintf(baseDirectory, sizeof(baseDirectory), "%s%s", applicationDirectory,
           directoryLength > 0 && applicationDirectory[directoryLength - 1] == '/'
               ? ""
               : "/");

  snprintf(resolved, size, "%s%s", baseDirectory, relativePath);
  if (FileExists(resolved))
    return true;

  snprintf(resolved, size, "%s../%s", baseDirectory, relativePath);
  return FileExists(resolved);
}

static bool LoadAssetTexture(const char *relativePath, Texture2D *texture) {
  char path[1024];
  if (!ResolveAssetPath(relativePath, path, sizeof(path)))
    return false;
  *texture = LoadTexture(path);
  return texture->id > 0;
}

static bool LoadNamedTexture(const char *name, Texture2D *texture) {
  char relativePath[256];
  snprintf(relativePath, sizeof(relativePath), "assets/%s.png", name);

  char path[1024];
  if (!ResolveAssetPath(relativePath, path, sizeof(path)))
    return false;
  *texture = LoadTexture(path);
  return texture->id > 0;
}

// Loads one texture per face slot: the override name first, then the block's
// base texture, and finally no texture at all (the face uses its block colour).
static void LoadBlockTextures(void) {
  for (int id = 1; id < BLOCK_COUNT; id++) {
    const char *baseName = BlockShortName((BlockId)id);
    for (int slot = 0; slot < BLOCK_FACE_COUNT; slot++) {
      const char *name = BlockTextureName((BlockId)id, (BlockFace)slot);
      Texture2D texture = {0};
      bool loaded = LoadNamedTexture(name, &texture);
      if (!loaded && strcmp(name, baseName) != 0)
        loaded = LoadNamedTexture(baseName, &texture);

      blockTextures.images[id][slot] = texture;
      blockTextures.loaded[id][slot] = loaded;
      if (loaded) {
        // Block textures are pixel art; keep them crisp when magnified.
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
      }
    }

    if (!blockTextures.loaded[id][BLOCK_FACE_SIDE]) {
      TraceLog(LOG_INFO, "No texture at assets/%s.png; using block colors.",
               baseName);
    }
  }
}

int main(void) {
  GameSettings settings = SettingsDefaults();
  SettingsLoad(&settings);
  Resolution initialResolution = SettingsResolution(&settings);

  SetConfigFlags(SettingsWindowFlags(&settings));
  InitWindow(initialResolution.width, initialResolution.height,
             "Veyloria " VEYLORIA_VERSION);
  SetExitKey(KEY_NULL);
  SettingsApplyRuntime(&settings);
  EnableCursor();
  AudioInit(&audio);

  Texture2D menuBackground = {0};
  Texture2D menuLogo = {0};
  bool hasMenuBackground =
      LoadAssetTexture(MENU_BACKGROUND_PATH, &menuBackground);
  bool hasMenuLogo = LoadAssetTexture(MENU_LOGO_PATH, &menuLogo);
  LoadBlockTextures();
  MesherInit(&blockTextures);

  WorldInit(&world);
  Player player = {0};
  ItemStack hotbar[HOTBAR_SIZE] = {0};
  for (int i = 0; i < HOTBAR_SIZE; i++)
    hotbar[i] = (ItemStack){(BlockId)(i + 1), INVENTORY_CAPACITY / 2};
  int selectedSlot = 0;
  bool debugMode = false;
  bool mouseCaptured = false;
  bool exitRequested = false;
  float stepTimer = 0.0f;
  RayHit miningTarget = {0};
  float miningProgress = 0.0f;
  bool miningActive = false;
  ScreenState screen = SCREEN_MAIN_MENU;

  bool loadedGame = false;
  if (FileExists(SAVE_FILE)) {
    loadedGame = LoadGame(SAVE_FILE, &world, &player, hotbar, HOTBAR_SIZE,
                          &selectedSlot);
    if (!loadedGame) {
      TraceLog(LOG_WARNING,
               "Could not load %s; starting a fresh world and backing it up.",
               SAVE_FILE);
      if (rename(SAVE_FILE, SAVE_BACKUP_FILE) == 0)
        TraceLog(LOG_INFO, "Previous save moved to %s.", SAVE_BACKUP_FILE);
    }
  }
  if (!loadedGame) {
    WorldGenerate(&world, (uint32_t)time(NULL));
    player.feet = (Vector3){WORLD_WIDTH * 0.5f,
                            FindSurfaceY(WORLD_WIDTH / 2, WORLD_DEPTH / 2),
                            WORLD_DEPTH * 0.5f};
  }
  MesherReload(&world);

  while (!WindowShouldClose() && !exitRequested) {
    float deltaTime = GetFrameTime();
    if (deltaTime > 0.05f)
      deltaTime = 0.05f;

    if (IsKeyPressed(KEY_F11)) {
      settings.fullscreen = !settings.fullscreen;
      SettingsApply(&settings);
    }

    bool justEnteredPlay = false;
    if (screen == SCREEN_MAIN_MENU) {
      if (IsKeyPressed(KEY_ESCAPE)) {
        exitRequested = true;
      } else if (UiMainMenuButtonClicked(0)) {
        screen = SCREEN_PLAYING;
        justEnteredPlay = true;
      } else if (UiMainMenuButtonClicked(1)) {
        screen = SCREEN_CONFIG;
      } else if (UiMainMenuButtonClicked(2)) {
        exitRequested = true;
      }
    } else if (screen == SCREEN_CONFIG) {
      if (IsKeyPressed(KEY_ESCAPE) || UiConfigBackClicked()) {
        screen = SCREEN_MAIN_MENU;
      } else {
        bool settingsChanged = false;
        for (int row = 0; row < CFG_FIELD_COUNT; row++) {
          if (UiConfigArrowClicked(row, false)) {
            SettingsAdjust(&settings, (ConfigField)row, -1);
            settingsChanged = true;
          } else if (UiConfigArrowClicked(row, true)) {
            SettingsAdjust(&settings, (ConfigField)row, 1);
            settingsChanged = true;
          }
        }
        if (settingsChanged)
          SettingsApply(&settings);
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
        // Digit keys are contiguous, so one slot per placeable block type.
        for (int slot = 0; slot < HOTBAR_SIZE; slot++) {
          if (IsKeyPressed(KEY_ONE + slot))
            selectedSlot = slot;
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
          int direction = (wheel > 0.0f) ? -1 : 1;
          selectedSlot = (selectedSlot + direction + HOTBAR_SIZE) % HOTBAR_SIZE;
        }
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
            MesherReload(&world);
            TraceLog(LOG_INFO, "World loaded from %s.", SAVE_FILE);
          } else {
            TraceLog(LOG_WARNING, "Could not load world from %s.", SAVE_FILE);
          }
        }

        UpdatePlayer(&player, &world, deltaTime,
                     windowFocused && !justRegainedFocus,
                     SettingsSensitivity(&settings));

        bool moving = player.grounded &&
                      (IsKeyDown(KEY_W) || IsKeyDown(KEY_S) ||
                       IsKeyDown(KEY_A) || IsKeyDown(KEY_D));
        if (moving) {
          stepTimer -= deltaTime;
          if (stepTimer <= 0.0f) {
            AudioPlayStep(&audio);
            stepTimer = IsKeyDown(KEY_LEFT_SHIFT) ? 0.32f : 0.45f;
          }
        } else {
          stepTimer = 0.15f;
        }

        Vector3 eye = PlayerEye(&player);
        RayHit target = CastBlockRay(&world, eye, LookDirection(&player));
        bool changedWorld = false;

        // Hold the left button to mine; progress resets when the target changes.
        if (!justEnteredPlay && target.hit &&
            IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
          if (!miningActive || miningTarget.x != target.x ||
              miningTarget.y != target.y || miningTarget.z != target.z) {
            miningTarget = target;
            miningProgress = 0.0f;
            miningActive = true;
          }

          BlockId minedBlock = GetBlock(target.x, target.y, target.z);
          float breakTime = blockDefs[minedBlock].hardness * MINING_TIME_SCALE;
          if (breakTime <= 0.0f)
            miningProgress = 1.0f;
          else
            miningProgress += deltaTime / breakTime;

          if (miningProgress >= 1.0f) {
            ItemStackGive(hotbar, HOTBAR_SIZE, minedBlock);
            SetBlock(target.x, target.y, target.z, BLOCK_AIR);
            AudioPlayBreak(&audio);
            changedWorld = true;
            miningProgress = 0.0f;
            miningActive = false;
          }
        } else {
          miningActive = false;
          miningProgress = 0.0f;
        }

        if (!justEnteredPlay && target.hit &&
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
            AudioPlayPlace(&audio);
          }
        }
        if (changedWorld)
          MesherRebuildDirty(&world);
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
      RayHit target = CastBlockRay(&world, eye, look);

      // A vertical sky gradient is drawn before the world; only colour is
      // written at this point, so the 3D depth test stays untouched.
      ClearBackground((Color){175, 210, 235, 255});
      DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                             (Color){105, 165, 230, 255},
                             (Color){175, 210, 235, 255});
      Frustum frustum =
          MesherMakeFrustum(eye, look, camera.up, camera.fovy,
                            (float)GetScreenWidth() / (float)GetScreenHeight());

      BeginMode3D(camera);
      MesherStats stats = MesherDraw(&frustum);
      if (target.hit) {
        DrawCubeWires(
            (Vector3){target.x + 0.5f, target.y + 0.5f, target.z + 0.5f},
            1.01f, 1.01f, 1.01f, RAYWHITE);
      }
      EndMode3D();

      UiDrawCrosshair(miningProgress);
      UiDrawHotbar(selectedSlot, hotbar);
      if (debugMode)
        UiDrawDebug(&world, &player, hotbar, selectedSlot, target,
                    stats.chunksDrawn, stats.chunksTotal);
    } else if (screen == SCREEN_MAIN_MENU) {
      UiDrawMainMenu(menuBackground, hasMenuBackground, menuLogo, hasMenuLogo);
    } else {
      UiDrawConfigMenu(menuBackground, hasMenuBackground, menuLogo, hasMenuLogo,
                       &settings);
    }
    EndDrawing();
  }

  if (!SaveGame(SAVE_FILE, SAVE_TEMP_FILE, &world, &player, hotbar,
                HOTBAR_SIZE, selectedSlot))
    TraceLog(LOG_WARNING, "Could not save world to %s on exit.", SAVE_FILE);

  MesherShutdown();
  WorldFree(&world);
  for (int id = 1; id < BLOCK_COUNT; id++) {
    for (int slot = 0; slot < BLOCK_FACE_COUNT; slot++) {
      if (blockTextures.loaded[id][slot])
        UnloadTexture(blockTextures.images[id][slot]);
    }
  }
  if (hasMenuBackground)
    UnloadTexture(menuBackground);
  if (hasMenuLogo)
    UnloadTexture(menuLogo);
  AudioShutdown(&audio);
  CloseWindow();
  return 0;
}
