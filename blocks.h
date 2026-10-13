#ifndef VEYLORIA_BLOCKS_H
#define VEYLORIA_BLOCKS_H

#include <stdbool.h>
#include <stdint.h>

#define VEYLORIA_BLOCK_NAMESPACE "veyloria"
#define INVENTORY_CAPACITY 64

// SUMMARY: Declares stable block IDs, per-face block definitions, and item helpers.

typedef uint8_t BlockId;

typedef enum BlockType {
    BLOCK_AIR = 0,
    BLOCK_GRASS = 1,
    BLOCK_DIRT = 2,
    BLOCK_STONE = 3,
    BLOCK_WOOD = 4,
    BLOCK_LEAVES = 5,
    BLOCK_COUNT = 6
} BlockType;

// One hotbar slot per placeable block type.
#define HOTBAR_SIZE (BLOCK_COUNT - 1)

// Texture and colour slots: the four lateral faces share BLOCK_FACE_SIDE.
typedef enum BlockFace {
    BLOCK_FACE_TOP = 0,
    BLOCK_FACE_SIDE = 1,
    BLOCK_FACE_BOTTOM = 2,
    BLOCK_FACE_COUNT = 3
} BlockFace;

typedef struct BlockColor {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} BlockColor;

typedef struct BlockDef {
    const char *name;
    bool solid;
    float hardness;
    BlockColor color[BLOCK_FACE_COUNT];
    // NULL means "use the block's base texture" (assets/<name>.png).
    const char *texture[BLOCK_FACE_COUNT];
} BlockDef;

typedef struct ItemStack {
    BlockId id;
    int count;
} ItemStack;

extern const BlockDef blockDefs[BLOCK_COUNT];

const char *BlockShortName(BlockId id);
BlockFace BlockFaceOf(int face);
const char *BlockTextureName(BlockId id, BlockFace face);
void ItemStackGive(ItemStack slots[], int slotCount, BlockId id);

#endif
