# V Post Cyc - Live Timecycle Editor

An in-game, real-time timecycle editor for GTA V (Legacy and Enhanced), built as an ASI plugin for ScriptHookV.

Edit fog, colors, lighting, reflections, metal shininess, sky, post-processing and more from an in-game menu, see the result instantly, then export or save your XML files.

![Screenshot](Screenshot_2026-11-19_21-37-20.png)

## Features

- **Live editing** of every weather cycle file (`w_*.xml`) across both regions (`GLOBAL`, `URBAN`) and all 13 time-of-day keyframes.
- **Live editing** of timecycle modifier files (`timecycle_mods_*.xml`), patched in-place in game memory.
- **Quick Edit**: jump straight to the active weather at the current hour.
- **Categories**: Fog & Haze, Metals & Reflections, Lighting & Colors, Sky & Clouds, PostFX & Tone Mapping, Water/Heat/Particles, Shadows/LOD/Misc.
- **Per-variable reset**: restore any single value (or a whole variable across all hours) to its original value.
- **Edit all 13 hours together** or one keyframe at a time.
- **Modifier preview** with adjustable strength.
- **World controls**: set or freeze the clock, switch weather, clear the active modifier.
- **Non-destructive**: live edits go to a cache folder. Your original files are only changed when you choose **Save** (with automatic backups). **Export** writes to a separate folder.
- **Configurable** toggle key and UI layout through an `.ini` file.

## Requirements

- GTA V (Legacy `GTA5.exe` or Enhanced `GTA5_Enhanced.exe`)
- [ScriptHookV](http://www.dev-c.com/gtav/scripthookv/) and its ASI loader (`dinput8.dll`)
- Your own `timecycle` XML files (`w_*.xml`, `timecycle_mods_*.xml`). These are not included in this repository.

## Installation

1. Build the project (see below) or download a release.
2. Copy into your GTA V root folder:
   - `TimecycleEditor.asi`
   - `TimecycleEditor.ini`
   - a `timecycle\` folder containing your `w_*.xml` and `timecycle_mods_*.xml` files
3. Launch the game and press `,` to open the menu.

## Controls

| Key | Action |
| --- | --- |
| `,` (configurable) | Open / close menu |
| Up / Down (or Numpad 8 / 2) | Navigate |
| Left / Right (or Numpad 4 / 6) | Change value |
| Enter (or Numpad 5) | Select / type an exact value |
| R or Delete | Reset the selected value to its original |
| Backspace (or Numpad 0) | Back |
| Hold Shift | 10x step |
| Hold Ctrl | 0.1x step |

## Configuration (`TimecycleEditor.ini`)

```ini
[General]
ToggleKey = ,                 ; , . / ; F1-F24 INSERT HOME END A-Z NUMPAD0-9 or hex VK (0xBC)
RedirectFromFolder = true     ; load timecycle files from the folder below
TimecycleFolder = timecycle
ExportFolder = timecycle/export

[Editor]
StepDefault = 0.05

[UI]
MenuX = 0.015
MenuY = 0.035
MenuWidth = 0.275
MaxVisibleRows = 14
```

## Save vs Export

| Action | Writes to | Originals touched? |
| --- | --- | --- |
| Live edit | `timecycle/_live/` | No |
| Export All Modified | `timecycle/export/` | No |
| Save All Modified | `timecycle/` | Yes, with backups in `timecycle/backup/` (`.orig`, `.bak1`-`.bak3`) |
| Discard All Live Changes | removes `_live/` | No |

## Screenshots

![Screenshot 1](Screenshot_2026-11-19_21-37-20.png)
![Screenshot 2](Screenshot_2026-11-19_21-37-28.png)
![Screenshot 3](Screenshot_2026-11-19_21-37-58.png)
![Screenshot 4](Screenshot_2026-11-19_21-38-12.png)
![Screenshot 5](Screenshot_2026-11-19_21-38-14.png)
![Screenshot 6](Screenshot_2026-11-19_21-38-17.png)
![Screenshot 7](Screenshot_2026-11-19_21-39-02.png)

## Building

Requires Visual Studio (C++ desktop workload) and CMake 3.20+.

```powershell
cmake -S . -B build -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build --config Release
```

Output: `build/bin/TimecycleEditor.asi`

## Troubleshooting

A `TimecycleEditor.log` file is written next to the ASI. It lists which game signatures were found and which files were loaded. If live reload does not work after a game update, the byte patterns in `src/game/signatures.h` likely need updating.

## Credits

- **[fxReloader](https://github.com/crxhvrd/fxReloader)** by CoreFX (GPL-3.0): game signatures, the sim-thread marshal approach, and the in-place modifier patching technique are derived from this project.
- **[MinHook](https://github.com/TsudaKageyu/minhook)** by Tsuda Kageyu (BSD 2-Clause).
- **[ScriptHookV SDK](http://www.dev-c.com/gtav/scripthookv/)** by Alexander Blade.
- **[FiveM / Cfx.re](https://github.com/citizenfx/fivem)** and **[CodeWalker](https://github.com/dexyfex/CodeWalker)**: used as references for timecycle structures and formats.

## License

GPL-3.0. See [LICENSE](LICENSE).
