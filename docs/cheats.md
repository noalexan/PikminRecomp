# Pikmin Recomp — cheat sheet

## Opening the cheat menu

| Input | Action |
| --- | --- |
| **F1** | open / close |
| **L3 + R3** (click both sticks) | open / close |
| **B** or **Esc** | close |
| D-pad / left stick, **A** | move, choose (mouse works too) |

While the menu is open, the game doesn't see the controller. Buttons still held when it closes are
passed on only once released, so closing with B doesn't whistle and Esc doesn't pause. Cheats work
during a day, not in menus or the title screen. They reset when the game is restarted.

## Cheats

| Cheat | What it does | Notes |
| --- | --- | --- |
| **Spawn pikmin** | Adds pikmin of the chosen **color** (Blue, Red, Yellow), **count** (1, 5, 10, 25, 50) and **type** (Leaf, Bud, Flower) around Olimar, straight into his squad. | The game's limit of 100 pikmin on the field still applies; extra ones aren't made. The HUD total (third number) only counts pikmin grown the normal way. |
| **All pikmin to flowers** | Every pikmin on the field becomes a flower pikmin (the fastest). | One-shot. |
| **Invincible Olimar** | Olimar's health stays full. | His pikmin can still be hurt. |
| **Freeze the day timer** | The sun meter stops: the day lasts as long as you want. | Turn it off to let the day end. |
| **Fly** | Olimar hovers and keeps walking in the air. **D-pad up / down** raises / lowers him. | While flying, the D-pad goes to the cheat, not the game. He takes off 40 units above where he stood. |

## Related shortcuts

| Input | Action |
| --- | --- |
| **Start** during a cutscene | skip it (fast-forward until it ends) |
| **F3** | performance overlay |
| **F11** | fullscreen on / off |

Implementation: `runtime/enhance/cheats.cpp` (the cheats, run at the top of each game frame) and
`runtime/ui/cheat_menu.cpp` (the Dear ImGui menu).
