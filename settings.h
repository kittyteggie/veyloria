#ifndef VEYLORIA_SETTINGS_H
#define VEYLORIA_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

// SUMMARY: Game settings, the config field table, and settings persistence.

#define DEFAULT_RESOLUTION_INDEX 1
#define DEFAULT_SENSITIVITY_INDEX 2
#define DEFAULT_FOV 75
#define DEFAULT_VOLUME 80
#define FOV_MIN 60
#define FOV_MAX 110
#define FOV_STEP 5
#define VOLUME_STEP 10
#define RESOLUTION_COUNT 7
#define SENSITIVITY_COUNT 8

typedef struct Resolution {
  int width;
  int height;
} Resolution;

typedef struct GameSettings {
  int resolutionIndex;
  int sensitivityIndex;
  int fovDegrees;
  int volume;
  bool fullscreen;
  bool vsync;
} GameSettings;

typedef enum ConfigField {
  CFG_RESOLUTION,
  CFG_FULLSCREEN,
  CFG_FOV,
  CFG_SENSITIVITY,
  CFG_VSYNC,
  CFG_VOLUME,
  CFG_FIELD_COUNT
} ConfigField;

GameSettings SettingsDefaults(void);
void SettingsLoad(GameSettings *settings);
bool SettingsSave(const GameSettings *settings);
void SettingsApplyRuntime(const GameSettings *settings);
void SettingsApply(GameSettings *settings);
void SettingsAdjust(GameSettings *settings, ConfigField field, int step);
void SettingsFieldText(const GameSettings *settings, ConfigField field,
                       char *buffer, size_t size);
const char *SettingsFieldName(ConfigField field);
Resolution SettingsResolution(const GameSettings *settings);
float SettingsSensitivity(const GameSettings *settings);
unsigned int SettingsWindowFlags(const GameSettings *settings);

#endif
