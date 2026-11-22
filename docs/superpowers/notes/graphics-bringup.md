# Graphics bring-up log (Plan 3, Task 7)

1. **Crash in `Texture::getAlpha` (garbage pixel pointer).** aurora's PC `GXTexObj` is 64 bytes; the game reserves the GameCube's 32, so initialising it in place overwrote the next fields of the game's `Texture`. Fix: texture objects get host-side shadows keyed by guest address (`gx_geometry.cpp`). `GXLightObj` is 64 bytes on both and stays in place.
2. **Crash after pressing Start (`0x4750495c` = "GPI\").** The SDK's `ARQPostRequest` fills the caller's `ARQRequest`; the game's completion callback (`freeBuffer`) reads `req->owner`. Fix: write all eight request fields.

Result: title screen renders correctly (logo, PRESS START, © line); Start opens the Start/Options menu.
Tools: `tools/ppm2png.py` (screenshot → PNG), `tools/resolve_bt.py` (backtrace frames → link-map names).
