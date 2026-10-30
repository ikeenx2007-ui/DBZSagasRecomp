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

## Installation

1. Download the latest `DBZSagas.zip` from [Releases](https://github.com/ikeenx2007-ui/DBZSagasRecomp/releases) and extract it.
2. Dump your own copy of the game and copy `default.xbe` and the `data_xbox` folder into the `assets` folder.
3. Run `DBZSagas.exe`. The first start compiles the game's code for your PC. It takes a while (30 to 90 minutes depending on your CPU) and only happens once.

Recomp settings can be adjusted in `settings.toml` beside the exe.

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

## System requirements

- Windows 10 64-bit or newer
- A GPU with Direct3D 12 support

## Building

### 1. Clone the repository
Clone this repository with [Git](https://git-scm.com/), including the SDK submodule:

```
git clone --recurse-submodules https://github.com/ikeenx2007-ui/DBZSagasRecomp.git
```

The `sdk` folder is a [fork of Xbox XBE Recomp](https://github.com/ikeenx2007-ui/XboxXBERecomp) that adds keyboard support.

### 2. Add the game files
Copy `default.xbe` and the `data_xbox` folder from your own copy of the game into the `assets` folder.

### 3. Install the dependencies
Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two). Building the release package also needs [7-Zip](https://www.7-zip.org/).

### 4. Build
Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.

Run `release.bat` to create `DBZSagas.zip`, the package for players, with no game files in it.

## Credits

- [TGP482](https://github.com/TGP482) for [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp), the recompiler and runtime this port is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.

## License

The source code of this project is under the [MIT License](LICENSE). The license does not cover the game or any of its files.
