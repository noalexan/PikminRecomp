// CPU and GPU usage for the debug HUD, sampled at most every 0.5 s. GPU busy % comes from NVML
// (dlopen'd, NVIDIA) or amdgpu sysfs, for the adapter Dawn actually uses; -1 = not available.
#pragma once
#include <cstdint>
#include <string>

namespace rt {

struct GpuInfo {
    std::string name, backend;
    uint32_t vendor = 0, device = 0;
};

// utime + stime (fields 14-15) of a /proc/<pid>/stat line; -1 if it doesn't parse.
double cpu_ticks_from_stat(const std::string& stat_line);

enum class GpuSource { None, Nvml, AmdSysfs };
GpuSource pick_gpu_source(uint32_t vendor, bool nvml_available, bool amd_sysfs_available);

struct SysSample {
    double cpu_pct = -1, game_thread_pct = -1, gpu_busy_pct = -1;
};
void sysinfo_init(long game_tid, const GpuInfo& gpu);
const SysSample& sysinfo_sample(double now_s);

}  // namespace rt
