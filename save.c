#include "save.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

// SUMMARY: Reads and writes the versioned save file with a compressed sparse chunk payload.

static const uint8_t SAVE_MAGIC[8] = {'V', 'E', 'Y', 'L', 'O', 'R', 'I', 'A'};

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

static bool PlayerValid(const Player *player) {
  return isfinite(player->feet.x) && isfinite(player->feet.y) &&
         isfinite(player->feet.z) && isfinite(player->yaw) &&
         isfinite(player->pitch) && player->feet.x >= PLAYER_RADIUS &&
         player->feet.x <= WORLD_WIDTH - PLAYER_RADIUS &&
         player->feet.y >= 0.0f && player->feet.y <= WORLD_HEIGHT - PLAYER_HEIGHT &&
         player->feet.z >= PLAYER_RADIUS &&
         player->feet.z <= WORLD_DEPTH - PLAYER_RADIUS &&
         player->pitch >= -MAX_PITCH && player->pitch <= MAX_PITCH;
}

static bool SlotValid(uint32_t id, int count) {
  return id < BLOCK_COUNT && count >= 0 && count <= INVENTORY_CAPACITY;
}

bool SaveGame(const char *path, const char *tempPath, const World *world,
              const Player *player, const ItemStack *slots, int slotCount,
              int selectedSlot) {
  if (slotCount < 0 || slotCount > SAVE_MAX_SLOTS ||
      selectedSlot < 0 || selectedSlot >= slotCount)
    return false;

  size_t payloadSize = 0;
  uint8_t *payload = WorldSerializeBlocks(world, &payloadSize);
  if (payload == NULL)
    return false;

  if (payloadSize % CHUNK_RECORD_SIZE != 0) {
    free(payload);
    return false;
  }
  int chunkCount = (int)(payloadSize / CHUNK_RECORD_SIZE);

  int compressedSize = 0;
  unsigned char *compressed = NULL;
  if (payloadSize > 0) {
    compressed = CompressData(payload, (int)payloadSize, &compressedSize);
    if (compressed == NULL || compressedSize <= 0) {
      if (compressed != NULL)
        MemFree(compressed);
      free(payload);
      return false;
    }
  }
  free(payload);

  FILE *file = fopen(tempPath, "wb");
  if (file == NULL) {
    if (compressed != NULL)
      MemFree(compressed);
    return false;
  }

  bool success =
      fwrite(SAVE_MAGIC, 1, sizeof(SAVE_MAGIC), file) == sizeof(SAVE_MAGIC) &&
      WriteU32(file, SAVE_VERSION) && WriteU32(file, WORLD_WIDTH) &&
      WriteU32(file, WORLD_DEPTH) && WriteU32(file, WORLD_HEIGHT) &&
      WriteFloat(file, player->feet.x) && WriteFloat(file, player->feet.y) &&
      WriteFloat(file, player->feet.z) && WriteFloat(file, player->yaw) &&
      WriteFloat(file, player->pitch) && WriteU32(file, (uint32_t)slotCount);

  for (int i = 0; success && i < slotCount; i++) {
    success = WriteU32(file, (uint32_t)slots[i].id) &&
              WriteU32(file, (uint32_t)slots[i].count);
  }

  success = success && WriteU32(file, (uint32_t)selectedSlot) &&
            WriteU32(file, (uint32_t)chunkCount) &&
            WriteU32(file, (uint32_t)compressedSize);
  if (success && compressedSize > 0) {
    success = fwrite(compressed, 1, (size_t)compressedSize, file) ==
              (size_t)compressedSize;
  }

  if (compressed != NULL)
    MemFree(compressed);
  if (fclose(file) != 0)
    success = false;

  if (!success || rename(tempPath, path) != 0) {
    remove(tempPath);
    return false;
  }
  return true;
}

bool LoadGame(const char *path, World *world, Player *player, ItemStack *slots,
              int slotCount, int *selectedSlot) {
  if (slotCount <= 0 || slotCount > SAVE_MAX_SLOTS)
    return false;

  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return false;

  uint8_t magic[8];
  uint32_t version, width, depth, height;
  Player savedPlayer = {0};
  uint32_t savedSlotCount = 0;
  bool valid =
      fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
      memcmp(magic, SAVE_MAGIC, sizeof(magic)) == 0 &&
      ReadU32(file, &version) && ReadU32(file, &width) &&
      ReadU32(file, &depth) && ReadU32(file, &height) &&
      ReadFloat(file, &savedPlayer.feet.x) &&
      ReadFloat(file, &savedPlayer.feet.y) &&
      ReadFloat(file, &savedPlayer.feet.z) && ReadFloat(file, &savedPlayer.yaw) &&
      ReadFloat(file, &savedPlayer.pitch) && ReadU32(file, &savedSlotCount);

  valid = valid && version == SAVE_VERSION && width == WORLD_WIDTH &&
          depth == WORLD_DEPTH && height == WORLD_HEIGHT &&
          PlayerValid(&savedPlayer) && savedSlotCount >= 1 &&
          savedSlotCount <= SAVE_MAX_SLOTS;

  ItemStack fileSlots[SAVE_MAX_SLOTS];
  memset(fileSlots, 0, sizeof(fileSlots));
  for (uint32_t i = 0; valid && i < savedSlotCount; i++) {
    uint32_t id = 0;
    uint32_t count = 0;
    valid = ReadU32(file, &id) && ReadU32(file, &count) &&
            SlotValid(id, (int)count);
    if (valid) {
      fileSlots[i].id = (BlockId)id;
      fileSlots[i].count = (int)count;
    }
  }

  uint32_t savedSelectedSlot = 0;
  uint32_t chunkCount = 0;
  uint32_t compressedSize = 0;
  valid = valid && ReadU32(file, &savedSelectedSlot) &&
          ReadU32(file, &chunkCount) && ReadU32(file, &compressedSize) &&
          savedSelectedSlot < savedSlotCount;

  uint64_t expectedPayload = (uint64_t)chunkCount * CHUNK_RECORD_SIZE;
  unsigned char *compressed = NULL;
  if (valid && chunkCount > 0) {
    valid = compressedSize > 0 &&
            (uint64_t)compressedSize <= expectedPayload * 2 + 64u;
    if (valid) {
      compressed = malloc(compressedSize);
      valid = compressed != NULL;
    }
    if (valid) {
      valid = fread(compressed, 1, compressedSize, file) == compressedSize;
    }
  } else if (valid) {
    valid = compressedSize == 0;
  }

  if (valid && (fgetc(file) != EOF || ferror(file)))
    valid = false;
  if (fclose(file) != 0)
    valid = false;

  uint8_t *payload = NULL;
  if (valid && chunkCount > 0) {
    int decompressedSize = 0;
    unsigned char *decompressed =
        DecompressData(compressed, (int)compressedSize, &decompressedSize);
    valid = decompressed != NULL && decompressedSize >= 0 &&
            (uint64_t)decompressedSize == expectedPayload;
    if (valid) {
      payload = malloc((size_t)decompressedSize);
      if (payload != NULL)
        memcpy(payload, decompressed, (size_t)decompressedSize);
      else
        valid = false;
    }
    if (decompressed != NULL)
      MemFree(decompressed);
  }
  free(compressed);

  if (valid && !WorldDeserializeBlocks(world, payload, (size_t)expectedPayload))
    valid = false;
  free(payload);

  if (!valid)
    return false;

  savedPlayer.verticalVelocity = 0.0f;
  savedPlayer.grounded = false;
  *player = savedPlayer;

  for (int i = 0; i < slotCount; i++) {
    slots[i] = (i < (int)savedSlotCount) ? fileSlots[i] : (ItemStack){BLOCK_AIR, 0};
  }
  *selectedSlot =
      (int)((savedSelectedSlot < (uint32_t)slotCount) ? savedSelectedSlot
                                                     : (uint32_t)slotCount - 1);
  return true;
}
