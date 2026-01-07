# Repository Guidelines

## Project Structure & Module Organization
The firmware lives under `App/` (scheduler, UI, radio drivers, helper modules) with subfolders such as `app/`, `driver/`, `helper/`, and `ui/`. Cube-generated startup code and vendor HAL layers remain in `Core/`, `Drivers/`, and `Middlewares/`, so keep hardware updates scoped there. Toolchain files and presets sit in `cmake/`, `CMakeLists.txt`, and `CMakePresets.json`. Documentation assets live in `images/` alongside `README.md`, while developer utilities reside in `tools/` (serial monitor, viewer, unbrick toolkit). Build outputs and intermediate object files are created in `build/<Preset>/`.

## Build, Test, and Development Commands
- `./compile-with-docker.sh Fusion` — canonical Dockerized build; the script rebuilds the container if missing, cleans `build/`, configures the preset, and emits `.elf/.bin/.hex` files under `build/Fusion/App/`.
- `./compile-with-docker.sh All` — sequentially compiles every preset to ensure edition parity before large merges.
- Native toolchain: `cmake --preset Bandscope && cmake --build --preset Bandscope` — use when you maintain an ARM GCC environment outside Docker.
- Inspect binary size with `arm-none-eabi-size build/Bandscope/App/f4hwn.bandscope.elf` before committing risky changes.

## Coding Style & Naming Conventions
C modules use 4-space indentation, Allman braces, and snake_case filenames (`board.c`, `helper/battery.c`). Feature toggles are uppercase macros (`ENABLE_FEAT_F4HWN_*`) defined through presets; keep any new logic guarded consistently so each firmware edition compiles. Add new headers only when multiple translation units require the declarations, otherwise prefer `static` helpers scoped within the module. Document timing-critical register sequences or non-obvious hardware side effects with short comments.

## Testing Guidelines
The repository has no automated CI, so contributors are expected to validate manually. Rebuild at least the presets affected by your changes, flash the `.bin` via [UVTools2](https://armel.github.io/uvtools2/), and exercise RX/TX, audio, and UI workflows touched. Capture and attach relevant serial diagnostics using `tools/serialtool` (or screenshots from `tools/k5viewer`) when debugging. If you add host utilities or math-heavy helpers, provide a small reproducible test or calculation log in the PR description.

## Commit & Pull Request Guidelines
Existing history favors concise imperative messages and references to PR numbers (`Fix VFO clamp`, `Merge pull request #41 ...`). Follow that format, keep subject lines under ~72 characters, and reference issues with `#ID` when applicable. Each PR should describe the user-visible change, list which presets/devices were tested, mention calibration impacts, and include screenshots for UI tweaks. Highlight any new configuration flags in both the PR body and README so downstream builders can reconfigure their radios confidently.
