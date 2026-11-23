#include "rt/sysinfo.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace rt {
namespace {
std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// NVML through dlopen (no build dependency).
struct Nvml {
    using Init = int (*)();
    using Count = int (*)(unsigned*);
    using Handle = int (*)(unsigned, void**);
    using Name = int (*)(void*, char*, unsigned);
    struct Util {
        unsigned gpu, memory;
    };
    using Utilization = int (*)(void*, Util*);
    void* lib = nullptr;
    void* device = nullptr;
    Utilization util = nullptr;

    bool open(const std::string& want) {
        lib = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        auto init = reinterpret_cast<Init>(dlsym(lib, "nvmlInit_v2"));
        auto count = reinterpret_cast<Count>(dlsym(lib, "nvmlDeviceGetCount_v2"));
        auto handle = reinterpret_cast<Handle>(dlsym(lib, "nvmlDeviceGetHandleByIndex_v2"));
        auto name = reinterpret_cast<Name>(dlsym(lib, "nvmlDeviceGetName"));
        util = reinterpret_cast<Utilization>(dlsym(lib, "nvmlDeviceGetUtilizationRates"));
        unsigned n = 0;
        if (!init || !count || !handle || !name || !util || init() != 0 || count(&n) != 0) return false;
        for (unsigned i = 0; i < n; ++i) {  // the device Dawn uses, by name
            void* d = nullptr;
            char buf[96] = {};
            if (handle(i, &d) != 0 || name(d, buf, sizeof buf) != 0) continue;
            const std::string a = lower(buf), b = lower(want);
            if (!b.empty() && (a.find(b) != std::string::npos || b.find(a) != std::string::npos)) {
                device = d;
                return true;
            }
        }
        return false;
    }
    double busy() {
        Util u{};
        return device && util(device, &u) == 0 ? static_cast<double>(u.gpu) : -1;
    }
};

std::string amd_busy_path(uint32_t vendor, uint32_t device) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
        const std::string dev = e.path().string() + "/device/";
        if (std::strtoul(read_file(dev + "vendor").c_str(), nullptr, 16) != vendor) continue;
        if (device && std::strtoul(read_file(dev + "device").c_str(), nullptr, 16) != device) continue;
        if (std::filesystem::exists(dev + "gpu_busy_percent", ec)) return dev + "gpu_busy_percent";
    }
    return {};
}

pid_t g_tid = 0;
GpuSource g_source = GpuSource::None;
Nvml g_nvml;
std::string g_amd_path;
SysSample g_sample;
double g_at = -1, g_proc_ticks = -1, g_thread_ticks = -1;
}  // namespace

double cpu_ticks_from_stat(const std::string& line) {
    const size_t close = line.rfind(')');
    if (close == std::string::npos) return -1;
    std::istringstream in(line.substr(close + 1));
    std::vector<std::string> f;
    for (std::string w; in >> w;) f.push_back(w);
    if (f.size() < 13) return -1;  // f[0] = state (field 3), f[11] = utime (14), f[12] = stime (15)
    return std::strtod(f[11].c_str(), nullptr) + std::strtod(f[12].c_str(), nullptr);
}

GpuSource pick_gpu_source(uint32_t vendor, bool nvml, bool amd) {
    if (vendor == 0x10de && nvml) return GpuSource::Nvml;
    if (vendor == 0x1002 && amd) return GpuSource::AmdSysfs;
    return GpuSource::None;
}

void sysinfo_init(pid_t game_tid, const GpuInfo& gpu) {
    g_tid = game_tid;
    const bool nvml = gpu.vendor == 0x10de && g_nvml.open(gpu.name);
    if (gpu.vendor == 0x1002) g_amd_path = amd_busy_path(gpu.vendor, gpu.device);
    g_source = pick_gpu_source(gpu.vendor, nvml, !g_amd_path.empty());
}

const SysSample& sysinfo_sample(double now) {
    if (g_at >= 0 && now - g_at < 0.5) return g_sample;
    const double hz = static_cast<double>(sysconf(_SC_CLK_TCK));
    const double proc = cpu_ticks_from_stat(read_file("/proc/self/stat"));
    const double thread = g_tid ? cpu_ticks_from_stat(read_file("/proc/self/task/" + std::to_string(g_tid) + "/stat")) : -1;
    if (g_at >= 0 && now > g_at) {
        const double dt = now - g_at;
        g_sample.cpu_pct = proc >= 0 && g_proc_ticks >= 0 ? (proc - g_proc_ticks) / hz / dt * 100.0 : -1;
        g_sample.game_thread_pct = thread >= 0 && g_thread_ticks >= 0 ? (thread - g_thread_ticks) / hz / dt * 100.0 : -1;
    }
    g_proc_ticks = proc;
    g_thread_ticks = thread;
    g_at = now;
    if (g_source == GpuSource::Nvml) g_sample.gpu_busy_pct = g_nvml.busy();
    else if (g_source == GpuSource::AmdSysfs) {
        const std::string v = read_file(g_amd_path);
        g_sample.gpu_busy_pct = v.empty() ? -1 : std::strtod(v.c_str(), nullptr);
    }
    return g_sample;
}

}  // namespace rt
