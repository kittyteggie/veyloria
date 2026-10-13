#ifndef VEYLORIA_PLAYER_H
#define VEYLORIA_PLAYER_H

#include <stdbool.h>

#include "raylib.h"
#include "world.h"

// SUMMARY: Shared player state, movement, and block raycasting.

#define PLAYER_RADIUS 0.30f
#define PLAYER_HEIGHT 1.80f
#define PLAYER_EYE_HEIGHT 1.62f
#define PLAYER_SNEAK_EYE_HEIGHT 1.27f
#define PLAYER_WALK_SPEED 4.5f
#define PLAYER_SPRINT_SPEED 7.0f
#define PLAYER_SNEAK_SPEED 1.8f
#define MAX_PITCH 1.5f
#define REACH_DISTANCE 6.0f

typedef struct Player {
  Vector3 feet;
  float yaw;
  float pitch;
  float verticalVelocity;
  bool grounded;
  bool sneaking;
} Player;

typedef struct RayHit {
  bool hit;
  int x;
  int y;
  int z;
  int placeX;
  int placeY;
  int placeZ;
  float distance;
} RayHit;

Vector3 PlayerEye(const Player *player);
Vector3 LookDirection(const Player *player);
bool PlayerCollides(const World *world, Vector3 feet);
bool PlayerHasSupport(const World *world, Vector3 feet);
void UpdatePlayer(Player *player, const World *world, float deltaTime,
                  bool applyMouseLook, float mouseSensitivity);
RayHit CastBlockRay(const World *world, Vector3 origin, Vector3 direction);
bool PlacementOverlapsPlayer(const Player *player, int x, int y, int z);

#endif
