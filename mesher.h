#ifndef VEYLORIA_MESHER_H
#define VEYLORIA_MESHER_H

#include "blocks.h"
#include "raylib.h"
#include "world.h"

// SUMMARY: Builds per-chunk meshes, culls them against the camera frustum, and draws them.

typedef struct Frustum {
  Vector3 normal[6];
  float distance[6];
} Frustum;

typedef struct MesherStats {
  int chunksDrawn;
  int chunksTotal;
} MesherStats;

// One texture per block and face slot; untextured faces use their block colour.
typedef struct BlockTextures {
  Texture2D images[BLOCK_COUNT][BLOCK_FACE_COUNT];
  bool loaded[BLOCK_COUNT][BLOCK_FACE_COUNT];
} BlockTextures;

void MesherInit(const BlockTextures *textures);
void MesherShutdown(void);
void MesherRebuildDirty(World *world);
void MesherReload(World *world);
Frustum MesherMakeFrustum(Vector3 eye, Vector3 forward, Vector3 cameraUp,
                          float fovDegrees, float aspect);
MesherStats MesherDraw(const Frustum *frustum);

#endif
