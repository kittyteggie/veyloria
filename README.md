# Veyloria

A small first-person voxel prototype written in C with raylib.

## Build and run

Install a C compiler, Make, and raylib, then run:

```sh
make
./bin/veyloria
```

Run `make test` to build and run the storage tests, and `make clean` to remove
build files from `bin/`.

## Controls

- **Main menu:** Click Play, Config, or Exit
- **Config:** Click `<` / `>` to change a setting; Escape returns
- **Escape while playing:** Return to the main menu
- **F11:** Toggle fullscreen
- **WASD:** Move
- **Mouse:** Look around
- **Space:** Jump
- **Left Shift:** Sprint
- **Left mouse button:** Break a block
- **Right mouse button:** Place a block
- **1 / 2:** Select hotbar slot
- **F1:** Toggle debug information
- **F5:** Save the world
- **F9:** Reload the last saved world
- **Escape from the main menu:** Quit

## Documentation

Config offers resolution (640×480 up to 1920×1080), fullscreen, field of view
(60–110), mouse sensitivity, VSync, and master volume. Settings are saved in
`veyloria.cfg`. With VSync on the frame rate is uncapped and synced to the
display; with VSync off it is capped at 60 FPS.

The game defaults to 800×600. Menu images go in `assets/`: `menubg.png` should
be 800×600 and `logo.png` should be 384×192. Both are optional; the menu has a
fallback background and title. The background is center-cropped to fill other
window sizes without stretching.

Block textures use the block name in `assets/`, for example `grass.png` and
`stone.png`. Each image is used on every face of that block with point filtering
(pixel art stays crisp); missing textures fall back to the built-in block colors.

The world is 256×256×256 blocks stored as sparse 16×16×16 chunks. Only chunks
that contain blocks are allocated, and breaking or placing a block remeshes just
the affected chunk and its bordering neighbours. Chunks outside the camera
frustum are not drawn; F1 shows how many are drawn versus loaded.

The game loads `veyloria-world.sav` on startup and saves automatically on exit.
World data is DEFLATE-compressed and only non-empty chunks are stored. Saves
from older versions cannot be read; on startup such a file is moved to
`veyloria-world.sav.bak` and a fresh world is created.
