#include "ui.h"

#include "version.h"

#include <stdio.h>

// SUMMARY: Renders the main menu, config menu, hotbar, and debug overlay.

#define MENU_LOGO_WIDTH 384
#define MENU_LOGO_HEIGHT 192

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

void UiDrawMainMenu(Texture2D background, bool hasBackground, Texture2D logo,
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

void UiDrawConfigMenu(Texture2D background, bool hasBackground, Texture2D logo,
                      bool hasLogo, const GameSettings *settings) {
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
    DrawText(SettingsFieldName((ConfigField)row), centerX - 225, (int)rowY + 8,
             20, RAYWHITE);
    DrawMenuButton(ConfigArrowBounds(row, false), "<");
    DrawMenuButton(ConfigArrowBounds(row, true), ">");
    SettingsFieldText(settings, (ConfigField)row, value, sizeof(value));
    DrawText(value, centerX + 66, (int)rowY + 10, 18, RAYWHITE);
  }

  DrawMenuButton(ConfigBackBounds(), "BACK");
}

static void DrawBlockIcon(Rectangle slot, BlockId id) {
  if (id == BLOCK_AIR)
    return;

  const BlockDef *def = &blockDefs[id];
  const BlockColor *colorTop = &def->color[BLOCK_FACE_TOP];
  const BlockColor *colorSide = &def->color[BLOCK_FACE_SIDE];
  Color top = {colorTop->red, colorTop->green, colorTop->blue, 255};
  Color side = {colorSide->red, colorSide->green, colorSide->blue, 255};
  int size = 24;
  int left = (int)(slot.x + (slot.width - size) * 0.5f);
  int topY = (int)(slot.y + (slot.height - size) * 0.5f) - 4;
  DrawRectangle(left, topY, size, size, top);
  DrawRectangle(left, topY + size - 6, size, 6, side);
}

// Draws the crosshair with a mining progress bar underneath while breaking.
void UiDrawCrosshair(float miningProgress) {
  int centerX = GetScreenWidth() / 2;
  int centerY = GetScreenHeight() / 2;
  DrawLine(centerX - 7, centerY, centerX + 7, centerY, RAYWHITE);
  DrawLine(centerX, centerY - 7, centerX, centerY + 7, RAYWHITE);

  if (miningProgress > 0.0f) {
    int width = 52;
    int height = 6;
    int x = centerX - width / 2;
    int y = centerY + 16;
    if (miningProgress > 1.0f)
      miningProgress = 1.0f;
    DrawRectangle(x - 1, y - 1, width + 2, height + 2, Fade(BLACK, 0.55f));
    DrawRectangle(x, y, (int)((float)width * miningProgress), height, RAYWHITE);
  }
}

void UiDrawHotbar(int selectedSlot, const ItemStack slots[]) {
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

void UiDrawDebug(const World *world, const Player *player,
                 const ItemStack slots[], int selectedSlot, RayHit hit,
                 int chunksDrawn, int chunksTotal) {
  Vector3 eye = PlayerEye(player);
  char lines[12][128];
  int lineCount = 0;

  snprintf(lines[lineCount++], sizeof(lines[0]), "Version: %s",
           VEYLORIA_VERSION);
  snprintf(lines[lineCount++], sizeof(lines[0]), "Block in hand: %s",
           blockDefs[slots[selectedSlot].id].name);
  snprintf(lines[lineCount++], sizeof(lines[0]), "Position: %.2f, %.2f, %.2f",
           player->feet.x, player->feet.y, player->feet.z);
  snprintf(lines[lineCount++], sizeof(lines[0]), "Eye: %.2f, %.2f, %.2f", eye.x,
           eye.y, eye.z);

  // The hotbar wraps so every slot fits in the panel.
  for (int start = 0; start < HOTBAR_SIZE; start += 3) {
    char *text = lines[lineCount];
    int length = snprintf(text, sizeof(lines[0]), "Hotbar:");
    for (int i = start; i < HOTBAR_SIZE && i < start + 3; i++) {
      if (length <= 0 || (size_t)length >= sizeof(lines[0]))
        break;
      length += snprintf(text + length, sizeof(lines[0]) - (size_t)length,
                         " %s %d%s", BlockShortName(slots[i].id), slots[i].count,
                         (i + 1 < HOTBAR_SIZE && i + 1 < start + 3) ? " |" : "");
    }
    lineCount++;
  }

  if (hit.hit) {
    snprintf(lines[lineCount++], sizeof(lines[0]),
             "Target: %s at %d, %d, %d (%.1fm)",
             blockDefs[WorldGetBlock(world, hit.x, hit.y, hit.z)].name, hit.x,
             hit.y, hit.z, hit.distance);
  } else {
    snprintf(lines[lineCount++], sizeof(lines[0]), "Target: none");
  }
  snprintf(lines[lineCount++], sizeof(lines[0]), "Chunks: %d drawn / %d loaded",
           chunksDrawn, chunksTotal);

  DrawRectangle(12, 12, 380, 34 + lineCount * 21, (Color){0, 0, 0, 170});
  DrawText("VEYLORIA DEBUG", 22, 20, 18, RAYWHITE);
  for (int i = 0; i < lineCount; i++)
    DrawText(lines[i], 22, 47 + i * 21, 16, RAYWHITE);
}

bool UiMainMenuButtonClicked(int index) {
  return MenuButtonClicked(MainMenuButtonBounds(index));
}

bool UiConfigBackClicked(void) { return MenuButtonClicked(ConfigBackBounds()); }

bool UiConfigArrowClicked(int row, bool rightArrow) {
  return MenuButtonClicked(ConfigArrowBounds(row, rightArrow));
}
