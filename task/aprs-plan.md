# APRS Receive/Decode Feature Plan

## Objective
- Replace the legacy Breakout game shortcut (hold key `7`) with an APRS receive/decoding mode that listens on the currently tuned VFO, demodulates AFSK1200 packets, displays decoded frames, and provides a minimal UI to review recent packets.

## Current Entry Point
- `App/app/main.c:585-621` handles numeric keys; under `ENABLE_FEAT_F4HWN_GAME`, key `7` triggers `APP_RunBreakout()`. We must gate this section behind a new APRS feature flag (e.g., `ENABLE_FEAT_F4HWN_APRS`) and detect `bKeyHeld` to only launch APRS on a long press.

## Proposed Architecture
1. **Feature Flag & Presets**
   - Add `ENABLE_FEAT_F4HWN_APRS` defaulting to `true` for Fusion/Bandscope/RescueOps (or all) in `CMakePresets.json`.
   - Remove/disable `ENABLE_FEAT_F4HWN_GAME` references when APRS replaces Breakout.
2. **APRS Module (`App/app/aprs.*`)**
   - Responsibilities: start/stop APRS mode, manage buffers, run demod/decoder state machine, draw UI pages.
   - Entry API similar to `APP_RunBreakout`.
3. **Signal Path**
   - Leverage existing audio samples (likely via DMA/ADC path used for spectrum or voice recording). Need to inspect `audio.c`, `app/spectrum.c`, or BK4819 baseband streaming to capture 8/16 kHz audio.
   - Configure BK4819 for baseband output with appropriate bandwidth/AF gain when APRS mode engages.
   - Run a Goertzel or quadrature demod to detect 1200/2200 Hz (Bell 202) and produce NRZI bits.
4. **Frame Decoding**
   - NRZI-to-NRZ conversion, bit de-stuffing, AX.25 frame detection (flags `0x7E`, CRC check, address+control decoding).
   - Extract APRS information field and parse position/status/messages.
   - Keep last N packets (e.g., ring buffer size 8) with timestamp + metadata (source, path, type).
5. **UI/Controls**
   - Dedicated screen: top status line (frequency, RX state, squelch), center list showing latest packets, bottom hints (`EXIT=Back`, `F+7=Clear`).
   - Provide detail view when using up/down + `MENU` to inspect long packets.
   - While APRS mode is active, disable normal RX audio output and optionally monitor squelch open indicator.
6. **Persistence/Settings**
   - Optional: simple menu entry enabling/disabling APRS decode or setting used audio filter.
   - If config is needed, update `settings.c`/`settings.h` and menu definitions.

## Task Breakdown & Status
| Status | Task |
| --- | --- |
| ☑ | Audit audio/baseband capture capabilities in `audio.c`, `radio.c`, and BK4819 driver to confirm sampling rates and required register tweaks. |
| ☑ | Define new build flag + preset defaults, remove Breakout-specific flag usage, ensure KEY7 hold detection launches APRS. |
| ☑ | Create `app/aprs.c/h` skeleton with state machine hooks (init, enter, loop, exit). |
| ☑ | Implement DSP pipeline: bandpass/pre-emphasis -> discriminators -> symbol timing -> NRZI decode -> AX.25 frame assembly. |
| ☑ | Implement APRS packet parser producing concise structs for display/logging. |
| ☑ | Build UI pages (list + detail) and integrate with key handling (EXIT to leave, F+7 to clear log, arrow keys to scroll). |
| ☐ | Add optional settings menu entries if needed (enable APRS, set audio gain, filter options). |
| ☐ | Test on hardware: tune to known APRS freq (144.390 MHz), capture logs, verify decode accuracy, measure CPU load, adjust buffers. |
| ☐ | Update documentation (README, AGENTS.md, changelog) describing new APRS mode and key combination. |

## Open Questions / Risks
- Can we reuse existing audio capture DMA path without starving other features? Need to confirm CPU budget for DSP at ~8 kHz.
- Do we need to pause other background tasks (scanner, voice prompts) during APRS mode?
- How to expose decoded data externally (UART/USB) for debugging?
- Should APRS mode use fixed frequency or follow the active VFO? (Assumption: use the VFO frequency/radio settings when launched.)

## Next Steps
1. Study BK4819 register flow around `RADIO_ConfigureChannel` to understand where to tap audio samples.
2. Prototype demodulation algorithm offline (maybe in host-side tool under `tools/misc`) before embedding into firmware.
3. Decide on memory footprint/performance trade-offs (e.g., buffer sizes, integer math vs lookup tables) and document constraints in this file.

## Notes from Repository Audit
- **Key handling (`App/app/main.c:407-640`)**: digit handler returns immediately when `bKeyHeld` is true, after calling `processFKeyFunction`. The Breakout shortcut (KEY_7) only runs on key release when `gWasFKeyPressed == true`. For APRS-on-hold we must intercept the held branch (probably before the early `return`) and differentiate via `Key == KEY_7 && bKeyHeld`.
- **Breakout module**: `App/app/breakout.c` provides `APP_RunBreakout()` invoked only when `ENABLE_FEAT_F4HWN_GAME` is on. Removing this dependency requires cleaning `breakout.h`, build includes, and preset flag.
- **BK4819 capabilities**: `driver/bk4819.c` exposes FSK helper routines (`BK4819_SetupAircopy`, `BK4819_PrepareFSKReceive`, etc.) already tuned for 1200 baud RX/TX. These register settings can likely seed the APRS signal path before feeding into a software AX.25 frame decoder.
- **Existing FSK consumer**: `app/aircopy.c` shows how to collect 36-word frames via `g_FSK_Buffer` and DMA/interrupt pipeline. Reusing that FIFO reader (or generalizing it) would save effort when sampling APRS bits rather than writing an entirely new ADC capture layer.

## Detailed Design Decisions
- **Feature flag plumbing**
  - Define `ENABLE_FEAT_F4HWN_APRS` in `cmake/options.cmake` (if exists) and propagate to all presets in `CMakePresets.json`.
  - Replace `ENABLE_FEAT_F4HWN_GAME` include guards with the new flag; remove Breakout references from non-essential builds.
  - Create menu exposure toggle (e.g., `APRS_ENABLE` bool) stored in EEPROM if runtime control desired.
- **Key handling**
  - Modify `MAIN_Key_DIGITS` to detect `Key == KEY_7` when `bKeyHeld` transitions from pressed to hold; require `gScreenToDisplay == DISPLAY_MAIN` to avoid hijacking text entry.
  - Add `APRS_Launch()` call that sets `gRequestDisplayScreen = DISPLAY_APRS`, resets APRS buffers, and returns without letting `processFKeyFunction` run.
  - Consider gating behind `gEeprom.MENU_LOCK` similar to Breakout due to RescueOps lock.
- **UI/Display layer**
  - Add `DISPLAY_APRS` enum entry plus `UI_DisplayAprs` function pointer hook in `ui/ui.c`.
  - Layout idea: header row shows `APRS MON` plus tuned frequency (reuse `UI_DisplayFrequency`), body lists last 4-5 packets with `SRC>DEST` and info snippet, bottom hints for `EXIT`, `F+7` actions.
  - Additional detail view triggered by `MENU` to expand selected packet; optionally in same screen with scroll offset.
- **APRS module structure**
  - `aprs.h` exports `void APRS_Run(void);`, `void APRS_Process(void);`, `void APRS_HandleEvent(KEY_Code_t key, bool pressed, bool held);`.
  - Module holds ring buffer of decoded packets, demod/decoder state, and uses scheduler to process incoming samples.
  - Integrate with `app/app.c` main task loop similar to other apps to keep mode-specific logic isolated.
- **DSP & decoding**
  - Configure BK4819 to route baseband audio to MCU ADC (reuse setup from `BK4819_SetAF(BK4819_AF_MUTE)` + enabling raw output?). Alternatively, reuse FSK hardware: `BK4819_SetupAircopy()` already sets 1200 baud demod; confirm register docs to ensure data path accessible (likely via `BK4819_REG_5F` FIFO). Need to experiment but plan to generalize `AIRCOPY_StorePacket` path to stream arbitrary bytes.
  - Once 0xAA preamble + NRZI bits retrieved, implement AX.25 framing: `FLAG 0x7E`, bit-stuff removal, CRC-16-IBM check, parse address fields and APRS info text. Keep parsing lightweight (position, status, message, telemetry).
  - Use integer math to minimize CPU; share bit-buffer utilities with existing CRC module.
- **Scheduler / interrupts**
  - Hook into existing BK4819 FSK interrupt (BK4819_REG_3F_FSK_FIFO_ALMOST_FULL) to feed APRS demod quickly; use `BK4819_ReadRegister(BK4819_REG_5F)` bursts.
  - Provide fallback polling routine if interrupts busy (APRS loop polls registers every frame).
- **Testing & tooling**
  - Build host-side test under `tools/misc/aprs-demod` to run demodulator on stored IQ/AF samples.
  - Add debug UART prints (guarded by `ENABLE_FEAT_F4HWN_DEBUG`) to inspect decoded frames.

## Implementation Stub Checklist
- `App/app/CMakeLists.txt`: add `aprs.c`/`aprs.h` to the build guarded by `ENABLE_FEAT_F4HWN_APRS`.
- Create `App/app/aprs.h` exporting:
  ```c
  #ifdef ENABLE_FEAT_F4HWN_APRS
  void APRS_Enter(void);
  void APRS_Leave(void);
  void APRS_Task(void);
  void APRS_HandleKey(KEY_Code_t key, bool pressed, bool held);
  const AprsPacket_t* APRS_GetPacket(uint8_t idx);
  #endif
  ```
  (structures `AprsPacket_t`, `AprsDecoder_t` stay private to `aprs.c`).
- Create `App/app/aprs.c` skeleton containing:
  - Global state (ring buffer, display cursor).
  - Hooks to configure BK4819 into APRS listen mode (call placeholder `APRS_SetupRadio()`).
  - Empty stubs for demod/decoder functions returning `false` until implemented.
- Update `App/app/main.c`: replace `APP_RunBreakout()` branch with new APRS entry detection for held KEY7 + add exit handling via `APRS_HandleKey`.
- Add `DISPLAY_APRS` constant + `UI_DisplayAprs()` stub (shows "APRS (WIP)" and instructions) so UI compiles even before DSP logic exists.
- Document new command/key flow in README/AGENTS once behavior is defined to avoid regressions.
