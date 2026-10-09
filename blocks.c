#include "blocks.h"

// SUMMARY: Defines Veyloria's built-in air, grass, and stone block properties.

const BlockDef blockDefs[BLOCK_COUNT] = {
    [BLOCK_AIR] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":air",
        .solid = false,
        .hardness = 0.0f
    },
    [BLOCK_GRASS] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":grass",
        .solid = true,
        .hardness = 0.6f
    },
    [BLOCK_STONE] = {
        .name = VEYLORIA_BLOCK_NAMESPACE ":stone",
        .solid = true,
        .hardness = 1.5f
    }
};
