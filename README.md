# Veyloria

A small first-person voxel prototype written in C with raylib.

## Build and run

Install a C compiler, Make, and raylib, then run:

```sh
make
./bin/veyloria
```

Run `make clean` to remove build files from `bin/`.

## Controls

- **Main menu:** Click Play, Config, or Exit
- **Config:** Click `<` / `>` to change resolution or sensitivity; Escape returns
- **Escape while playing:** Return to the main menu
- **WASD:** Move
- **Mouse:** Look around
- **Space:** Jump
- **Left Shift:** Sprint
- **Left mouse button:** Break a block
- **Right mouse button:** Place a block
- **1 / 2:** Select grass / stone
- **F1:** Toggle debug information
- **F5:** Save the world
- **F9:** Reload the last saved world
- **Escape from the main menu:** Quit


## Documentation

Config offers 640×480, 800×600, and 1024×768 resolutions, plus adjustable mouse
sensitivity. Settings are saved in `veyloria.cfg`.

The game defaults to 800×600. Menu images go in `assets/`: `menubg.png` should
be 800×600 and `logo.png` should be 384×192. Both are optional; the menu has a
fallback background and title. The background is center-cropped to fill other
4:3 window sizes without stretching.

Block textures use the block name in `assets/`, for example `grass.png` and
`stone.png`. Each image is used on every face of that block; missing textures
fall back to the built-in block colors.

The game loads `veyloria-world.sav` on startup and saves automatically on exit.
World data is DEFLATE-compressed, and existing version 1 saves are still supported.
