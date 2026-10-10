#include "mesher.h"

#include "raymath.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// SUMMARY: Turns dirty chunks into GPU meshes and draws the visible ones.

#define FRUSTUM_FAR 1200.0f

typedef struct MeshBuilder {
  float *vertices;
  float *normals;
  float *texcoords;
  unsigned char *colors;
  int vertexCount;
  int capacity;
} MeshBuilder;

typedef struct ChunkRender {
  Model models[BLOCK_COUNT];
} ChunkRender;

static ChunkRender chunkRenders[CHUNKS_X][CHUNKS_Y][CHUNKS_Z];
static const BlockTextures *blockTextures;

void MesherInit(const BlockTextures *textures) { blockTextures = textures; }

static bool FaceTextured(BlockId id, BlockFace face) {
  return blockTextures->loaded[id][face];
}

static void EnsureMeshCapacity(MeshBuilder *builder, int required) {
  if (required <= builder->capacity)
    return;

  int newCapacity = builder->capacity ? builder->capacity : 1024;
  while (newCapacity < required)
    newCapacity *= 2;

  builder->vertices =
      MemRealloc(builder->vertices, (unsigned int)(newCapacity * 3 * sizeof(float)));
  builder->normals =
      MemRealloc(builder->normals, (unsigned int)(newCapacity * 3 * sizeof(float)));
  builder->texcoords =
      MemRealloc(builder->texcoords, (unsigned int)(newCapacity * 2 * sizeof(float)));
  builder->colors = MemRealloc(
      builder->colors, (unsigned int)(newCapacity * 4 * sizeof(unsigned char)));
  if (!builder->vertices || !builder->normals || !builder->texcoords ||
      !builder->colors) {
    TraceLog(LOG_FATAL, "Failed to allocate world mesh");
    exit(EXIT_FAILURE);
  }
  builder->capacity = newCapacity;
}

static bool BlockIsSolid(const World *world, int x, int y, int z) {
  return blockDefs[WorldGetBlock(world, x, y, z)].solid;
}

// Classic corner ambient occlusion: 3 is fully lit, 0 is the darkest corner.
static int VertexOcclusion(bool side1, bool side2, bool corner) {
  if (side1 && side2)
    return 0;
  return 3 - ((int)side1 + (int)side2 + (int)corner);
}

static void AddFace(const World *world, MeshBuilder *builder, int x, int y,
                    int z, BlockId id, int face) {
  // Each face is four corners in outward-facing winding order.
  static const Vector3 corners[6][4] = {
      {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, // +X
      {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}, // -X
      {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, // +Y
      {{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}, // -Y
      {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, // +Z
      {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}  // -Z
  };
  static const Vector3 normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  static const float shade[6] = {0.78f, 0.54f, 1.0f, 0.58f, 0.88f, 0.70f};
  static const int orderFlat[6] = {0, 1, 2, 0, 2, 3};
  static const int orderFlipped[6] = {0, 1, 3, 1, 2, 3};
  // UVs per face and corner: u runs along the face's screen-right direction and
  // v runs downwards, so a texture appears upright and unmirrored from outside.
  static const int texU[6][4] = {
      {1, 1, 0, 0}, {1, 1, 0, 0}, {0, 0, 1, 1},
      {0, 0, 1, 1}, {1, 1, 0, 0}, {1, 1, 0, 0}};
  static const int texV[6][4] = {
      {1, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 1, 0},
      {0, 1, 1, 0}, {1, 0, 0, 1}, {1, 0, 0, 1}};

  BlockFace slot = BlockFaceOf(face);
  const BlockColor *tint = &blockDefs[id].color[slot];
  bool textured = FaceTextured(id, slot);
  int normalAxis = face / 2;
  int axis1 = (normalAxis + 1) % 3;
  int axis2 = (normalAxis + 2) % 3;
  int normalOffset[3] = {(int)normals[face].x, (int)normals[face].y,
                         (int)normals[face].z};
  int base[3] = {x + normalOffset[0], y + normalOffset[1],
                 z + normalOffset[2]};

  int occlusion[4];
  for (int corner = 0; corner < 4; corner++) {
    Vector3 point = corners[face][corner];
    int dir[3] = {point.x > 0.5f ? 1 : -1, point.y > 0.5f ? 1 : -1,
                  point.z > 0.5f ? 1 : -1};
    int offset1[3] = {0, 0, 0};
    int offset2[3] = {0, 0, 0};
    offset1[axis1] = dir[axis1];
    offset2[axis2] = dir[axis2];

    bool side1 =
        BlockIsSolid(world, base[0] + offset1[0], base[1] + offset1[1],
                     base[2] + offset1[2]);
    bool side2 =
        BlockIsSolid(world, base[0] + offset2[0], base[1] + offset2[1],
                     base[2] + offset2[2]);
    bool cornerSolid = BlockIsSolid(
        world, base[0] + offset1[0] + offset2[0],
        base[1] + offset1[1] + offset2[1], base[2] + offset1[2] + offset2[2]);
    occlusion[corner] = VertexOcclusion(side1, side2, cornerSolid);
  }

  // Cut the quad along the brighter diagonal so the AO gradient stays smooth.
  const int *order = (occlusion[0] + occlusion[2] > occlusion[1] + occlusion[3])
                         ? orderFlipped
                         : orderFlat;

  EnsureMeshCapacity(builder, builder->vertexCount + 6);
  for (int i = 0; i < 6; i++) {
    int corner = order[i];
    Vector3 point = corners[face][corner];
    float brightness =
        shade[face] * (0.55f + 0.45f * (float)occlusion[corner] / 3.0f);
    float red = textured ? 255.0f : (float)tint->red;
    float green = textured ? 255.0f : (float)tint->green;
    float blue = textured ? 255.0f : (float)tint->blue;

    int vertex = builder->vertexCount++;
    builder->vertices[vertex * 3 + 0] = (float)x + point.x;
    builder->vertices[vertex * 3 + 1] = (float)y + point.y;
    builder->vertices[vertex * 3 + 2] = (float)z + point.z;
    builder->normals[vertex * 3 + 0] = normals[face].x;
    builder->normals[vertex * 3 + 1] = normals[face].y;
    builder->normals[vertex * 3 + 2] = normals[face].z;
    builder->texcoords[vertex * 2 + 0] = (float)texU[face][corner];
    builder->texcoords[vertex * 2 + 1] = (float)texV[face][corner];
    builder->colors[vertex * 4 + 0] = (unsigned char)(red * brightness);
    builder->colors[vertex * 4 + 1] = (unsigned char)(green * brightness);
    builder->colors[vertex * 4 + 2] = (unsigned char)(blue * brightness);
    builder->colors[vertex * 4 + 3] = 255;
  }
}

static void UnloadChunkModels(int cx, int cy, int cz) {
  for (int id = 1; id < BLOCK_COUNT; id++) {
    if (chunkRenders[cx][cy][cz].models[id].meshCount > 0)
      UnloadModel(chunkRenders[cx][cy][cz].models[id]);
  }
  memset(&chunkRenders[cx][cy][cz], 0, sizeof(chunkRenders[cx][cy][cz]));
}

static void UnloadAllChunkModels(void) {
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++)
        UnloadChunkModels(cx, cy, cz);
    }
  }
}

// Packs the per-face-slot meshes of one block type into a single model, with
// one material per mesh so every slot keeps its own texture.
static Model BuildBlockModel(MeshBuilder builders[BLOCK_FACE_COUNT],
                             BlockId id) {
  int slots[BLOCK_FACE_COUNT];
  int meshCount = 0;
  for (int slot = 0; slot < BLOCK_FACE_COUNT; slot++) {
    if (builders[slot].vertexCount > 0)
      slots[meshCount++] = slot;
  }
  if (meshCount == 0)
    return (Model){0};

  Model model = {0};
  model.transform = MatrixIdentity();
  model.meshCount = meshCount;
  model.meshes = (Mesh *)MemAlloc((unsigned int)meshCount * sizeof(Mesh));
  model.materialCount = meshCount;
  model.materials =
      (Material *)MemAlloc((unsigned int)meshCount * sizeof(Material));
  model.meshMaterial =
      (int *)MemAlloc((unsigned int)meshCount * sizeof(int));

  for (int i = 0; i < meshCount; i++) {
    int slot = slots[i];
    MeshBuilder *builder = &builders[slot];

    Mesh mesh = {0};
    mesh.vertexCount = builder->vertexCount;
    mesh.triangleCount = builder->vertexCount / 3;
    mesh.vertices = builder->vertices;
    mesh.normals = builder->normals;
    mesh.texcoords = builder->texcoords;
    mesh.colors = builder->colors;
    UploadMesh(&mesh, false);

    model.meshes[i] = mesh;
    model.materials[i] = LoadMaterialDefault();
    model.meshMaterial[i] = i;
    if (FaceTextured(id, (BlockFace)slot)) {
      SetMaterialTexture(&model.materials[i], MATERIAL_MAP_DIFFUSE,
                         blockTextures->images[id][slot]);
    }
  }
  return model;
}

static void BuildChunkMesh(World *world, int cx, int cy, int cz) {
  static const int neighbor[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  MeshBuilder builders[BLOCK_COUNT][BLOCK_FACE_COUNT] = {0};
  const Chunk *chunk = WorldChunkAtConst(world, cx, cy, cz);

  if (chunk != NULL && chunk->maxLocalY >= 0) {
    int baseX = cx * CHUNK_SIZE;
    int baseY = cy * CHUNK_SIZE;
    int baseZ = cz * CHUNK_SIZE;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
      for (int lz = 0; lz < CHUNK_SIZE; lz++) {
        for (int ly = 0; ly <= chunk->maxLocalY; ly++) {
          BlockId id = chunk->blocks[lx][ly][lz];
          if (id == BLOCK_AIR)
            continue;

          for (int face = 0; face < 6; face++) {
            int nx = baseX + lx + neighbor[face][0];
            int ny = baseY + ly + neighbor[face][1];
            int nz = baseZ + lz + neighbor[face][2];
            if (!BlockIsSolid(world, nx, ny, nz)) {
              BlockFace slot = BlockFaceOf(face);
              AddFace(world, &builders[id][slot], baseX + lx, baseY + ly,
                      baseZ + lz, id, face);
            }
          }
        }
      }
    }
  }

  UnloadChunkModels(cx, cy, cz);
  for (int id = 1; id < BLOCK_COUNT; id++)
    chunkRenders[cx][cy][cz].models[id] = BuildBlockModel(builders[id], (BlockId)id);
  WorldMarkChunkClean(world, cx, cy, cz);
}

void MesherRebuildDirty(World *world) {
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++) {
        if (WorldChunkDirty(world, cx, cy, cz))
          BuildChunkMesh(world, cx, cy, cz);
      }
    }
  }
}

void MesherReload(World *world) {
  UnloadAllChunkModels();
  WorldMarkAllChunksDirty(world);
  MesherRebuildDirty(world);
}

void MesherShutdown(void) { UnloadAllChunkModels(); }

Frustum MesherMakeFrustum(Vector3 eye, Vector3 forward, Vector3 cameraUp,
                          float fovDegrees, float aspect) {
  Vector3 f = Vector3Normalize(forward);
  Vector3 r = Vector3Normalize(Vector3CrossProduct(f, cameraUp));
  Vector3 u = Vector3CrossProduct(r, f);
  float tanV = tanf(fovDegrees * DEG2RAD * 0.5f);
  float tanH = tanV * aspect;

  Vector3 leftDir = Vector3Subtract(f, Vector3Scale(r, tanH));
  Vector3 rightDir = Vector3Add(f, Vector3Scale(r, tanH));
  Vector3 upDir = Vector3Add(f, Vector3Scale(u, tanV));
  Vector3 downDir = Vector3Subtract(f, Vector3Scale(u, tanV));

  Frustum frustum;
  frustum.normal[0] = Vector3Normalize(Vector3CrossProduct(leftDir, u));
  frustum.normal[1] = Vector3Normalize(Vector3CrossProduct(u, rightDir));
  frustum.normal[2] = Vector3Normalize(Vector3CrossProduct(upDir, r));
  frustum.normal[3] = Vector3Normalize(Vector3CrossProduct(r, downDir));
  frustum.normal[4] = f;
  frustum.normal[5] = Vector3Scale(f, -1.0f);
  for (int i = 0; i < 5; i++)
    frustum.distance[i] = -Vector3DotProduct(frustum.normal[i], eye);
  frustum.distance[5] = Vector3DotProduct(f, eye) + FRUSTUM_FAR;
  return frustum;
}

static bool FrustumVisible(const Frustum *frustum, Vector3 min, Vector3 max) {
  for (int i = 0; i < 6; i++) {
    Vector3 corner = {
        frustum->normal[i].x >= 0.0f ? max.x : min.x,
        frustum->normal[i].y >= 0.0f ? max.y : min.y,
        frustum->normal[i].z >= 0.0f ? max.z : min.z,
    };
    if (Vector3DotProduct(frustum->normal[i], corner) + frustum->distance[i] <
        0.0f)
      return false;
  }
  return true;
}

MesherStats MesherDraw(const Frustum *frustum) {
  MesherStats stats = {0, 0};
  for (int cx = 0; cx < CHUNKS_X; cx++) {
    for (int cy = 0; cy < CHUNKS_Y; cy++) {
      for (int cz = 0; cz < CHUNKS_Z; cz++) {
        const Model *models = chunkRenders[cx][cy][cz].models;
        bool hasMesh = false;
        for (int id = 1; id < BLOCK_COUNT; id++) {
          if (models[id].meshCount > 0) {
            hasMesh = true;
            break;
          }
        }
        if (!hasMesh)
          continue;

        stats.chunksTotal++;
        Vector3 min = {(float)(cx * CHUNK_SIZE), (float)(cy * CHUNK_SIZE),
                       (float)(cz * CHUNK_SIZE)};
        Vector3 max = {min.x + CHUNK_SIZE, min.y + CHUNK_SIZE,
                       min.z + CHUNK_SIZE};
        if (!FrustumVisible(frustum, min, max))
          continue;

        stats.chunksDrawn++;
        for (int id = 1; id < BLOCK_COUNT; id++) {
          if (models[id].meshCount > 0)
            DrawModel(models[id], (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
        }
      }
    }
  }
  return stats;
}
