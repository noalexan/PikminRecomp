// pikmin: runs the recompiled game. Plan 2 supports headless mode only.
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "rt/boot.hpp"
#include "rt/clock.hpp"
#include "rt/config.hpp"
#include "rt/disc.hpp"
#include "rt/gfx.hpp"
#include "rt/irq.hpp"
#include "rt/log.hpp"
#include "rt/sched.hpp"

namespace rt {
uint32_t vi_retrace_count();
void vi_set_frame_limit(uint32_t n);
}  // namespace rt

namespace {

[[noreturn]] void fail(const std::string& msg) {
    std::fprintf(stderr, "pikmin: %s\n", msg.c_str());
    std::exit(2);
}

void usage() {
    std::puts(
        "usage: pikmin [--disc PATH] [--headless | --offscreen] [--fast] [--frames N] [--watchdog SECONDS]\n"
        "              [--screenshot FRAME:PATH] [--config PATH]");
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

std::vector<uint8_t> read_dol(const rt::Disc& disc) {
    auto be32 = [](const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; };
    uint32_t dol_off = be32(disc.header() + 0x420);
    std::vector<uint8_t> hdr(0x100);
    disc.read(dol_off, hdr);
    uint32_t end = 0x100;
    for (int i = 0; i < 18; ++i) {
        uint32_t off = be32(&hdr[i * 4]), size = be32(&hdr[0x90 + i * 4]);
        if (size) end = std::max(end, off + size);
    }
    std::vector<uint8_t> dol(end);
    disc.read(dol_off, dol);
    return dol;
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

}  // namespace

int main(int argc, char** argv) {
    std::string config_path = rt::default_config_path(), disc_arg;
    bool headless_flag = false, offscreen = false, fast = false;
    uint32_t shot_frame = 0;
    std::string shot_path;
    uint32_t frames = 0;
    int watchdog = 5;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) fail("missing value after " + a);
            return argv[++i];
        };
        if (a == "--disc") disc_arg = next();
        else if (a == "--headless") headless_flag = true;
        else if (a == "--offscreen") offscreen = true;
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
        else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else fail("unknown option " + a + " (see --help)");
    }

    rt::Config cfg = rt::load_config(config_path);
    for (auto& w : cfg.warnings) rt::log("config", "%s", w.c_str());
    std::string disc_path = disc_arg.empty() ? cfg.disc : disc_arg;
    if (disc_path.empty()) {
        fail("no disc image: pass --disc PATH or set disc = \"...\" in " + config_path);
    }
    const bool headless = headless_flag || (cfg.headless && !offscreen);

    std::unique_ptr<rt::Disc> disc;
    try {
        disc = std::make_unique<rt::Disc>(rt::Disc::open(disc_path));
    } catch (const rt::DiscError& e) {
        fail(e.what());
    }
    std::vector<uint8_t> dol = read_dol(*disc);

    static std::vector<uint8_t> ram(PPC_RAM_SIZE), lc(PPC_LC_SIZE);
    if (!headless) {
        rt::GfxOptions g;
        g.headless_surface = offscreen;
        g.width = static_cast<uint32_t>(cfg.window_width);
        g.height = static_cast<uint32_t>(cfg.window_height);
        g.fullscreen = cfg.fullscreen;
        g.vsync = cfg.vsync;
        g.state_dir = state_dir();
        g.guest_ram_end = ram.data() + ram.size();
        if (!rt::gfx_init(g)) rt::exit_process(2);  // gfx_init printed the one-line reason
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

    rt::log("boot", "disc %s, %s clock", disc_path.c_str(), fast ? "fast" : "real-time");
    rt::boot_guest(&ctx, header32);
    std::fflush(stdout);
    return 0;
}
