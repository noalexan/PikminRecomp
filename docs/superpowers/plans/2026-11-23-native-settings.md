# Native Settings Screens Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Settings live in Pikmin's own Options menu (Display and Game screens in the style of the
Sound screen), reachable from the title and pause menus; Quit Game asks Yes/No natively; no ImGui.

**Architecture:** Screens are built in memory from the archive copies of `o_sound.blo` /
`o_select.blo` (`rt::blo` edits, `rt::arc` add). `enhance/settings_menu.cpp` hooks
`ogScrTitleMgr` and `ogScrPauseMgr` (ctor/update/draw) to run extra `zen::DrawMenu`s; values go
through `ui/settings.*`, which applies them live and writes `config.toml`.

**Tech Stack:** C++20 runtime, recompiler hooks, pytest headless/offscreen runs.

**Spec:** `docs/superpowers/specs/2026-11-23-native-settings-design.md`

## Global Constraints

- Game code is patched only through `"hook"` in `recomp/overrides.toml`.
- New labels English in all languages: "Options", "Quit Game", "Display", "Game", "Window",
  "Fullscreen", "Windowed", "Aspect", "Wide", "4:3", "V-sync", "On", "Off", "Frame rate",
  "Original", "60", "Max", "Camera", "Modern", "HUD size", "Quit Pikmin?", "Yes", "No".
- Config keys/values: `fullscreen` bool, `aspect` `"auto"`/`"4:3"`, `vsync` bool, `target_fps`
  `"original"`/`60`/`"auto"`, `camera` `"modern"`/`"original"`, `hud_scale` 0.1…1.0.
- Native item indices unchanged; existing tests keep passing.
- Quit waits while `rt::card_busy()`.

## Review Focus

1. Non-English language (`fre`/`ger`/`ita`/`spa` archives): screens build or are left out with one log line — covered by `test_blo` over all languages.
2. Challenge Mode unlocked (`m_selec2`): Quit Game is index 3 — covered by the existing `test_menu_extra_indices` pattern (updated).
3. Config with comments/unknown keys: only changed keys rewritten — `test_config` `config_write_preserves`.
4. Pausing mid-game, opening Options, returning: the game resumes with no stuck input — human check (reaching gameplay in a test is ~9000 frames).
5. Quit while saving: waits for `card_busy()` — existing `test_card_busy` kept.

---

### Task 1: Drop the ImGui overlay, keep card_busy

**Files:** Delete `runtime/ui/overlay_draw.cpp`, `tests/runtime/test_overlay.cpp`; reduce `runtime/ui/overlay.*`
to nothing used (remove), revert pad blocking and `MENU` in `hle/pad.cpp`, `rt/input_script.*`,
`rt/gfx.cpp`, `main.cpp`; keep `hle/card.cpp` `card_busy()`. Esc and touchpad → Start in
`rt/pad_layout.cpp`.

- [ ] Remove, build, `ctest` + `pytest -q tests/runtime` green (overlay tests deleted).

### Task 2: Config writer

**Files:** `runtime/rt/config.{hpp,cpp}`, test `tests/runtime/test_config.cpp`.
- Produces: `std::string rt::write_config_value(const std::string& text, const std::string& key, const std::string& value)`; `bool rt::save_config_value(const std::string& path, const std::string& key, const std::string& value)`. `value` is already in TOML syntax (`"4:3"` quoted by the caller).
- [ ] Test `config_write_preserves`: `"# mine\ndisc = \"/x.iso\"  # keep\nmystery = 7\nhud_scale = 0.5\n"` → set `hud_scale`=`0.7` changes only that line; set `aspect`=`"4:3"` appends `aspect = "4:3"`; set `disc`=`"/y.iso"` keeps `  # keep`. Implement; pass.

### Task 3: Screens (`rt::blo`, `rt::arc`)

**Files:** `runtime/rt/blo.{hpp,cpp}`, `runtime/rt/arc.{hpp,cpp}`, test `tests/runtime/test_blo.cpp`.
- Produces: `bool rt::blo::set_text(std::vector<uint8_t>&, const std::string& pane, const std::string& text)`; `bool rt::blo::clone_picture(std::vector<uint8_t>&, const std::string& src, const std::string& name, int16_t dx, int16_t dy)`; `bool rt::blo::rename(...)`/item removal as needed; `bool rt::arc::add(std::vector<uint8_t>& arc, std::vector<uint8_t>& dir, const std::string& name, const std::vector<uint8_t>& bytes)`.
- Builders in `enhance/settings_screens.cpp`: `std::optional<std::vector<uint8_t>> make_display_screen(sound)`, `make_game_screen(sound)`, `make_pause_options(select)`, `make_quit_screen(pause)`.
- [ ] Test `settings_screens_all_languages`: for each language archive, each builder returns a screen whose `menu_items` are 3 rows (2 for quit) with the expected labels. Implement; pass.

### Task 4: Settings layer

**Files:** Create `runtime/ui/settings.{hpp,cpp}`.
- Produces (namespace `ui`): `enum class Setting { Window, Aspect, Vsync, Fps, Camera, Hud }`; `int value(Setting)`; `int choices(Setting)` (2, 2, 2, 3, 2, 10); `void set(Setting, int)` applies live + marks dirty; `void save()` writes dirty keys via `rt::save_config_value` to the run's config path; `void init(const std::string& config_path)`.
- No separate test (covered by Task 5 runs).

### Task 5: Menu hooks

**Files:** `runtime/enhance/menu_entries.cpp` (title gets one item, pause two, `o_select` two), create
`runtime/enhance/settings_menu.cpp`, `recomp/overrides.toml` hooks on `ogScrTitleMgr` ctor/update/draw and `ogScrPauseMgr` ctor/update/draw; test `tests/runtime/test_menu.py`.
- [ ] Update `test_menu.py`: title has Start/Options/Quit (quit at index 2 → `[menu] quit confirm`); Options → down 4 → A logs `[settings] open display`; right → `[settings] window = windowed` and config file gets `fullscreen = false`; quit → Yes exits 0.
- [ ] Implement; pass; offscreen screenshots of Options list, Display, Game, quit confirm (human check).
- [ ] Commit.

### Task 6: Docs
- [ ] CLAUDE.md Enhancements section + `docs/superpowers/notes/settings-menu.md`. Commit.
