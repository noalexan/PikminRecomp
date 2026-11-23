# In-game settings and quit: notes

- The title's Options list is `option.blo`, Sound is `s_select.blo`, Rumble `v_select.blo`, Language
  `ms_selec.blo` (not the `o_*.blo` files, which are unused leftovers). Screens load from
  `archives/blo__<lang>.arc`; the loose `screen/*_blo` copies are not read.
- `option.blo` keeps `he03` inside `yoko` (relative coordinates) while `he00..he02` are top level;
  `rt::blo::add_menu_items` converts nested heNN to absolute positions.
- Other languages lay out `s_select.blo` differently (named `hmNN`, two-line labels), so the
  Display / Game screens are built from the English one everywhere (their labels are English).
- An added item must never reach the game: `m_select`'s third index means Challenge Mode to
  `ogScrTitleMgr::update`. The `DrawMenu::update` hook records the choice and writes 0x7fff as the
  result; title and pause code ignore that index, and our manager hooks open the screen.
- Sound screen dots: `zen::setTextColor(on_c | offc, picture)` and alpha `+0xf0 = 0xff`, as
  `StereoOnOff` / `DispBar*` do (the blink on the active dot is left out). Left/right come from a
  `zen::ZenController` (`+4` bits 0x400000 / 0x100000, with key repeat); the pause manager has none,
  so one is allocated in its constructor hook.
- The pause manager, finished on an unknown index, idles in state 0 with its menu closed; our
  screens draw over it and `DrawMenu::start(menu, -1)` brings it back. Pause-time Sound/Rumble
  changes are written with `MemoryCard::saveOptions` when the Options list closes, as the title does.
