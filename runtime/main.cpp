// pikmin: runs the recompiled game. Plan 2 supports headless mode only.
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "rt/audio_out.hpp"
#include "enhance/enhance.hpp"
#include "rt/boot.hpp"
#include "rt/clock.hpp"
#include "rt/config.hpp"
#include "rt/diag.hpp"
#include "rt/trace.hpp"
#include "rt/disc.hpp"
#include "rt/gfx.hpp"
#include "rt/input_script.hpp"
#include "rt/irq.hpp"
#include "rt/log.hpp"
#include "rt/sched.hpp"

namespace rt {
extern std::string g_card_dir;
uint32_t vi_retrace_count();
void vi_set_frame_limit(uint32_t n);
}  // namespace rt

namespace {

[[noreturn]] void fail(const std::string& msg) {
    rt::log_raw("pikmin: %s", msg.c_str());
    std::exit(2);
}

void usage() {
    std::puts(
        "usage: pikmin [--disc PATH] [--headless | --offscreen] [--fast] [--frames N] [--watchdog SECONDS]\n"
        "              [--screenshot FRAME:PATH] [--input-script PATH] [--config PATH]\n"
        "              [--aspect auto|4:3] [--target-fps auto|original|N] [--hud-scale X] [--camera modern|original]");
}

// $XDG_DATA_HOME/pikminrecomp (falls back to ~/.local/share/pikminrecomp), created if missing.
std::string state_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base = xdg && *xdg ? xdg : std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.local/share";
    std::string dir = base + "/pikminrecomp";
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    }
    return dir;
}

extern "C" const uint8_t pikmin_dol_start[], pikmin_dol_end[];  // dol_blob.S

// Game data when neither --disc nor config names one: installed tree, then the source checkout.
std::string default_data_dir() {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::vector<std::string> candidates;
    if (n > 0) {
        std::string e(exe, static_cast<size_t>(n));
        candidates.push_back(e.substr(0, e.rfind('/')) + "/" PIKMIN_DATA_REL);
    }
    candidates.push_back(PIKMIN_SOURCE_DATA);
    for (auto& d : candidates) {
        struct stat st{};
        if (stat((d + "/sys/fst.bin").c_str(), &st) == 0) return d;
    }
    return {};
}

pthread_t g_game_thread;

// Runs on the game thread when the watchdog fires: show where it is stuck, then exit.
void on_watchdog_signal(int) {
    void* frames[48];
    int n = backtrace(frames, 48);
    static const char kHead[] = "watchdog: game thread backtrace:\n";
    write(2, kHead, sizeof kHead - 1);
    backtrace_symbols_fd(frames, n, 2);
    std::_Exit(3);
}

void start_watchdog(int seconds) {
    g_game_thread = pthread_self();
    signal(SIGUSR2, on_watchdog_signal);
    std::thread([seconds] {
        uint32_t last = rt::vi_retrace_count();
        auto since = std::chrono::steady_clock::now();
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            uint32_t now = rt::vi_retrace_count();
            if (now != last) {
                last = now;
                since = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - since > std::chrono::seconds(seconds)) {
                std::fflush(stdout);
                std::fprintf(stderr, "watchdog: no retrace for %d s; last HLE %s called from lr 0x%08x\n", seconds,
                             rt::current_function.name, rt::current_function.lr);
                pthread_kill(g_game_thread, SIGUSR2);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                std::_Exit(3);
            }
        }
    }).detach();
}

// Opens this run's log file and writes its header: build, command line, settings, mode and clock.
void open_run_log(int argc, char** argv, const rt::Config& cfg, const enh::Settings& es, const std::string& disc_path,
                  const char* mode, bool fast) {
    rt::log_open(rt::log_dir());
    rt::trace_init();
    std::string cmd;
    for (int i = 0; i < argc; ++i) cmd += (i ? " " : "") + std::string(argv[i]);
    rt::log_file_only("run", "pikmin %s (%s)", rt::kBuildHash, rt::kBuildType);
    rt::log_file_only("run", "command: %s", cmd.c_str());
    rt::log_file_only("run", "mode: %s, clock: %s", mode, fast ? "fast" : "real-time");
    rt::log_file_only("run", "disc: %s", disc_path.c_str());
    rt::log_file_only("run", "config: language=%d window=%dx%d fullscreen=%d vsync=%d audio=%d", cfg.language,
                      cfg.window_width, cfg.window_height, cfg.fullscreen, cfg.vsync, cfg.audio);
    rt::log_file_only("run", "enhancements: aspect=%s target_fps=%d hud_scale=%.2f camera=%s",
                      es.aspect > 0.f ? "4:3" : "auto", es.target_fps, es.hud_scale,
                      es.modern_camera ? "modern" : "original");
    for (auto& w : cfg.warnings) rt::log_file_only("config", "%s", w.c_str());
    rt::log_file_only("run", "log file: %s", rt::log_path().c_str());
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path = rt::default_config_path(), disc_arg;
    bool headless_flag = false, offscreen = false, fast = false;
    uint32_t shot_frame = 0;
    std::string shot_path;
    uint32_t frames = 0;
    int watchdog = 5;
    std::string aspect_arg, fps_arg, hud_arg, camera_arg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) fail("missing value after " + a);
            return argv[++i];
        };
        if (a == "--disc") disc_arg = next();
        else if (a == "--headless") headless_flag = true;
        else if (a == "--offscreen") offscreen = true;
        else if (a == "--input-script") {
            std::string path = next();
            std::ifstream f(path);
            if (!f) fail("cannot read input script " + path);
            std::stringstream ss;
            ss << f.rdbuf();
            auto parsed = rt::parse_input_script(ss.str());
            if (!parsed.error.empty()) fail(parsed.error);
            rt::set_input_script(parsed.entries);
        }
        else if (a == "--screenshot") {
            std::string v = next();
            size_t colon = v.find(':');
            if (colon == std::string::npos || colon == 0) fail("--screenshot expects FRAME:PATH");
            shot_frame = static_cast<uint32_t>(std::stoul(v.substr(0, colon)));
            shot_path = v.substr(colon + 1);
        }
        else if (a == "--fast") fast = true;
        else if (a == "--frames") frames = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--watchdog") watchdog = std::stoi(next());
        else if (a == "--config") config_path = next();
        else if (a == "--aspect") aspect_arg = next();
        else if (a == "--target-fps") fps_arg = next();
        else if (a == "--hud-scale") hud_arg = next();
        else if (a == "--camera") camera_arg = next();
        else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else fail("unknown option " + a + " (see --help)");
    }

    rt::Config cfg = rt::load_config(config_path);
    for (auto& w : cfg.warnings) rt::log("config", "%s", w.c_str());
    std::string disc_path = disc_arg.empty() ? cfg.disc : disc_arg;
    if (disc_path.empty()) disc_path = default_data_dir();
    if (disc_path.empty()) {
        fail("no game data: install it (cmake --install), pass --disc PATH or set disc = \"...\" in " + config_path);
    }
    const bool headless = headless_flag || (cfg.headless && !offscreen);

    // Enhancements: config file, overridden by the command line.
    enh::Settings& es = enh::settings();
    es.aspect = cfg.aspect;
    es.target_fps = cfg.target_fps;
    es.hud_scale = cfg.hud_scale;
    es.modern_camera = cfg.modern_camera;
    if (!aspect_arg.empty() && (es.aspect = rt::parse_aspect(aspect_arg)) < 0.f) fail("--aspect expects auto or 4:3");
    if (!fps_arg.empty() && (es.target_fps = rt::parse_fps(fps_arg)) < -1) {
        fail("--target-fps expects auto, original or 20-1000");
    }
    if (!hud_arg.empty()) {
        es.hud_scale = std::strtof(hud_arg.c_str(), nullptr);
        if (es.hud_scale < 0.2f || es.hud_scale > 1.5f) fail("--hud-scale expects 0.2-1.5");
    }
    if (!camera_arg.empty()) {
        if (camera_arg != "modern" && camera_arg != "original") fail("--camera expects modern or original");
        es.modern_camera = camera_arg == "modern";
    }
    // Headless runs have nothing to show: keep the original pacing unless asked otherwise.
    if (headless && fps_arg.empty()) es.target_fps = -1;

    open_run_log(argc, argv, cfg, es, disc_path, headless ? "headless" : offscreen ? "offscreen" : "window", fast);

    std::unique_ptr<rt::Disc> disc;
    try {
        disc = std::make_unique<rt::Disc>(rt::Disc::open(disc_path));
    } catch (const rt::DiscError& e) {
        fail(e.what());
    }
    std::span<const uint8_t> dol(pikmin_dol_start, static_cast<size_t>(pikmin_dol_end - pikmin_dol_start));

    static std::vector<uint8_t> ram(PPC_RAM_SIZE), lc(PPC_LC_SIZE);
    if (!headless) {
        rt::GfxOptions g;
        g.headless_surface = offscreen;
        g.width = static_cast<uint32_t>(cfg.window_width);
        g.height = static_cast<uint32_t>(cfg.window_height);
        g.fullscreen = cfg.fullscreen;
        g.vsync = cfg.vsync;
        g.aspect_4_3 = es.aspect > 0.f;
        g.state_dir = state_dir();
        g.guest_ram_end = ram.data() + ram.size();
        if (!rt::gfx_init(g)) rt::exit_process(2);  // gfx_init printed the one-line reason
        const int hz = rt::gfx_display_refresh();
        es.display_hz = hz > 0 ? hz : 60;
        es.window_vsync = !offscreen && cfg.vsync && hz > 0;
        if (!shot_path.empty()) rt::gfx_request_screenshot(shot_frame, shot_path);
    } else if (!shot_path.empty()) {
        fail("--screenshot needs graphics (drop --headless, or use --offscreen)");
    }

    static ppc_ctx ctx{};
    ctx.mem = ram.data();
    ctx.lc = lc.data();
    rt::g_language = static_cast<uint32_t>(cfg.language);

    rt::sched().init(&ctx, rt::kMainThreadEa);
    rt::clock().init(fast ? rt::Clock::Mode::Fast : rt::Clock::Mode::Real);
    try {
        rt::load_dol(&ctx, dol);
    } catch (const rt::BootError& e) {
        fail(e.what());
    }
    rt::init_registers(&ctx);
    rt::irq().init(&ctx);
    uint8_t header32[0x20];
    std::memcpy(header32, disc->header(), sizeof header32);
    rt::set_disc(std::move(disc));
    if (frames) rt::vi_set_frame_limit(frames);
    if (watchdog > 0) start_watchdog(watchdog);

    // Sound plays only in real-time runs with graphics; --headless/--fast stay deviceless (the WAV
    // dump still works there).
    rt::audio_out_init(cfg.audio && !headless && !fast);
    rt::g_card_dir = state_dir() + "/card_a";
    rt::log("boot", "disc %s, %s clock", disc_path.c_str(), fast ? "fast" : "real-time");
    if (es.target_fps < 0) rt::log("boot", "original frame pacing (PAL VI)");
    else rt::log("boot", "game loop at %d fps%s, display %d Hz", enh::effective_fps(), es.window_vsync ? " (vsync)" : "",
                 es.display_hz);
    rt::boot_guest(&ctx, header32);
    std::fflush(stdout);
    return 0;
}
