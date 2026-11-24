#include "ui/debug_hud.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>

#include "enhance/enhance.hpp"
#include "rt/config.hpp"
#include "rt/diag.hpp"
#include "rt/gfx.hpp"
#include "rt/log.hpp"
#include "rt/perf.hpp"
#include "rt/sysinfo.hpp"
#include "rt/trace.hpp"

#ifdef PIKMIN_GRAPHICS
#include <imgui.h>
#include <imgui_internal.h>
#endif

namespace ui {
namespace {
std::string g_path;
bool g_on = false;
}  // namespace

void hud_init(const std::string& config_path, bool on) {
    g_path = config_path;
    g_on = on;
}

void hud_set(bool on, bool save) {
    g_on = on;
    rt::log("ui", "debug hud %s", on ? "on" : "off");
    if (save && !g_path.empty()) rt::save_config_value(g_path, "debug_hud", on ? "true" : "false");
}

bool hud_on() { return g_on; }
void hud_toggle() { hud_set(!g_on, true); }

#ifndef PIKMIN_GRAPHICS
void hud_draw() {}
#else
// The version number at the bottom-right corner of the title screen.
void version_draw() {
    if (!enh::title_visible()) return;
    const ImGuiIO& io = ImGui::GetIO();
    const float scale = std::max(1.f, io.DisplaySize.y / 720.f);
    char text[64];
    std::snprintf(text, sizeof text, "v%s", rt::kVersion);
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * 1.25f * scale, margin = 14.f * scale;
    const ImVec2 ext = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
    const ImVec2 pos(io.DisplaySize.x - ext.x - margin, io.DisplaySize.y - ext.y - margin);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddText(font, size, ImVec2(pos.x + scale, pos.y + scale), IM_COL32(0, 0, 0, 160), text);
    dl->AddText(font, size, pos, IM_COL32(255, 255, 255, 190), text);
}

void hud_draw() {
    if (ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->WithinFrameScope) version_draw();
    if (!g_on || !ImGui::GetCurrentContext() || !ImGui::GetCurrentContext()->WithinFrameScope) return;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const rt::SysSample& sys = rt::sysinfo_sample(now);
    const rt::GpuInfo gpu = rt::gfx_adapter_info();
    const ImGuiIO& io = ImGui::GetIO();
    const float scale = std::max(1.f, io.DisplaySize.y / 1080.f);

    ImGui::SetNextWindowPos(ImVec2(10.f * scale, 10.f * scale));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##pikmin_debug_hud", nullptr, flags)) {
        ImGui::SetWindowFontScale(scale);
        const ImVec4 red(1.f, 0.35f, 0.3f, 1.f), dim(0.75f, 0.75f, 0.75f, 1.f);
        ImGui::Text("%.0f fps  %.1f ms", rt::perf_fps(), rt::perf_ms());
        ImGui::SameLine();
        ImGui::TextColored(dim, "(1%% low %.0f, target %.1f ms)", rt::perf_one_percent_low(), rt::perf_target_ms());

        // Frame times over the last 4 s: one column per pixel (the slowest frame in it), drops in red.
        const auto& hist = rt::perf_history();
        const ImVec2 size(360.f * scale, 60.f * scale);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(0, 0, 0, 90));
        const double cap = std::min(100.0, std::max({3.0 * rt::perf_target_ms(), 3.0 * rt::perf_ms(), 1.0}));
        if (!hist.empty()) {
            const double t1 = hist.back().t, t0 = t1 - 4.0;
            const int cols = static_cast<int>(size.x);
            std::vector<float> worst(static_cast<size_t>(cols), 0.f);
            std::vector<bool> drop(static_cast<size_t>(cols), false);
            for (const auto& f : hist) {
                const int col = std::clamp(static_cast<int>((f.t - t0) / 4.0 * cols), 0, cols - 1);
                worst[static_cast<size_t>(col)] = std::max(worst[static_cast<size_t>(col)], static_cast<float>(f.total_ms));
                if (f.drop) drop[static_cast<size_t>(col)] = true;
            }
            for (int x = 0; x < cols; ++x) {
                const float v = worst[static_cast<size_t>(x)];
                if (v <= 0.f) continue;
                const float h = static_cast<float>(std::min<double>(v, cap) / cap) * size.y;
                dl->AddLine(ImVec2(p.x + x, p.y + size.y), ImVec2(p.x + x, p.y + size.y - h),
                            drop[static_cast<size_t>(x)] ? IM_COL32(255, 80, 70, 255) : IM_COL32(120, 220, 120, 200));
            }
            const float ty = p.y + size.y - static_cast<float>(rt::perf_target_ms() / cap) * size.y;
            dl->AddLine(ImVec2(p.x, ty), ImVec2(p.x + size.x, ty), IM_COL32(255, 255, 255, 60));
        }
        ImGui::Dummy(size);
        if (!hist.empty()) {
            double game = 0, present = 0, pace = 0;
            int n = 0;
            for (auto it = hist.rbegin(); it != hist.rend() && n < 60; ++it, ++n)
                game += it->game_ms, present += it->present_ms, pace += it->pace_ms;
            ImGui::Text("game %.1f ms  present %.1f ms  pacing %.1f ms", game / n, present / n, pace / n);
        }
        auto pct = [](double v) {
            static char b[4][16];
            static int i = 0;
            char* s = b[i++ & 3];
            if (v < 0) std::snprintf(s, 16, "n/a");
            else std::snprintf(s, 16, "%.0f%%", v);
            return s;
        };
        ImGui::Text("CPU %s of a core (game thread %s)", pct(sys.cpu_pct), pct(sys.game_thread_pct));
        ImGui::Text("GPU %s - %s - busy %s", gpu.name.empty() ? "?" : gpu.name.c_str(), gpu.backend.c_str(),
                    pct(sys.gpu_busy_pct));
        ImGui::TextColored(dim, "%d fps target  retrace %u", enh::effective_fps(), rt::g_trace_retrace);
        if (auto d = rt::perf_last_drop()) {
            ImGui::TextColored(red, "last drop: %.0f ms at retrace %u", d->ms, d->retrace);
            if (!d->cause.empty()) ImGui::TextColored(red, "  %s", d->cause.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
#endif

}  // namespace ui
