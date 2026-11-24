// Enhancements: widescreen (Hor+), anchored/scaled HUD, high framerate, right-stick camera.
// Game functions are patched through "hook" entries in recomp/overrides.toml: the host hle_<name>
// runs instead of the guest function and can call the original translation f_<addr>_orig.
#pragma once
#include <cstdint>

extern "C" {
#include "ppc.h"
}

namespace enh {

struct Settings {
    float aspect = 0.f;         // 0 = follow the window (Hor+), 4/3 = original (pillarboxed by aurora)
    int target_fps = 0;         // 0 = display refresh, -1 = original PAL VI pacing, else a cap
    float hud_scale = 0.5f;     // gameplay HUD size, anchored to the screen edges
    bool modern_camera = true;  // right stick = camera; swarm = modifier + right stick
    bool cutscene_fill = true;  // in-game movies keep the 4:3 horizontal FOV (crop) instead of Hor+
    bool window_vsync = false;  // presenting to a vsync'd window (vsync paces frames)
    int display_hz = 60;        // refresh rate of the display (or 60 offscreen)
};
Settings& settings();

// Effective aspect ratio of the 3D view (window aspect, or 4/3).
float view_aspect();
// Adds Quit Game / Options items and the settings screens to the game's menus (enhance/menu_entries.cpp).
void install_menu_entries();
// The added item (0 = first) a patched DrawMenu finished on since the last call, or -1.
int take_menu_choice(uint32_t menu);
// ogScrTitleMgr::draw: the original, or our Display / Game / Quit screen (enhance/settings_menu.cpp).
void title_draw(ppc_ctx* c);
bool title_visible();  // the title screen drew within the last 0.25 s (version overlay)
// Game frames per second in the unlocked mode (target_fps or the display refresh).
int effective_fps();
inline bool original_timing() { return settings().target_fps < 0; }
// How many original (PAL) game frames this frame stands for: real frame time x the original frame
// rate (50 Hz / retraces per frame). 1 in the original mode. Frame-counted systems scale by it.
float orig_frames();
// Whole original frames that elapsed this frame (0 most frames at 240 fps; 1 in the original mode).
// Shared by every per-frame step so they stay in lockstep, as on PAL (enhance/particles.cpp).
int orig_frame_steps();
// Fraction [0, 1) of the next original frame already elapsed (0 in the original mode): for
// drawing state that only advances per original frame (enhance/particles.cpp).
float orig_frame_phase();
// Frame time the original game used for one frame (min(retraces x 20 ms, 1/30 s)).
float orig_frame_dt();

// Cutscene skip (enhance/cutscene_skip.cpp). PADRead: Start during a movie starts the skip and is
// hidden from the game. cutscene_turbo: true while skipping (frames run as original frames, unpaced).
void cutscene_filter_pad(ppc_ctx* c, uint32_t status);
bool cutscene_turbo(ppc_ctx* c);

// Cheats (enhance/cheats.cpp; menu in ui/cheat_menu.cpp). Requests are queued by the menu and run
// at the top of the next game frame (cheats_frame, from the System::updateSysClock hook).
struct Cheats {
    bool invincible = false;   // Olimar's health stays full
    bool freeze_time = false;  // the day's clock stops
    bool fly = false;          // Olimar hovers; D-pad up/down changes the height
};
struct CheatStatus {
    bool in_game = false;  // a captain exists (gameplay)
    int pikmin = 0;        // pikmin on the field
};
Cheats& cheats();
const CheatStatus& cheat_status();
void cheat_spawn(int color, int count, int happa);  // color 0 blue, 1 red, 2 yellow; happa 0 leaf, 1 bud, 2 flower
void cheat_all_flowers();
void cheats_frame(ppc_ctx* c);
// PADRead: a released pad while the menu is open; D-pad up/down go to fly mode.
void cheats_filter_pad(ppc_ctx* c, uint32_t status, bool menu_open);

// Hooks from the GX shims (graphics builds only).
void on_set_projection(float m[16], uint32_t type);  // adjusts 2D projections in place
bool on_set_scissor(uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h);
void on_set_viewport(float x, float y, float w, float h);

// PAD: splits the right stick between the camera and (with the modifier held) the guest C-stick.
// `status` is the guest PADStatus of port 0.
void route_right_stick(ppc_ctx* c, uint32_t status, bool swarm_modifier);

// Guest globals used by the patches (PAL GPIP01 addresses, from build.map).
constexpr uint32_t kGsysPtr = 0x803ec9ccu;  // System* gsys
constexpr uint32_t kFrameTimeOff = 0x290;  // float System::mFrameTime
// MoviePlayer* (gameflow.mMoviePlayer); +0x124 (u8) = a movie is playing.
constexpr uint32_t kMoviePlayerPtr = 0x803a2814u;

}  // namespace enh
