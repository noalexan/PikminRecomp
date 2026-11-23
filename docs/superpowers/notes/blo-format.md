# P2D screen files (.blo)

Read by `P2DScreen::set` → `P2DScreen::makeHiearachyPanes` (`0x801b1474`) from
`screen/<lang>_blo/*.blo`. Big-endian, read sequentially; alignment is to the absolute file offset.

## Stream

A sequence of u16 tags:

| tag | meaning |
|---|---|
| `0x0000` | end of file (top level) |
| `0x0001` + u16 | the previous pane's children follow (recursive) |
| `0x0002` + u16 | end of children |
| `0x0010` | `P2DPane` (16 bytes: the common header only) |
| `0x0011` | `P2DWindow` (not used by the menu screens; the patcher rejects it) |
| `0x0012` | `P2DPicture` |
| `0x0013` | `P2DTextBox` |

## Pane header (all pane tags, `P2DPane(P2DPane*, RandomAccessStream*, u16)` `0x801af27c`)

`u16 tag, u8 visible, u8 pad, char name[4], s16 x, s16 y, s16 w, s16 h` (w/h become x+w, y+h).
Positions are relative to the parent pane.

## Resources (`P2DStream::getResource`)

`u8 kind, u8 length`, then `length` name bytes when kind is 0 or 2 (e.g. `02 0c "sumiw9_2.bfn"`).

## Picture (`0x801b018c`)

header, resource TIMG (texture), resource TLUT (palette), u8 binding, u8 flags (mirror/rotate/wrap),
align 4.

## Text box (`0x801b2c7c`)

header, resource FONT, u32 RGBA colour (top), u32 RGBA colour (bottom), u8 flags (bit 7: four s16
spacing/size values follow; low 7 bits: alignment), u8, [4 × s16], s16 text length, text bytes
(no terminator; `\x1bSH[-n]` kerning escapes, `\n` line breaks), align 4.

## Menus (`zen::DrawMenu`, `0x801c184c`)

Items are found by name with `sprintf`: `he%02d` (counted from 00 until one is missing — the item
count), `hm%02d` (text), optional `i%02dl` / `i%02dr` (side icons), and `z%02dl` / `z%02dr` (cursor
sparkle path, counted separately). `hmNN` sit in the `yoko` group (relative positions); `heNN`
are top-level copies at absolute positions. `pall` is the frame, sized by `MenuPanelMgr`.

| file | items |
|---|---|
| `m_select.blo` | Start, Options |
| `m_selec2.blo` | Start, Options, Challenge Mode (items re-spaced 47 → 39 px) |
| `pause.blo` | Continue, Go to Sunset (in `yoko`), Continue from Last Save (`hm02`, top level) |

All 15 menu files (3 × eng/fre/ger/ita/spa) parse with this grammar.
