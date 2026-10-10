#include "blocks.h"

// SUMMARY: Defines Veyloria's built-in air, grass, and stone block properties.

const BlockDef blockDefs[BLOCK_COUNT] = {
    [BLOCK_AIR] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":air",
        .solid = false
    },
    [BLOCK_GRASS] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":grass",
        .solid = true
    },
    [BLOCK_STONE] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":stone",
        .solid = true
    }
};
