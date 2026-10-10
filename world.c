#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SUMMARY: Implements sparse chunk allocation, block access, dirty tracking, and chunk serialization.

static void WriteU16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

static uint16_t ReadU16(const uint8_t *in) {
  return (uint16_t)in[0] | ((uint16_t)in[1] << 8);
}

static bool ChunkHasBlocks(const Chunk *chunk) {
  return chunk != NULL && chunk->maxLocalY >= 0;
}

void WorldInit(World *world) { memset(world, 0, sizeof(*world)); }

void WorldFree(World *world) {
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++) {
        free(world->chunks[cx][cy][cz]);
        world->chunks[cx][cy][cz] = NULL;
      }
    }
  }
}

bool WorldInBounds(int x, int y, int z) {
  return x >= 0 && x < WORLD_WIDTH && y >= 0 && y < WORLD_HEIGHT && z >= 0 &&
         z < WORLD_DEPTH;
}

BlockId WorldGetBlock(const World *world, int x, int y, int z) {
  if (!WorldInBounds(x, y, z))
    return BLOCK_AIR;

  const Chunk *chunk = world->chunks[x / CHUNK_SIZE][y / CHUNK_SIZE][z / CHUNK_SIZE];
  if (chunk == NULL)
    return BLOCK_AIR;
  return chunk->blocks[x % CHUNK_SIZE][y % CHUNK_SIZE][z % CHUNK_SIZE];
}

Chunk *WorldEnsureChunk(World *world, int cx, int cy, int cz) {
  if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y || cz < 0 ||
      cz >= CHUNKS_Z)
    return NULL;

  if (world->chunks[cx][cy][cz] == NULL) {
    Chunk *chunk = calloc(1, sizeof(Chunk));
    if (chunk == NULL) {
      fprintf(stderr, "veyloria: failed to allocate chunk %d, %d, %d\n", cx, cy,
              cz);
      return NULL;
    }
    chunk->maxLocalY = -1;
    chunk->dirty = true;
    world->chunks[cx][cy][cz] = chunk;
  }
  return world->chunks[cx][cy][cz];
}

Chunk *WorldChunkAt(World *world, int cx, int cy, int cz) {
  if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y || cz < 0 ||
      cz >= CHUNKS_Z)
    return NULL;
  return world->chunks[cx][cy][cz];
}

const Chunk *WorldChunkAtConst(const World *world, int cx, int cy, int cz) {
  if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y || cz < 0 ||
      cz >= CHUNKS_Z)
    return NULL;
  return world->chunks[cx][cy][cz];
}

int WorldCountChunks(const World *world) {
  int count = 0;
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++) {
        if (world->chunks[cx][cy][cz] != NULL)
          count++;
      }
    }
  }
  return count;
}

bool WorldChunkDirty(const World *world, int cx, int cy, int cz) {
  const Chunk *chunk = WorldChunkAtConst(world, cx, cy, cz);
  return chunk != NULL && chunk->dirty;
}

void WorldMarkChunkDirty(World *world, int cx, int cy, int cz) {
  Chunk *chunk = WorldChunkAt(world, cx, cy, cz);
  if (chunk != NULL)
    chunk->dirty = true;
}

void WorldMarkChunkClean(World *world, int cx, int cy, int cz) {
  Chunk *chunk = WorldChunkAt(world, cx, cy, cz);
  if (chunk != NULL)
    chunk->dirty = false;
}

void WorldMarkAllChunksDirty(World *world) {
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++) {
        if (world->chunks[cx][cy][cz] != NULL)
          world->chunks[cx][cy][cz]->dirty = true;
      }
    }
  }
}

static void ChunkUpdateMaxLocalY(Chunk *chunk) {
  chunk->maxLocalY = -1;
  for (int y = CHUNK_SIZE - 1; y >= 0 && chunk->maxLocalY < 0; y--) {
    for (int x = 0; x < CHUNK_SIZE; x++) {
      for (int z = 0; z < CHUNK_SIZE; z++) {
        if (chunk->blocks[x][y][z] != BLOCK_AIR) {
          chunk->maxLocalY = y;
          break;
        }
      }
    }
  }
}

void WorldSetBlock(World *world, int x, int y, int z, BlockId id) {
  if (!WorldInBounds(x, y, z))
    return;

  int cx = x / CHUNK_SIZE;
  int cy = y / CHUNK_SIZE;
  int cz = z / CHUNK_SIZE;
  Chunk *chunk = world->chunks[cx][cy][cz];
  if (chunk == NULL) {
    if (id == BLOCK_AIR)
      return;
    chunk = WorldEnsureChunk(world, cx, cy, cz);
    if (chunk == NULL)
      return;
  }

  int lx = x % CHUNK_SIZE;
  int ly = y % CHUNK_SIZE;
  int lz = z % CHUNK_SIZE;
  if (chunk->blocks[lx][ly][lz] == id)
    return;

  chunk->blocks[lx][ly][lz] = id;
  chunk->dirty = true;
  if (id != BLOCK_AIR && ly > chunk->maxLocalY) {
    chunk->maxLocalY = ly;
  } else if (id == BLOCK_AIR && ly == chunk->maxLocalY) {
    ChunkUpdateMaxLocalY(chunk);
  }

  // Faces of a bordering chunk are culled against this block, so it must be remeshed too.
  if (lx == 0)
    WorldMarkChunkDirty(world, cx - 1, cy, cz);
  if (lx == CHUNK_SIZE - 1)
    WorldMarkChunkDirty(world, cx + 1, cy, cz);
  if (ly == 0)
    WorldMarkChunkDirty(world, cx, cy - 1, cz);
  if (ly == CHUNK_SIZE - 1)
    WorldMarkChunkDirty(world, cx, cy + 1, cz);
  if (lz == 0)
    WorldMarkChunkDirty(world, cx, cy, cz - 1);
  if (lz == CHUNK_SIZE - 1)
    WorldMarkChunkDirty(world, cx, cy, cz + 1);
}

uint8_t *WorldSerializeBlocks(const World *world, size_t *outSize) {
  int chunkCount = 0;
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++) {
        if (ChunkHasBlocks(world->chunks[cx][cy][cz]))
          chunkCount++;
      }
    }
  }

  size_t size = (size_t)chunkCount * CHUNK_RECORD_SIZE;
  *outSize = size;
  uint8_t *data = malloc(size > 0 ? size : 1);
  if (data == NULL) {
    *outSize = 0;
    return NULL;
  }

  uint8_t *cursor = data;
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++) {
        const Chunk *chunk = world->chunks[cx][cy][cz];
        if (!ChunkHasBlocks(chunk))
          continue;

        WriteU16(cursor + 0, (uint16_t)cx);
        WriteU16(cursor + 2, (uint16_t)cy);
        WriteU16(cursor + 4, (uint16_t)cz);
        WriteU16(cursor + 6, 0);
        memcpy(cursor + CHUNK_RECORD_HEADER, chunk->blocks,
               sizeof(chunk->blocks));
        cursor += CHUNK_RECORD_SIZE;
      }
    }
  }
  return data;
}

bool WorldDeserializeBlocks(World *world, const uint8_t *data, size_t size) {
  if (size % CHUNK_RECORD_SIZE != 0)
    return false;
  if (size > 0 && data == NULL)
    return false;

  size_t chunkCount = size / CHUNK_RECORD_SIZE;
  for (size_t i = 0; i < chunkCount; i++) {
    const uint8_t *record = data + i * CHUNK_RECORD_SIZE;
    if (ReadU16(record + 0) >= CHUNKS_X || ReadU16(record + 2) >= CHUNKS_Y ||
        ReadU16(record + 4) >= CHUNKS_Z)
      return false;

    const uint8_t *blocks = record + CHUNK_RECORD_HEADER;
    for (int j = 0; j < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; j++) {
      if (blocks[j] >= BLOCK_COUNT)
        return false;
    }
  }

  WorldFree(world);
  for (size_t i = 0; i < chunkCount; i++) {
    const uint8_t *record = data + i * CHUNK_RECORD_SIZE;
    int cx = ReadU16(record + 0);
    int cy = ReadU16(record + 2);
    int cz = ReadU16(record + 4);
    Chunk *chunk = WorldEnsureChunk(world, cx, cy, cz);
    if (chunk == NULL)
      return false;

    memcpy(chunk->blocks, record + CHUNK_RECORD_HEADER, sizeof(chunk->blocks));
    ChunkUpdateMaxLocalY(chunk);
    chunk->dirty = true;
  }
  return true;
}
