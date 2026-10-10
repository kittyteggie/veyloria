#ifndef VEYLORIA_WORLD_H
#define VEYLORIA_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "blocks.h"

#define WORLD_WIDTH 256
#define WORLD_DEPTH 256
#define WORLD_HEIGHT 256
#define CHUNK_SIZE 16
#define CHUNKS_X (WORLD_WIDTH / CHUNK_SIZE)
#define CHUNKS_Y (WORLD_HEIGHT / CHUNK_SIZE)
#define CHUNKS_Z (WORLD_DEPTH / CHUNK_SIZE)

// SUMMARY: Sparse chunked voxel storage with dirty tracking and block payload serialization.

typedef struct Chunk {
  BlockId blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
  int maxLocalY;
  bool dirty;
} Chunk;

typedef struct World {
  Chunk *chunks[CHUNKS_X][CHUNKS_Y][CHUNKS_Z];
} World;

void WorldInit(World *world);
void WorldFree(World *world);
bool WorldInBounds(int x, int y, int z);
BlockId WorldGetBlock(const World *world, int x, int y, int z);
void WorldSetBlock(World *world, int x, int y, int z, BlockId id);

Chunk *WorldEnsureChunk(World *world, int cx, int cy, int cz);
Chunk *WorldChunkAt(World *world, int cx, int cy, int cz);
const Chunk *WorldChunkAtConst(const World *world, int cx, int cy, int cz);
int WorldCountChunks(const World *world);

bool WorldChunkDirty(const World *world, int cx, int cy, int cz);
void WorldMarkChunkDirty(World *world, int cx, int cy, int cz);
void WorldMarkChunkClean(World *world, int cx, int cy, int cz);
void WorldMarkAllChunksDirty(World *world);

#define CHUNK_RECORD_HEADER 8
#define CHUNK_RECORD_SIZE \
  (CHUNK_RECORD_HEADER + CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE)

uint8_t *WorldSerializeBlocks(const World *world, size_t *outSize);
bool WorldDeserializeBlocks(World *world, const uint8_t *data, size_t size);

#endif
