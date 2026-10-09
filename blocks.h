#ifndef VEYLORIA_BLOCKS_H
#define VEYLORIA_BLOCKS_H

#include <stdbool.h>
#include <stdint.h>

#define VEYLORIA_BLOCK_NAMESPACE "veyloria"

// SUMMARY: Declares stable block IDs and the namespaced block-definition interface.

typedef uint16_t BlockId;

typedef enum BlockType {
    BLOCK_AIR = 0,
    BLOCK_GRASS = 1,
    BLOCK_STONE = 2,
    BLOCK_COUNT = 3
} BlockType;

typedef struct BlockDef {
    const char *name;
    bool solid;
    float hardness;
} BlockDef;

extern const BlockDef blockDefs[BLOCK_COUNT];

#endif
