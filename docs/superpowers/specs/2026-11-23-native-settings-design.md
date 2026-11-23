# Native Settings Screens — Design

Supersedes the overlay half of `2026-11-23-exit-and-settings-design.md` (the Dear ImGui panel and
the Esc / touchpad escape hatch). The native menu entries, the BLO patcher and the DVD/archive file
overrides from that design stay.

## Why

The user wants the settings *in the game*: drawn by Pikmin's own menu code, with its font, frames,
cursor sparkles and sounds, not a PC-style panel on top. The game already has an Options menu, so
the new settings join it instead of being a separate "Settings" entry.

## Goals

- **One Options menu.** Title: `Start / Options / Quit Game` (and `Challenge Mode` before
  Quit Game when unlocked). The separate "Settings" entry goes.
- **Options gains two screens**, after the game's own:
  `Sound · Language · Rumble · High Scores · Display · Game`.
- **Display and Game look like the Sound screen**: three rows each, choices shown as dots, a dot bar
  for a range. Left/right changes the value; it applies immediately; leaving the screen saves it.

  | Screen | Row | Values | config key |
  |---|---|---|---|
  | Display | Window | ● Fullscreen ● Windowed | `fullscreen` |
  | Display | Aspect | ● Wide ● 4:3 | `aspect` (`"auto"` / `"4:3"`) |
  | Display | V-sync | ● On ● Off | `vsync` |
  | Game | Frame rate | ● Original ● 60 ● Max | `target_fps` (`"original"` / `60` / `"auto"`) |
  | Game | Camera | ● Modern ● Original | `camera` |
  | Game | HUD size | bar of 10 dots = 0.1 … 1.0 | `hud_scale` |

- **Pause menu:** `Continue / Go to Sunset / Options / Quit Game`. Pause Options offers
  `Sound · Rumble · Display · Game`.
- **Quit Game** asks first ("Quit Pikmin?" Yes / No) in the game's own style, defaulting to No;
  if a memory-card file is open it waits for it to close before exiting.
- **F11** toggles fullscreen (kept; cheap, and the Display screen reflects it).

## Non-goals

- Every value of the old settings table: frame-rate 30/120/144 and the Audio on/off toggle are
  dropped (the Sound screen's volumes cover muting). `config.toml` still accepts them; a value the
  screen can't show displays as the nearest dot (60 for 120/144, Original for 30) and is only
  rewritten if the user changes that row.
- Language and High Scores from the pause menu (changing language mid-game would need every loaded
  screen reloaded).
- A menu reachable from cutscenes, story text, loading or file select. Esc and the DS4 touchpad
  act as **Start** (gameplay: opens pause; elsewhere whatever Start does there).
- Translations: the new labels are English in every language (as before).
- Controls remapping, graphics quality options.

## Anti-goals

- Dear ImGui anywhere in the game's UI.
- Shipping or writing modified game data: new screens are built in memory from the user's disc.
- Changing what the game computes outside the new items: native items keep their indices and
  behaviour, the game's own options still save to the memory card as before, `--fast` input
  scripts and the existing tests keep working.
- Quitting in the middle of a memory-card write.

## Constraints

- Game code changes only through `"hook"` entries in `recomp/overrides.toml`.
- Every HLE entry point starts with `HLE_ENTRY`; exits use `rt::exit_process()`.
- `--headless` runs the menus too (they are game code) but applying a window setting is a no-op.
- Game data never in git; tests needing it skip without it.

## Approaches considered

- **A. Sound-style screens built from `o_sound.blo` (chosen).** Each new screen is a patched copy of
  the Sound screen, run as a `zen::DrawMenu` like the game's own; our hook does what
  `ogScrTitleMgr::update` does for Sound (left/right → value, show it by toggling dot panes).
  Most reuse, looks identical. Costs a few dropped values (three rows, up to three choices).
- **B. One screen with every setting** as `Label ◀ value ▶` text rows. Keeps every value, but a new,
  taller layout and rewritten text that the game never does.
- **C. Our own drawing** with the game's font and textures. Most freedom, most code, and easiest to
  get subtly wrong.

The user picked A.

## How it works

### Recon (what the game does today)

- `zen::ogScrTitleMgr` owns six `DrawMenu`s: `+0x0c/+0x10` the title menu (`m_select` /
  `m_selec2`), `+0x14` the current one of those, `+0x18` Options (`o_select`), `+0x1c` Sound
  (`o_sound`), `+0x20` Rumble, `+0x24` Language. `+0x98` (s16) is the sub-mode: 0 title, 1 Options
  list, 2 Sound, 3 Rumble, 4 Language. `update` dispatches on it.
- Options list (mode 1): index 0 → Sound, 1 → Language, 2 → Rumble, 3 → High Scores (leaves the
  title), B → writes `GamePrefs` and `MemoryCard::saveOptions`, back to mode 0.
- Sound (mode 2): the menu's current item is the row; `Controller` bits `0x400000` / `0x100000`
  (left/right repeat) change stereo, BGM 0–10, SFX 0–10 through `GamePrefs` setters and play
  system SE `0x133`; `StereoOnOff` / `DispBarBGM` / `DispBarSE` show the values by toggling the
  `on01…on22` dot panes. Finishing (A or B) returns to mode 1.
- `o_sound.blo`: rows are `hm00…hm02` (text, inside `yoko`) with top-level `he00…he02`; the choice
  dots are pictures `on21`/`on22`, the bars `on01…on10`, `on11…on20`; title `titl`/`titm`; frame
  `pall`. **The game reads screens from `archives/blo__<lang>.arc`; the loose `screen/*_blo` copies
  differ (the loose English `o_sound.blo` has an Italian title)** — always build from the archive.

### Screens

- `rt::blo` grows the edits needed to turn a copy of `o_sound.blo` into `o_disp.blo` / `o_game.blo`:
  replace the text of named text boxes (titles, row labels, choice labels), and add picture panes
  cloned from an existing dot (for the third Frame-rate choice). Same fallback as today: a parse or
  edit failure leaves that screen out and logs one line; the Options list then doesn't get its item.
- `o_select.blo` gets two items (Display, Game) with the existing `add_menu_items`; `m_select` /
  `m_selec2` get one (Quit Game) instead of two; `pause.blo` gets two (Options, Quit Game).
- New file names are added to each language's archive and `.dir` in memory (`rt::arc` gains
  *add*, next to *replace*), served through `rt::set_file_override`.
- A pause Options list (`o_pause.blo`, my call on the name): a copy of `o_select.blo` with the items
  Sound, Rumble, Display, Game.

### Driving them (`runtime/enhance/settings_menu.cpp`)

- **Title.** Hook `ogScrTitleMgr::__ct` to also construct our DrawMenus (guest `System::alloc` +
  `DrawMenu::__ct` with our file names), and `ogScrTitleMgr::update` / `draw`:
  - In mode 1, when the Options list finishes on index 4/5, switch to our own sub-modes
    (5 Display, 6 Game, values outside the game's range) and `start` our menu.
  - In modes 5/6 the hook runs our menu instead of calling the original: update the DrawMenu,
    apply left/right to the row's setting (same repeat bits, same SE `0x133`), refresh the dots,
    and on finish save the changed keys and go back to mode 1 with the cursor on our item.
  - `draw` draws our menu in modes 5/6 (and otherwise calls the original).
- **Pause.** Hook `ogScrPauseMgr` update / draw the same way: the pause menu finishing on Options
  opens the pause Options list; from it Sound and Rumble run our copy of the game's Sound / Rumble
  logic (same `GamePrefs` setters, saved with `MemoryCard::saveOptions` when the list closes, as the
  title does), Display / Game run the shared screen code. B from the list returns to the pause menu.
- **Quit Game** (title or pause): a Yes/No confirmation in the game's style. First choice: the
  game's own two-choice window (`zen::ogNitakuMgr`) if it can be driven standalone; otherwise a
  two-item DrawMenu screen built from `pause.blo` ("Quit Pikmin?" / Yes / No). Yes → wait while
  `rt::card_busy()` → `rt::exit_process(0)`.
- The existing `DrawMenu::update` hook keeps re-opening a menu that finishes on an added item, so
  the game never sees an index it doesn't know; the new code takes over from there.

### Settings layer (`runtime/ui/settings.*`, from the old Task 5, minus ImGui)

- `apply_window / apply_aspect / apply_vsync / apply_fps / apply_camera / apply_hud_scale` update
  live state (`enh::settings()`, viewport policy, `aurora_enable_vsync`, `SDL_SetWindowFullscreen`).
- `rt::write_config_value` / `save_config_value` change only the edited keys, keeping comments,
  unknown keys and order; create the file if missing. Command-line overrides apply to the run only;
  a key is written only when its row was changed.

### What happens to the uncommitted overlay work

- Removed: `runtime/ui/overlay_draw.cpp`, the ImGui pages, the `MENU` input-script pseudo-button,
  the pad blocking in `hle/pad.cpp` (native menus own their input).
- Kept: `rt::card_busy()` in `hle/card.cpp` and its test; the quit-waits-for-card logic moves into
  the quit confirmation.
- Esc / touchpad: added to the default layouts as Start (`rt/pad_layout.cpp`).

## Testing

- Unit (`test_blo.cpp`): building `o_disp` / `o_game` / `o_pause` from every language's archive copy
  gives the expected items and dot panes; garbage and truncated input → no screen.
- Unit (`test_config.cpp`): config writer keeps comments / unknown keys; creates missing file.
- Headless `--fast` (`test_menu.py`): Options → Display → right on Window logs the change and
  writes `fullscreen = false` to the test's config; B returns to the Options list with the cursor
  on Display; native Options items still behave (Sound changes BGM volume as before); title Quit →
  No returns to the menu, Yes exits 0.
- Screenshots (offscreen, human check): Options list with the two new items, Display and Game
  screens, pause menu with Options / Quit Game, the quit confirmation.

## Left to the builder

- Exact pane edits per screen (label positions when "Fullscreen" is wider than "Stereo"; the third
  dot's position for Frame rate) — compare against a screenshot of the Sound screen.
- How `ogScrPauseMgr` sequences its menus and draws (Ghidra), and where the pause Options list sits
  on screen relative to the pause frame.
- Whether `ogNitakuMgr` works standalone; the fallback is described above.
- The Rumble screen's logic for the pause copy (`ogScrTitleMgr::update` mode 3).
