#include "player.h"

#include "blocks.h"

#include <math.h>

// SUMMARY: Implements player movement physics and block raycasting.

Vector3 PlayerEye(const Player *player) {
  float height = player->sneaking ? PLAYER_SNEAK_EYE_HEIGHT : PLAYER_EYE_HEIGHT;
  return (Vector3){player->feet.x, player->feet.y + height, player->feet.z};
}

Vector3 LookDirection(const Player *player) {
  float horizontal = cosf(player->pitch);
  return (Vector3){sinf(player->yaw) * horizontal, sinf(player->pitch),
                   -cosf(player->yaw) * horizontal};
}

bool PlayerCollides(const World *world, Vector3 feet) {
  int minX = (int)floorf(feet.x - PLAYER_RADIUS);
  int maxX = (int)floorf(feet.x + PLAYER_RADIUS);
  int minY = (int)floorf(feet.y + 0.001f);
  int maxY = (int)floorf(feet.y + PLAYER_HEIGHT - 0.001f);
  int minZ = (int)floorf(feet.z - PLAYER_RADIUS);
  int maxZ = (int)floorf(feet.z + PLAYER_RADIUS);

  for (int x = minX; x <= maxX; x++) {
    for (int y = minY; y <= maxY; y++) {
      for (int z = minZ; z <= maxZ; z++) {
        if (blockDefs[WorldGetBlock(world, x, y, z)].solid)
          return true;
      }
    }
  }
  return false;
}

// True when at least one block sits directly beneath the player's footprint.
bool PlayerHasSupport(const World *world, Vector3 feet) {
  int minX = (int)floorf(feet.x - PLAYER_RADIUS);
  int maxX = (int)floorf(feet.x + PLAYER_RADIUS);
  int minZ = (int)floorf(feet.z - PLAYER_RADIUS);
  int maxZ = (int)floorf(feet.z + PLAYER_RADIUS);
  int y = (int)floorf(feet.y - 0.001f);

  for (int x = minX; x <= maxX; x++) {
    for (int z = minZ; z <= maxZ; z++) {
      if (blockDefs[WorldGetBlock(world, x, y, z)].solid)
        return true;
    }
  }
  return false;
}

static void MovePlayerAxis(Player *player, const World *world, float amount,
                           int axis) {
  if (amount == 0.0f)
    return;

  Vector3 next = player->feet;
  if (axis == 0)
    next.x += amount;
  else if (axis == 1)
    next.y += amount;
  else
    next.z += amount;

  if (axis == 0) {
    if (next.x < PLAYER_RADIUS)
      next.x = PLAYER_RADIUS;
    if (next.x > WORLD_WIDTH - PLAYER_RADIUS)
      next.x = WORLD_WIDTH - PLAYER_RADIUS;
  } else if (axis == 2) {
    if (next.z < PLAYER_RADIUS)
      next.z = PLAYER_RADIUS;
    if (next.z > WORLD_DEPTH - PLAYER_RADIUS)
      next.z = WORLD_DEPTH - PLAYER_RADIUS;
  } else {
    if (next.y < 0.0f) {
      next.y = 0.0f;
      player->grounded = amount < 0.0f;
      player->verticalVelocity = 0.0f;
    }
    if (next.y > WORLD_HEIGHT - PLAYER_HEIGHT) {
      next.y = WORLD_HEIGHT - PLAYER_HEIGHT;
      player->verticalVelocity = 0.0f;
    }
  }

  if (!PlayerCollides(world, next)) {
    // Sneaking keeps you on ledges: refuse moves with nothing underneath.
    if (axis != 1 && player->sneaking && player->grounded &&
        !PlayerHasSupport(world, next))
      return;
    player->feet = next;
  } else if (axis == 1 && amount < 0.0f) {
    player->grounded = true;
    player->verticalVelocity = 0.0f;
  } else if (axis == 1 && amount > 0.0f) {
    player->verticalVelocity = 0.0f;
  }
}

void UpdatePlayer(Player *player, const World *world, float deltaTime,
                  bool applyMouseLook, float mouseSensitivity) {
  Vector2 mouse = GetMouseDelta();
  if (applyMouseLook) {
    player->yaw += mouse.x * mouseSensitivity;
    player->pitch -= mouse.y * mouseSensitivity;
  }
  if (player->pitch > MAX_PITCH)
    player->pitch = MAX_PITCH;
  if (player->pitch < -MAX_PITCH)
    player->pitch = -MAX_PITCH;

  float forwardInput = (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S);
  float sideInput = (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A);
  float inputLength =
      sqrtf(forwardInput * forwardInput + sideInput * sideInput);
  if (inputLength > 1.0f) {
    forwardInput /= inputLength;
    sideInput /= inputLength;
  }

  float speed = PLAYER_WALK_SPEED;
  player->sneaking = IsKeyDown(KEY_LEFT_SHIFT);
  if (player->sneaking)
    speed = PLAYER_SNEAK_SPEED;
  else if (IsKeyDown(KEY_LEFT_CONTROL))
    speed = PLAYER_SPRINT_SPEED;
  Vector3 forward = {sinf(player->yaw), 0.0f, -cosf(player->yaw)};
  Vector3 right = {cosf(player->yaw), 0.0f, sinf(player->yaw)};
  float moveX =
      (forward.x * forwardInput + right.x * sideInput) * speed * deltaTime;
  float moveZ =
      (forward.z * forwardInput + right.z * sideInput) * speed * deltaTime;
  MovePlayerAxis(player, world, moveX, 0);
  MovePlayerAxis(player, world, moveZ, 2);

  if (IsKeyPressed(KEY_SPACE) && player->grounded) {
    player->verticalVelocity = 7.0f;
    player->grounded = false;
  }

  player->grounded = false;
  player->verticalVelocity -= 20.0f * deltaTime;
  if (player->verticalVelocity < -30.0f)
    player->verticalVelocity = -30.0f;
  MovePlayerAxis(player, world, player->verticalVelocity * deltaTime, 1);
}

static int StepAxis(float direction) {
  if (direction > 0.0f)
    return 1;
  if (direction < 0.0f)
    return -1;
  return 0;
}

static float InitialRayBoundary(float origin, int cell, int step,
                                float direction) {
  if (step > 0)
    return ((float)cell + 1.0f - origin) / direction;
  if (step < 0)
    return (origin - (float)cell) / -direction;
  return INFINITY;
}

RayHit CastBlockRay(const World *world, Vector3 origin, Vector3 direction) {
  RayHit result = {0};
  int x = (int)floorf(origin.x);
  int y = (int)floorf(origin.y);
  int z = (int)floorf(origin.z);
  int stepX = StepAxis(direction.x);
  int stepY = StepAxis(direction.y);
  int stepZ = StepAxis(direction.z);
  float deltaX = (stepX == 0) ? INFINITY : fabsf(1.0f / direction.x);
  float deltaY = (stepY == 0) ? INFINITY : fabsf(1.0f / direction.y);
  float deltaZ = (stepZ == 0) ? INFINITY : fabsf(1.0f / direction.z);
  float maxX = InitialRayBoundary(origin.x, x, stepX, direction.x);
  float maxY = InitialRayBoundary(origin.y, y, stepY, direction.y);
  float maxZ = InitialRayBoundary(origin.z, z, stepZ, direction.z);
  int previousX = x;
  int previousY = y;
  int previousZ = z;
  float distance = 0.0f;

  while (distance <= REACH_DISTANCE) {
    if (blockDefs[WorldGetBlock(world, x, y, z)].solid) {
      result.hit = true;
      result.x = x;
      result.y = y;
      result.z = z;
      result.placeX = previousX;
      result.placeY = previousY;
      result.placeZ = previousZ;
      result.distance = distance;
      return result;
    }

    previousX = x;
    previousY = y;
    previousZ = z;
    if (maxX <= maxY && maxX <= maxZ) {
      x += stepX;
      distance = maxX;
      maxX += deltaX;
    } else if (maxY <= maxZ) {
      y += stepY;
      distance = maxY;
      maxY += deltaY;
    } else {
      z += stepZ;
      distance = maxZ;
      maxZ += deltaZ;
    }
  }
  return result;
}

bool PlacementOverlapsPlayer(const Player *player, int x, int y, int z) {
  float playerMinX = player->feet.x - PLAYER_RADIUS;
  float playerMaxX = player->feet.x + PLAYER_RADIUS;
  float playerMinY = player->feet.y;
  float playerMaxY = player->feet.y + PLAYER_HEIGHT;
  float playerMinZ = player->feet.z - PLAYER_RADIUS;
  float playerMaxZ = player->feet.z + PLAYER_RADIUS;

  return playerMinX < x + 1.0f && playerMaxX > x && playerMinY < y + 1.0f &&
         playerMaxY > y && playerMinZ < z + 1.0f && playerMaxZ > z;
}
