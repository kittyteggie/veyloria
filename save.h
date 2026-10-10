#ifndef VEYLORIA_SAVE_H
#define VEYLORIA_SAVE_H

#include <stdbool.h>

#include "blocks.h"
#include "player.h"
#include "world.h"

#define SAVE_VERSION 3u
#define SAVE_MAX_SLOTS 64

// SUMMARY: Versioned world persistence: header, player state, hotbar slots, compressed chunk payload.

bool SaveGame(const char *path, const char *tempPath, const World *world,
              const Player *player, const ItemStack *slots, int slotCount,
              int selectedSlot);
bool LoadGame(const char *path, World *world, Player *player, ItemStack *slots,
              int slotCount, int *selectedSlot);

#endif
