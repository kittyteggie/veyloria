#include "blocks.h"

#include <string.h>

// SUMMARY: Defines the built-in block properties and their per-face textures.

const BlockDef blockDefs[BLOCK_COUNT] = {
    [BLOCK_AIR] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":air",
        .solid = false,
        .hardness = 0.0f,
        .color = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
        .texture = {NULL, NULL, NULL}
    },
    [BLOCK_GRASS] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":grass",
        .solid = true,
        .hardness = 0.6f,
        .color = {{98, 160, 68}, {134, 96, 67}, {134, 96, 67}},
        .texture = {"grass_top", "grass_side", "dirt"}
    },
    [BLOCK_DIRT] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":dirt",
        .solid = true,
        .hardness = 0.5f,
        .color = {{134, 96, 67}, {134, 96, 67}, {134, 96, 67}},
        .texture = {NULL, NULL, NULL}
    },
    [BLOCK_STONE] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":stone",
        .solid = true,
        .hardness = 1.5f,
        .color = {{132, 136, 143}, {132, 136, 143}, {132, 136, 143}},
        .texture = {NULL, NULL, NULL}
    },
    [BLOCK_WOOD] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":wood",
        .solid = true,
        .hardness = 2.0f,
        .color = {{122, 94, 58}, {104, 78, 46}, {122, 94, 58}},
        .texture = {"wood_top", "wood_side", "wood_top"}
    },
    [BLOCK_LEAVES] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":leaves",
        .solid = true,
        .hardness = 0.2f,
        .color = {{72, 138, 50}, {60, 122, 42}, {60, 122, 42}},
        .texture = {NULL, NULL, NULL}
    }
};

const char *BlockShortName(BlockId id) {
    const char *name = blockDefs[id].name;
    const char *separator = strrchr(name, ':');
    return separator ? separator + 1 : name;
}

BlockFace BlockFaceOf(int face) {
    if (face == 2)
        return BLOCK_FACE_TOP;
    if (face == 3)
        return BLOCK_FACE_BOTTOM;
    return BLOCK_FACE_SIDE;
}

const char *BlockTextureName(BlockId id, BlockFace face) {
    const BlockDef *def = &blockDefs[id];
    const char *texture = def->texture[face];
    if (texture == NULL && face == BLOCK_FACE_BOTTOM)
        texture = def->texture[BLOCK_FACE_SIDE];
    return texture != NULL ? texture : BlockShortName(id);
}

void ItemStackGive(ItemStack slots[], int slotCount, BlockId id) {
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
