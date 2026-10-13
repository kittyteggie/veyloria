#ifndef VEYLORIA_UI_H
#define VEYLORIA_UI_H

#include <stdbool.h>

#include "blocks.h"
#include "player.h"
#include "raylib.h"
#include "settings.h"
#include "world.h"

// SUMMARY: Draws the menus, hotbar, and debug overlay, and reports UI clicks.

void UiDrawMainMenu(Texture2D background, bool hasBackground, Texture2D logo,
                    bool hasLogo);
void UiDrawConfigMenu(Texture2D background, bool hasBackground, Texture2D logo,
                      bool hasLogo, const GameSettings *settings);
void UiDrawCrosshair(float miningProgress);
void UiDrawHotbar(int selectedSlot, const ItemStack slots[]);
void UiDrawDebug(const World *world, const Player *player,
                 const ItemStack slots[], int selectedSlot, RayHit hit,
                 int chunksDrawn, int chunksTotal);
bool UiMainMenuButtonClicked(int index);
bool UiConfigBackClicked(void);
bool UiConfigArrowClicked(int row, bool rightArrow);

#endif
