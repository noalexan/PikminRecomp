# Exit and Settings Menu — Design

## Why

The game starts fullscreen and the only way out is closing the window, which a fullscreen game
played with a pad doesn't offer. Settings (window mode, frame-rate cap, …) can only be changed by
editing `config.toml`. The user wants to quit and change settings from inside the game, in a way
that feels like part of Pikmin where the game already has menus.

## Goals

- **Quit Game** and **Settings** entries in Pikmin's own menus: the title menu (after Start /
  Options, and after Challenge Mode when it is unlocked) and the in-game pause menu (after
  Continue / Go to Sunset). Same font, cursor, sounds and layout as the existing entries.
- A **settings panel** drawn over the game (Dear ImGui, which aurora already bundles), usable
  entirely with the controller and with keyboard/mouse.
- An **escape hatch from anywhere** (story messages, cutscenes, loading, file select): **Esc** or
  the **DS4 touchpad click** opens the overlay with Resume / Settings / Quit Game.
- Settings apply immediately where possible and are saved to `config.toml`.
- **F11** toggles fullscreen.
- The mouse pointer stays hidden in play (done, `193ad53`) and shows while the overlay is open.

## Non-goals

- A settings screen drawn in Pikmin's own style (only the menu *entries* are native).
- Remapping controls, graphics quality options, save management.
- Changing settings that need a restart without saying so: those are marked "applies on restart".

## Anti-goals

- Shipping modified game data: the extra menu entries are made at run time from the user's own
  disc files and never written to disk.
- Changing what the game computes outside the new entries: existing menu items keep their
  indices, behaviour and timing; `--fast` input scripts and tests keep working unchanged.
- Quitting in the middle of a memory-card write.

## Constraints

- Game code is patched only through `"hook"` entries in `recomp/overrides.toml` (CLAUDE.md).
- Every HLE function starts with `HLE_ENTRY`; exits from a fiber use `rt::exit_process()`.
- The overlay must not exist in `--headless` runs; offscreen runs may draw it (for tests).
- Game data never goes in git; tests needing it skip without it.

## Approaches considered

- **A. Overlay only** (Esc/touchpad opens an ImGui menu). Cheap, works everywhere, but looks like
  a PC tool.
- **B. Hotkeys only.** Tiny, but undiscoverable, and settings stay in a file.
- **C. Native entries only.** Looks right, but a settings *screen* in the game's style means a new
  P2D screen layout and widgets: many times the work.
- **Chosen: A + C.** Native entries where the game has menus, opening the overlay for the settings
  themselves; the overlay is also the escape hatch everywhere else.

## How it works

### Native entries (title and pause menus)

Recon findings (Ghidra, `screen/<lang>_blo/*.blo`):

- `zen::DrawMenu(const char* blo, bool, bool)` (`0x801c184c`) builds its items by looking up
  panes named with a `sprintf` pattern (`hm00`, `hm01`, …) until one is missing. Items are
  `P2DTextBox` panes in font `sumiw9_2.bfn`, not pictures.
- The title menu is `screen/blo/m_select.blo` (Start, Options) or `m_selec2.blo` (Start, Options,
  Challenge Mode) — the game's own 3-item variant shows how extra items are laid out.
- The pause menu (`zen::ogScrPauseMgr`, `pause.blo`) reads the chosen index from `DrawMenu+0x110`
  once `DrawMenu+0x100` is 0, and acts on 0 (Continue) and 1 (Go to Sunset), plus 2 in a
  variant.
- Screen files resolve per language (`eng_blo`, `fre_blo`, `ger_blo`, `ita_blo`, `spa_blo`).

Design:

- **Patched screen files, made at load time.** A host-side BLO patcher reads the original file,
  appends two text-box items after the last `hmNN` (cloned from it: same font, colours, size;
  positioned at the original item spacing; next names; labels "Settings" and "Quit Game", per
  language), and grows the frame panes the way `m_selec2.blo` does relative to `m_select.blo`.
- **A DVD overlay serves the patched bytes**: `hle/dvd.cpp` gets a map from FST entry to
  replacement bytes; `DVDOpen`/`DVDFastOpen` report the replacement's length and reads come
  from it. Nothing is written to disk.
- **Selections are intercepted at the menu**: hooks on `DrawMenu`'s constructor (remember which
  instances were built from a patched file and how many items are native) and on
  `DrawMenu::update`: when the menu finishes on an added item, the hook re-opens the menu on
  that item (so the game never sees an index it doesn't know) and asks the overlay to open
  Settings or the Quit confirmation.

### Overlay (`runtime/ui/`)

- ImGui frame built each presented frame (aurora calls `ImGui::NewFrame` in `aurora_begin_frame`
  when a window surface exists). Styled with Pikmin-like colours; the game font if it can be
  converted cheaply, otherwise ImGui's default at a readable size.
- Pages: **Main** (Resume, Settings, Quit Game), **Settings**, **Quit confirmation** ("Quit
  Pikmin? Unsaved progress since the last save is lost." Yes / No).
- Opening: Esc, the DS4 touchpad click, or the native entries (F11 stays fullscreen only).
  Closing: Resume, B/Esc on the main page, or the same open button.
- While open: the guest sees a neutral pad (no buttons, centred sticks) so nothing leaks into
  the game; an input layer above the game layer prefers a visible pointer. The game keeps
  running underneath (the native pause already freezes gameplay; elsewhere the escape hatch is
  for quitting). *My call: freezing the guest would need re-presenting a stale frame.*
- Quit: if a memory-card operation is in progress, "Saving…" is shown and the quit waits for it;
  then `rt::exit_process(0)`.

### Settings

| Setting | Values | Applies |
|---|---|---|
| Window | Fullscreen (borderless desktop) / Windowed | live (`SDL_SetWindowFullscreen`), F11 toggles |
| Frame rate | Display refresh / 30 / 60 / 120 / 144 / Original (25) | live (`enh::settings().target_fps`) |
| V-sync | on / off | live (`aurora_enable_vsync`) |
| Aspect | Widescreen / 4:3 | live (viewport policy + widescreen state) |
| HUD size | 0.3 – 1.0 | live |
| Camera | Modern / Original | live |
| Audio | on / off | on restart (stated in the panel) |

Saved to `config.toml` by a writer that changes only the edited keys and keeps comments, unknown
keys and order. A config file given with `--config` is the one written.

## Left to the builder

- The BLO binary layout details (header, pane records, text-box record) — read from
  `P2DScreen::makeHiearachyPanes` / `P2DTextBox` construction and the files themselves.
- Exact label strings per language (English everywhere is acceptable for a first version; my
  call).
- The DrawMenu frame/cursor panes that must grow (`pall`, `yoko`, `z00l…` dots): compare
  `m_select.blo` with `m_selec2.blo`.
- How to detect a memory-card write in progress (`hle/card.cpp` state).
- Which aurora input events carry Esc and the touchpad button (the overlay's `aurora::input`
  layer receives them), and how ImGui gets controller navigation (aurora's ImGui setup; enable
  `ImGuiConfigFlags_NavEnableGamepad` and feed pad state if the backend doesn't).
