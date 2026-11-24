// Cutscene skip: Start (Esc, DS4 touchpad) during an in-game movie runs the game in turbo until the
// movie list ends.
//
// The game can jump a movie to its end (MoviePlayer::skipScene), but movies drive gameplay: demo
// event keys start creature actions whose own animations then do the work (day 1: event 3/4 boots
// the Onion, whose animation keys emit the seed). Jumping, or even fast-forwarding only the
// cinematic, skips those and leaves the game stuck. Turbo instead runs every game frame as one
// original PAL frame (enhance/framerate.cpp: original frame time, every per-frame step once) and
// presents as fast as the display allows (~10x at 240 Hz), so game logic is exactly that of normal
// play, only faster.
//
// While a movie plays, Start is hidden from the game.
#include "enhance/enhance.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"

namespace enh {
namespace {
constexpr uint16_t kPadStart = 0x1000;  // PAD_BUTTON_START
bool g_skipping = false;
bool g_start_held = false;

bool movie_playing(ppc_ctx* c) {
    const uint32_t mp = rt::rd32(c, kMoviePlayerPtr);
    return mp && rt::rd8(c, mp + 0x124) != 0;
}
}  // namespace

void cutscene_filter_pad(ppc_ctx* c, uint32_t status) {
    const uint16_t buttons = rt::rd16(c, status);
    const bool start = (buttons & kPadStart) != 0;
    if (!movie_playing(c)) {
        g_skipping = false;
        g_start_held = start;  // a press that began outside a movie doesn't skip the next one
        return;
    }
    if (start && !g_start_held && !g_skipping) {
        g_skipping = true;
        rt::log("cutscene", "skipping (turbo)");
    }
    g_start_held = start;
    if (start) rt::wr16(c, status, static_cast<uint16_t>(buttons & ~kPadStart));
}

bool cutscene_turbo(ppc_ctx* c) {
    if (g_skipping && !movie_playing(c)) {
        g_skipping = false;
        rt::log("cutscene", "skip done");
    }
    return g_skipping;
}

}  // namespace enh
