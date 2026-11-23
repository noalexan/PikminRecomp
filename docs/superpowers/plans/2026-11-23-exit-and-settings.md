# Exit and Settings Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Quit the game and change settings from inside it: native "Settings" / "Quit Game" entries in Pikmin's title and pause menus, an ImGui overlay for the settings and as an escape hatch from anywhere (Esc / DS4 touchpad), live-applied and saved to `config.toml`.

**Architecture:** Patched menu screens are made at load time (a BLO patcher adds two text items to `m_select.blo`, `m_selec2.blo`, `pause.blo`) and served through a DVD file overlay. Hooks on `zen::DrawMenu` catch the added items and hand them to the overlay (`runtime/ui/`), which draws with aurora's ImGui, blocks guest pad input while open, and applies settings through a small `ui::apply_*` layer that also writes `config.toml`.

**Tech Stack:** C++20 runtime, aurora (Dear ImGui, SDL3, `aurora::input` layers), recompiler hooks (`recomp/overrides.toml`), pytest + offscreen screenshots.

**Spec:** `docs/superpowers/specs/2026-11-23-exit-and-settings-design.md`

## Global Constraints

- Game functions are patched only via `"hook"` in `recomp/overrides.toml`; never edit generated code.
- Every HLE function starts with `HLE_ENTRY`; exits from a fiber use `rt::exit_process()`.
- No overlay in `--headless`; offscreen runs draw it (tests use them).
- Patched screen files exist only in memory; nothing derived from game data is written to disk or git.
- Existing menu items keep their indices and behaviour; `--fast` input scripts already in `build/` and the test suite keep working unchanged.
- No quit while a memory-card file is open.
- Settings table (spec): Window Fullscreen/Windowed (live, F11); Frame rate Display/30/60/120/144/Original (live); V-sync (live); Aspect Widescreen/4:3 (live); HUD size 0.3–1.0 (live); Camera Modern/Original (live); Audio on/off (on restart, labelled).
- Labels: "Settings", "Quit Game" (English in all languages for this version).

## Review Focus

1. **Language other than English** (config `language = 1..4`, `fre_blo` etc.): the patcher must work on each language's file or leave it untouched; a parse failure must fall back to the original bytes with one log line, never crash. Test: Task 2 `test_patch_all_languages`.
2. **Challenge Mode unlocked** (`m_selec2.blo`, 3 native items): added items are indices 3 and 4, not 2 and 3. Test: Task 3 `test_menu_extra_indices` covers both files.
3. **Overlay opened during a cutscene / story message**: the guest must see no buttons while it's open and none "stuck" after closing (the closing press must not reach the game). Test: Task 4 `test_overlay_blocks_pad`.
4. **Quit chosen while the game is saving**: the quit waits until the card file is closed. Test: Task 4 `test_quit_waits_for_card` (C++ unit test against `hle/card.cpp` state).
5. **`config.toml` with comments, unknown keys, or no file at all**: saving keeps everything else byte-identical and creates the file (and directory) if missing. Test: Task 5 `config_write_preserves`.

---

## File Structure

- `runtime/rt/blo.hpp/.cpp` — parse a P2D `.blo`, append menu items, serialize. Pure functions on byte vectors.
- `runtime/hle/dvd.cpp` (+ `rt/disc.hpp`) — file overlay: `rt::set_file_override(entry, bytes)`.
- `runtime/enhance/menu_entries.cpp` — DVD-open hook point that installs patched screens; `DrawMenu` ctor/update hooks.
- `runtime/ui/overlay.hpp/.cpp` — overlay state machine (closed / main / settings / quit-confirm), ImGui drawing, input layer.
- `runtime/ui/settings.hpp/.cpp` — `apply_*` functions and config persistence.
- `runtime/rt/config.cpp/.hpp` — `write_config_value`.
- `runtime/hle/card.cpp` — `rt::card_busy()`.
- `runtime/hle/pad.cpp` — neutral pad while the overlay is open.
- `runtime/rt/input_script.cpp` — pseudo-button `MENU` (touchpad) for tests.
- Tests: `tests/runtime/test_blo.cpp`, `tests/runtime/test_config.cpp` (extend), `tests/runtime/test_card_busy.cpp`, `tests/runtime/test_menu.py`.

---

### Task 1: DVD file overlay

**Files:** Modify `runtime/hle/dvd.cpp`, `runtime/rt/disc.hpp`; Test `tests/runtime/test_disc.cpp` (extend).

**Interfaces:**
- Produces (namespace `rt`): `void set_file_override(int entry, std::vector<uint8_t> bytes)`; `void clear_file_overrides()`; `const std::vector<uint8_t>* file_override(int entry)`. `fill_file_info` reports `bytes.size()` as the length for an overridden entry; `read_into` serves `[offset, offset+len)` from the bytes (zero past the end, like disc reads).
- Note: `fill_file_info` writes `startAddr`/`length` into the guest `DVDFileInfo`; reads address the file by `startAddr + offset`. Overridden reads must be recognised by the file info's entry, not by disc offset (store the entry number in the host-side table keyed by the `DVDFileInfo` address, set in `fill_file_info`).

- [ ] **Step 1: Failing test** `file_override_serves_bytes` in `test_disc.cpp`: open a disc from the test fixture already used there, `set_file_override(e, {1,2,3,4,5})`, call the DVD HLE path used by the existing tests (or `rt::read_file_range` if the existing tests go through Disc directly — add a thin `rt::dvd_read_entry(entry, offset, len)` helper used by `read_into` if needed) and assert length 5, bytes `2,3,4` at offset 1, zeros past the end; `clear_file_overrides()` restores the original bytes.
- [ ] **Step 2:** build + `ctest -R test_disc` → FAIL. **Step 3:** implement. **Step 4:** PASS, full `ctest`.
- [ ] **Step 5: Commit** `feat(dvd): in-memory file overrides`.

### Task 2: BLO patcher

**Files:** Create `runtime/rt/blo.hpp`, `runtime/rt/blo.cpp`; Test `tests/runtime/test_blo.cpp`.

**Interfaces:**
- Produces (namespace `rt::blo`):
  - `struct TextItem { std::string name; std::string text; int16_t x, y, w, h; };`
  - `std::optional<std::vector<uint8_t>> add_menu_items(const std::vector<uint8_t>& blo, const std::vector<std::string>& labels)` — finds the last `hmNN` text box, appends one text box per label named `hm(NN+1)`, … cloned from it and offset by the spacing between the last two `hmNN` items; grows the frame panes as `m_selec2.blo` does relative to `m_select.blo`; returns `nullopt` if the file doesn't parse or has fewer than two `hmNN` items.
  - `std::vector<TextItem> menu_items(const std::vector<uint8_t>& blo)` — for tests and logging.
- Format: work it out from `P2DScreen::makeHiearachyPanes` (`0x801b1474`), the P2DPane/P2DTextBox/P2DPicture constructors, and the files (records start with a u16 tag/size pair, e.g. `0x0010 0x0100 "ROOT"`; text boxes carry font name `sumiw9_2.bfn` and the string with `SH[..]` kerning escapes). Document the layout in a comment at the top of `blo.cpp`. First step of the task is that reading; record it in `docs/superpowers/notes/blo-format.md`.

- [ ] **Step 1: Read the format** (Ghidra `Decompile.java` on the functions above; `xxd` the three files) and write `docs/superpowers/notes/blo-format.md`.
- [ ] **Step 2: Failing tests** in `test_blo.cpp` (skip with exit 77 when `orig/GPIP01/files/dataDir/screen/eng_blo/m_select.blo` is missing; path from a compile definition `PIKMIN_SOURCE_DATA`, already used by `main.cpp`):
  - `menu_items_of_originals`: `m_select.blo` → `hm00 "Start"`, `hm01` (text contains `"O"` and `"ons"`); `m_selec2.blo` → 3 items; `pause.blo` → `hm00 "Continue"`, `hm01`.
  - `add_two_items`: `add_menu_items(m_select, {"Settings","Quit Game"})` → 4 items, `hm02 "Settings"`, `hm03 "Quit Game"`, `hm02.y - hm01.y == hm01.y - hm00.y`, same w/h as `hm01`; re-parsing the output gives the same original items unchanged.
  - `test_patch_all_languages`: for each of `eng fre ger ita spa` and each of the three files, `add_menu_items` returns a value with original count + 2 items (or, if a language file genuinely differs, `nullopt` — the test then asserts the original is untouched and prints which).
  - `garbage_is_rejected`: random bytes and a truncated `m_select.blo` → `nullopt`.
- [ ] **Step 3:** FAIL. **Step 4:** implement. **Step 5:** PASS, full `ctest`.
- [ ] **Step 6: Commit** `feat: BLO patcher for extra menu items`.

### Task 3: Native menu entries

**Files:** Create `runtime/enhance/menu_entries.cpp`; Modify `recomp/overrides.toml`, `runtime/hle/dvd.cpp` (call `menu_entries_on_open`); Test `tests/runtime/test_menu.py`.

**Interfaces:**
- Consumes: `rt::set_file_override` (Task 1), `rt::blo::add_menu_items` (Task 2).
- Produces:
  - `void enh::menu_entries_on_open(int entry, const std::string& path)` — called from `hle_DVDOpen`/`hle_DVDConvertPathToEntrynum` for paths ending in `/m_select.blo`, `/m_selec2.blo`, `/pause.blo`; on first sight reads the original file, patches it, installs the override (or logs `[menu] cannot patch <path>; menu unchanged`).
  - `enum class ui::Request { None, Settings, Quit };` and `void ui::request(Request)` — declared in `runtime/ui/overlay.hpp` (Task 4 implements; this task adds a stub that logs `[menu] settings requested` / `[menu] quit requested`).
- Hooks (`"hook"` in overrides.toml, `f_<addr>_orig` weak):
  - `__ct__Q23zen8DrawMenuFPcbb` (`0x801c184c`): after the original, if the name argument (r4, guest string) ends in one of the three files and its override is installed, remember `this` → native item count (= patched count − 2).
  - `update__Q23zen8DrawMenuFP10Controller` (`0x801c2c24`): after the original, for a remembered menu, if `+0x100 == 0` (finished) and the chosen index (same rule as `ogScrPauseMgr::update`: `+0x110`, or −1 when `+0x1d0 < 0` and `+0x1d4 != 0`) is ≥ native count: call `start__Q23zen8DrawMenuFi(this, index)` (guest call `f_801c2408`) so the menu stays open on that item, then `ui::request(index == native ? Settings : Quit)`.
  - Find in Ghidra whether a menu is destroyed and its address reused (`__dt__`); if so hook it to forget `this`.

- [ ] **Step 1: Failing test** `test_menu.py::test_title_menu_has_entries` (offscreen, `--fast`, skip without data/GPU): script `300 START / 303 - / 400 A / 403 -` (opens the Start/Options menu, as `build/s1.txt`), screenshot at 380; assert `[menu] patched screen/blo/m_select.blo (4 items)` in stderr and save the PNG as `build/test_menu_title.png` for the human check in Step 6.
- [ ] **Step 2:** `test_menu_extra_indices`: same start, then `410 - 0 -127 / 415 - 0 0 / 420 - 0 -127 / 425 - 0 0 / 430 A / 433 -` (stick down twice; confirm the sign against `build/s3.txt`, where `0 127` moves the title cursor to the 3rd item, A) → stderr contains `[menu] settings requested` and the game did **not** start a new file (no `[scene] OnePlayerSection::init` before frame 600). A second test with one more stick-down → `[menu] quit requested`. (Pause-menu coverage is by the human check in Step 6; reaching gameplay in a test takes ~9000 frames.)
- [ ] **Step 3:** FAIL. **Step 4:** implement hooks + `menu_entries.cpp`. **Step 5:** PASS; full `pytest -q` (existing title-screen tests unchanged).
- [ ] **Step 6: Human check:** `tools/peek.sh` with the Step 2 script at 425 and with `build/s6.txt` + `9800 START`-style pause (find a pausable frame) — look at both screenshots; record in the commit message.
- [ ] **Step 7: Commit** `feat: Settings and Quit Game entries in the title and pause menus`.

### Task 4: Overlay

**Files:** Create `runtime/ui/overlay.hpp`, `runtime/ui/overlay.cpp`; Modify `runtime/CMakeLists.txt` (glob `ui/*.cpp` when `PIKMIN_GRAPHICS`), `runtime/rt/gfx.cpp` (call `ui::frame()` between `aurora_begin_frame` and the next present), `runtime/hle/pad.cpp`, `runtime/hle/card.cpp`, `runtime/rt/input_script.cpp`; Test `tests/runtime/test_card_busy.cpp`, `tests/runtime/test_menu.py`.

**Interfaces:**
- Consumes: `ui::Request` (Task 3).
- Produces (namespace `ui`):
  - `void init()` (after `gfx_init`, window or offscreen), `void frame()` (build the ImGui frame; no-op when closed), `bool is_open()`, `void open(Page)`, `void close()`, `enum class Page { Main, Settings, QuitConfirm }`; `request(Settings)` → `open(Page::Settings)`, `request(Quit)` → `open(Page::QuitConfirm)`.
  - `bool rt::card_busy()` in `hle/card.cpp`: true between a successful `CARDOpen`/`CARDCreate` and `CARDClose` (count open handles).
- Input: an `aurora::input` layer at priority `kGameLayerPriority + 100`, enabled only while open, `pointerMode` → `Visible`, `capturesSource` → true for keyboard/mouse/gamepads (so aurora's pad reads see nothing); `onEvent` opens/closes on Esc and the gamepad touchpad button (check the event kinds in `aurora/input.hpp`). Opening needs a layer that is always enabled but captures nothing (a second, passive layer) to see Esc/touchpad while closed.
- `hle/pad.cpp`: while `ui::is_open()` and for 2 retraces after closing, report all ports as connected with no buttons and centred sticks/triggers.
- ImGui: enable `ImGuiConfigFlags_NavEnableGamepad | NavEnableKeyboard`; if aurora's backend doesn't feed gamepad state, feed `io.AddKeyEvent(ImGuiKey_Gamepad*)` from the SDL gamepad in `frame()`. Style: dark green/olive panel, rounded, scaled ×1.5 relative to 1080p (`io.FontGlobalScale` from window height).
- Quit: `QuitConfirm` Yes → if `card_busy()`, show "Saving…" and re-check each frame; else `rt::log("ui", "quit")` then `rt::exit_process(0)`.
- Input script: pseudo-button `MENU` toggles the overlay as the touchpad would (test hook; `ui::toggle_from_script()`).

- [ ] **Step 1: Failing tests:**
  - `test_card_busy.cpp::test_quit_waits_for_card`: with a temp `g_card_dir`, drive `hle_CARDOpen`-style calls through `hle_test_util.hpp` (as the other HLE tests do): `card_busy()` false → after create/open true → after close false.
  - `test_menu.py::test_overlay_blocks_pad`: offscreen `--fast`, script `250 MENU / 253 - / 300 START / 303 - / 340 MENU / 343 -`, `PIKMIN_TRACE=hle`: START at 300 must not reach the game (no title-menu open: no `[trace] hle ...` for the menu-open path — simpler: screenshot at 330 shows the overlay and the title still says PRESS START; at 400 still PRESS START). Assert via log lines `[ui] open main` / `[ui] close` and a pad-read counter log `[ui] blocked N pad reads` with N > 0.
  - `test_menu.py::test_settings_entry_opens_overlay`: Task 3's settings script → `[ui] open settings`.
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS; full suite.
- [ ] **Step 5: Human check:** real window (`fullscreen = false` config), Esc opens/closes, touchpad opens, controller navigates, Quit → confirm → exits with code 0; screenshot with `spectacle -b -n -a`.
- [ ] **Step 6: Commit** `feat: in-game overlay with Resume, Settings and Quit`.

### Task 5: Settings: live apply and save

**Files:** Create `runtime/ui/settings.hpp/.cpp`; Modify `runtime/rt/config.hpp/.cpp`, `runtime/rt/gfx.cpp` (F11, window mode), `runtime/ui/overlay.cpp` (Settings page), `runtime/main.cpp` (pass the config path to `ui::init`); Test `tests/runtime/test_config.cpp`.

**Interfaces:**
- Produces:
  - `std::string rt::write_config_value(const std::string& text, const std::string& key, const std::string& value)` — replaces the value of `key = ...` keeping its trailing comment, or appends `key = value\n`; values are written in the parser's syntax (strings quoted).
  - `bool rt::save_config_value(const std::string& path, const std::string& key, const std::string& value)` — read (missing → empty), `write_config_value`, create parent dirs, write to `path.tmp`, rename.
  - `ui::apply_window(bool fullscreen)`, `ui::apply_fps(int target_fps)`, `ui::apply_vsync(bool)`, `ui::apply_aspect(bool four_three)`, `ui::apply_hud_scale(float)`, `ui::apply_camera(bool modern)`, `ui::set_audio(bool)` — each updates the live state (`enh::settings()`, `AuroraSetViewportPolicy`, `aurora_enable_vsync`, `SDL_SetWindowFullscreen`) and calls `save_config_value` with the config keys `fullscreen`, `target_fps` (`"auto"`/`"original"`/N), `vsync`, `aspect` (`"auto"`/`"4:3"`), `hud_scale`, `camera` (`"modern"`/`"original"`), `audio`.
- Command-line overrides (`--aspect` etc.) apply to the run only; the panel shows the live value and saving writes only the key that was changed.

- [ ] **Step 1: Failing tests** in `test_config.cpp`:
  - `config_write_preserves`: input `"# mine\ndisc = \"/x.iso\"  # keep\nmystery = 7\nhud_scale = 0.5\n"`; set `hud_scale` to `0.7` → only that line changes (`hud_scale = 0.7`); set `aspect` to `4:3` → appended `aspect = "4:3"`; set `disc` to `/y.iso` → `disc = "/y.iso"  # keep`; parsing the result gives the new values and still warns about `mystery`.
  - `save_creates_file`: `save_config_value(tmp/"a/b/config.toml", "vsync", "false")` creates dirs and file containing `vsync = false`.
- [ ] **Step 2:** FAIL. **Step 3:** implement config functions, then `settings.cpp` and the Settings page (one row per setting; left/right changes value; "applies on restart" next to Audio). **Step 4:** PASS; full suite.
- [ ] **Step 5: Human check:** each setting changes live in a real window; quit and relaunch keeps them; F11 toggles; `config.toml` diff shows only changed keys.
- [ ] **Step 6: Commit** `feat: settings panel with live apply and config.toml saving`.

### Task 6: Docs

- [ ] Update `CLAUDE.md` (Enhancements: menu entries, overlay, settings, F11, `MENU` script pseudo-button, file overrides; Architecture: `runtime/ui/`), README-level note in the config section. Add `docs/superpowers/notes/menu-entries.md` (why DrawMenu indices are intercepted, why the game isn't frozen under the overlay). **Commit** `docs: exit and settings menu`.
