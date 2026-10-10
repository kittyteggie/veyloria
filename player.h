#ifndef VEYLORIA_PLAYER_H
#define VEYLORIA_PLAYER_H

#include <stdbool.h>

#include "raylib.h"

// SUMMARY: Shared player state and the physical constants used by movement and save validation.

#define PLAYER_RADIUS 0.30f
#define PLAYER_HEIGHT 1.80f
#define PLAYER_EYE_HEIGHT 1.62f
#define MAX_PITCH 1.5f

typedef struct Player {
  Vector3 feet;
  float yaw;
  float pitch;
  float verticalVelocity;
  bool grounded;
} Player;

#endif
