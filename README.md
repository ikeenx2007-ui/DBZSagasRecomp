# Dragon Ball Z: Sagas - XBE Recomp

An unofficial native PC port of the original Xbox version of Dragon Ball Z: Sagas, by **iKen_x**.

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp) tool.

> [!IMPORTANT]
> This project does not include any game files. You must provide the files from your own legally acquired copy of the game.
>
> This is a fan project. It is not affiliated with or endorsed by Atari, Pipeworks, Bandai Namco, Toei Animation, Bird Studio/Shueisha or any other owner of Dragon Ball or Dragon Ball Z: Sagas.

## Supported version

- Dragon Ball Z: Sagas for the original **Xbox, USA (NTSC-U)**.

Other versions (PAL, PlayStation 2, GameCube) are not supported.

### Checking your copy
The executable only runs the exact `default.xbe` it was built from. Each release lists the SHA-1 it expects. To see yours, open a terminal in the folder with your `default.xbe` and run:

```
certutil -hashfile default.xbe SHA1
```

If the number differs from the one in the release, the game shows a message about another release: you have a different version of the game. You can still build the port yourself from your own copy (see [Building](#building)).

## System requirements

- Windows 10 64-bit or newer
- A GPU with Direct3D 12 support
- About 3 to 5 GB of free disk space (the game, the bundled compiler and the first start's build)

## Installation

1. Download the latest `DBZSagas.zip` from [Releases](https://github.com/ikeenx2007-ui/DBZSagasRecomp/releases) and extract it.
2. Put your game in the `assets` folder, in one of these two ways:
   - **The disc image**: your own dump of the disc as an `.iso` file.
   - **The extracted files**: `default.xbe` and the `data_xbox` folder, directly inside `assets` (no folder in between).
3. Run `DBZSagas.exe`.

The first start compiles the game's code for your PC, with the compiler in the `compiler` folder. It takes a while (30 to 90 minutes depending on your CPU) and only happens once. **Do not close the window while it works.**

Recomp settings can be adjusted in `settings.toml` beside the exe.

### Getting the game files from your disc
- **From a modded Xbox**: copy the game's folder from the console over FTP, then take `default.xbe` and `data_xbox` from it.
- **From an ISO of your own disc**: put the `.iso` in `assets` as it is, or extract it with a tool such as [extract-xiso](https://github.com/XboxDev/extract-xiso) and copy `default.xbe` and `data_xbox`.

## Controls

### Controller
Xbox, PlayStation and Switch controllers are supported, with hot plugging and rumble.

### Keyboard
The keyboard works as controller 1, so the game can be played without a controller. A controller on the same port keeps working at the same time.

| Action | Xbox button | Key |
|---|---|---|
| Move | Left stick | W A S D |
| Camera | Right stick | Arrow keys |
| Jump | A | K or Space |
| Ki blast | B | L |
| Punch | X | J |
| Kick | Y | I |
| | Black / White | O / P |
| | Left / Right trigger | Q / E |
| | D-pad | T F G H |
| | Start / Back | Enter / Backspace |
| | Left / Right stick click | C / V |

Keys can be changed in `controls.ini`, created beside the exe on the first start. Several keys can be given for one button, separated by commas (`A = K, Space`). The file also sets which controller port the keyboard drives and whether it only works while the game window is in front.

## Troubleshooting

- **A message about another release**: your `default.xbe` is not the version this release was built from. See [Checking your copy](#checking-your-copy).
- **The game asks for the game files**: `assets` must hold the `.iso`, or `default.xbe` with `data_xbox` beside it, not inside another folder.
- **The first start was closed halfway**: run `DBZSagas.exe` again, it carries on.
- **The game closes or freezes**: the log is in the `logs` folder beside the exe, and a crash leaves a file in `CrashDumps`. Attach both when you report the problem in [Issues](https://github.com/ikeenx2007-ui/DBZSagasRecomp/issues).

## Building

### 1. Install the dependencies
- [Git](https://git-scm.com/download/win)
- Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two)
- [7-Zip](https://www.7-zip.org/), only to build the release package
- About 5 GB of free disk space

### 2. Clone the repository
Clone this repository with its SDK submodule:

```
git clone --recurse-submodules https://github.com/ikeenx2007-ui/DBZSagasRecomp.git
```

The `sdk` folder is a [fork of Xbox XBE Recomp](https://github.com/ikeenx2007-ui/XboxXBERecomp) that adds keyboard support. A copy downloaded as a ZIP has no `sdk` folder: the build downloads the fork by itself.

### 3. Add the game files
Copy `default.xbe` and the `data_xbox` folder from your own copy of the game into the `assets` folder.

### 4. Build
Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.

Run `release.bat` to create `DBZSagas.zip`, the package for players, with no game files in it.

## Credits

- [TGP482](https://github.com/TGP482) for [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp), the recompiler and runtime this port is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.

## License

The source code of this project is under the [MIT License](LICENSE). The license does not cover the game or any of its files.
