#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"

// SUMMARY: Applies settings to the window and loads or saves veyloria.cfg.

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define SETTINGS_FILE "veyloria.cfg"
#define SETTINGS_TEMP_FILE "veyloria.cfg.tmp"

static const Resolution resolutions[RESOLUTION_COUNT] = {
    {640, 480},  {WINDOW_WIDTH, WINDOW_HEIGHT}, {1024, 768}, {1280, 720},
    {1280, 960}, {1600, 900},                   {1920, 1080}};
static const float sensitivities[SENSITIVITY_COUNT] = {
    0.0005f, 0.0010f, 0.0015f, 0.0020f,
    0.0025f, 0.0030f, 0.0035f, 0.0040f};
static const char *configFieldNames[CFG_FIELD_COUNT] = {
    "Resolution", "Fullscreen", "Field of view", "Mouse sensitivity", "VSync",
    "Volume"};

GameSettings SettingsDefaults(void) {
  GameSettings settings = {DEFAULT_RESOLUTION_INDEX, DEFAULT_SENSITIVITY_INDEX,
                           DEFAULT_FOV, DEFAULT_VOLUME, false, true};
  return settings;
}

Resolution SettingsResolution(const GameSettings *settings) {
  return resolutions[settings->resolutionIndex];
}

float SettingsSensitivity(const GameSettings *settings) {
  return sensitivities[settings->sensitivityIndex];
}

unsigned int SettingsWindowFlags(const GameSettings *settings) {
  unsigned int flags = FLAG_MSAA_4X_HINT;
  if (settings->vsync)
    flags |= FLAG_VSYNC_HINT;
  if (settings->fullscreen)
    flags |= FLAG_FULLSCREEN_MODE;
  return flags;
}

const char *SettingsFieldName(ConfigField field) {
  return configFieldNames[field];
}

void SettingsFieldText(const GameSettings *settings, ConfigField field,
                       char *buffer, size_t size) {
  switch (field) {
  case CFG_RESOLUTION: {
    const Resolution resolution = resolutions[settings->resolutionIndex];
    snprintf(buffer, size, "%d x %d", resolution.width, resolution.height);
    break;
  }
  case CFG_FULLSCREEN:
    snprintf(buffer, size, "%s", settings->fullscreen ? "On" : "Off");
    break;
  case CFG_FOV:
    snprintf(buffer, size, "%d", settings->fovDegrees);
    break;
  case CFG_SENSITIVITY:
    snprintf(buffer, size, "%.4f", sensitivities[settings->sensitivityIndex]);
    break;
  case CFG_VSYNC:
    snprintf(buffer, size, "%s", settings->vsync ? "On" : "Off");
    break;
  case CFG_VOLUME:
    snprintf(buffer, size, "%d%%", settings->volume);
    break;
  default:
    snprintf(buffer, size, "?");
    break;
  }
}

void SettingsAdjust(GameSettings *settings, ConfigField field, int step) {
  switch (field) {
  case CFG_RESOLUTION:
    settings->resolutionIndex =
        (settings->resolutionIndex + RESOLUTION_COUNT + step) % RESOLUTION_COUNT;
    break;
  case CFG_FULLSCREEN:
    settings->fullscreen = !settings->fullscreen;
    break;
  case CFG_FOV:
    settings->fovDegrees += step * FOV_STEP;
    if (settings->fovDegrees < FOV_MIN)
      settings->fovDegrees = FOV_MIN;
    if (settings->fovDegrees > FOV_MAX)
      settings->fovDegrees = FOV_MAX;
    break;
  case CFG_SENSITIVITY:
    settings->sensitivityIndex += step;
    if (settings->sensitivityIndex < 0)
      settings->sensitivityIndex = 0;
    if (settings->sensitivityIndex > SENSITIVITY_COUNT - 1)
      settings->sensitivityIndex = SENSITIVITY_COUNT - 1;
    break;
  case CFG_VSYNC:
    settings->vsync = !settings->vsync;
    break;
  case CFG_VOLUME:
    settings->volume += step * VOLUME_STEP;
    if (settings->volume < 0)
      settings->volume = 0;
    if (settings->volume > 100)
      settings->volume = 100;
    break;
  default:
    break;
  }
}

void SettingsLoad(GameSettings *settings) {
  FILE *file = fopen(SETTINGS_FILE, "r");
  if (!file)
    return;

  char line[64];
  while (fgets(line, sizeof(line), file)) {
    char key[32];
    int value;
    if (sscanf(line, "%31[^=]=%d", key, &value) != 2)
      continue;

    if (strcmp(key, "resolution") == 0 && value >= 0 &&
        value < RESOLUTION_COUNT)
      settings->resolutionIndex = value;
    else if (strcmp(key, "sensitivity") == 0 && value >= 0 &&
             value < SENSITIVITY_COUNT)
      settings->sensitivityIndex = value;
    else if (strcmp(key, "fov") == 0 && value >= FOV_MIN && value <= FOV_MAX)
      settings->fovDegrees = value;
    else if (strcmp(key, "volume") == 0 && value >= 0 && value <= 100)
      settings->volume = value;
    else if (strcmp(key, "fullscreen") == 0)
      settings->fullscreen = value != 0;
    else if (strcmp(key, "vsync") == 0)
      settings->vsync = value != 0;
  }
  fclose(file);
}

bool SettingsSave(const GameSettings *settings) {
  FILE *file = fopen(SETTINGS_TEMP_FILE, "w");
  if (!file)
    return false;

  bool success =
      fprintf(file, "resolution=%d\nsensitivity=%d\nfov=%d\nfullscreen=%d\n"
                    "vsync=%d\nvolume=%d\n",
              settings->resolutionIndex, settings->sensitivityIndex,
              settings->fovDegrees, settings->fullscreen ? 1 : 0,
              settings->vsync ? 1 : 0, settings->volume) > 0;
  if (fclose(file) != 0)
    success = false;
  if (!success || rename(SETTINGS_TEMP_FILE, SETTINGS_FILE) != 0) {
    remove(SETTINGS_TEMP_FILE);
    return false;
  }
  return true;
}

void SettingsApplyRuntime(const GameSettings *settings) {
  if (settings->vsync)
    SetWindowState(FLAG_VSYNC_HINT);
  else
    ClearWindowState(FLAG_VSYNC_HINT);
  SetTargetFPS(settings->vsync ? 0 : 60);
  SetMasterVolume((float)settings->volume / 100.0f);
}

void SettingsApply(GameSettings *settings) {
  if (settings->fullscreen != IsWindowFullscreen())
    ToggleFullscreen();
  if (!settings->fullscreen) {
    Resolution resolution = resolutions[settings->resolutionIndex];
    SetWindowSize(resolution.width, resolution.height);
  }

  SettingsApplyRuntime(settings);
  if (!SettingsSave(settings))
    TraceLog(LOG_WARNING, "Could not save %s.", SETTINGS_FILE);
}
