#include "ui/cheat_menu.hpp"

#include <algorithm>
#include <iterator>

#include "enhance/enhance.hpp"
#include "rt/log.hpp"

#ifdef PIKMIN_GRAPHICS
#include <imgui.h>
#include <imgui_internal.h>
#endif

namespace ui {
namespace {
bool g_open = false;
bool g_just_opened = false;
int g_color = 1;  // red
int g_count = 1;  // index into kCounts
int g_happa = 0;  // leaf
constexpr int kCounts[] = {1, 5, 10, 25, 50};
}  // namespace

bool cheat_menu_open() { return g_open; }

void cheat_menu_toggle() {
    g_open = !g_open;
    g_just_opened = g_open;
    rt::log("ui", "cheat menu %s", g_open ? "open" : "closed");
#ifdef PIKMIN_GRAPHICS
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        const ImGuiConfigFlags nav = ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags = g_open ? (io.ConfigFlags | nav) : (io.ConfigFlags & ~nav);
    }
#endif
}

#ifndef PIKMIN_GRAPHICS
void cheat_menu_draw() {}
#else
namespace {
// A row of mutually exclusive choices; works with the D-pad (left/right) and A.
void choice_row(const char* label, int& value, const char* const* names, int n) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    for (int i = 0; i < n; ++i) {
        ImGui::SameLine(i == 0 ? ImGui::GetFontSize() * 7.f : 0.f);
        ImGui::PushID(label);
        ImGui::PushID(i);
        if (ImGui::RadioButton(names[i], value == i)) value = i;
        ImGui::PopID();
        ImGui::PopID();
    }
}
}  // namespace

void cheat_menu_draw() {
    if (!g_open || !ImGui::GetCurrentContext() || !ImGui::GetCurrentContext()->WithinFrameScope) return;
    ImGuiIO& io = ImGui::GetIO();
    const float scale = std::max(1.f, io.DisplaySize.y / 1080.f);
    const enh::CheatStatus& st = enh::cheat_status();
    enh::Cheats& ch = enh::cheats();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    if (g_just_opened) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f * scale, 14.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f * scale, 5.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f * scale, 8.f * scale));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    bool keep = true;
    if (ImGui::Begin("Cheats", &keep, flags)) {
        ImGui::SetWindowFontScale(scale * 1.15f);
        if (g_just_opened) {
            ImGui::SetKeyboardFocusHere();  // nav cursor on the first item
            g_just_opened = false;
        }
        const ImVec4 dim(0.7f, 0.7f, 0.7f, 1.f), green(0.55f, 0.9f, 0.5f, 1.f);
        if (st.in_game) ImGui::TextColored(green, "%d pikmin on the field", st.pikmin);
        else ImGui::TextColored(dim, "Cheats act during a day (not in menus).");

        ImGui::SeparatorText("Spawn pikmin");
        static const char* colors[] = {"Blue", "Red", "Yellow"};
        static const char* counts[] = {"1", "5", "10", "25", "50"};
        static const char* types[] = {"Leaf", "Bud", "Flower"};
        choice_row("Color", g_color, colors, 3);
        choice_row("Count", g_count, counts, 5);
        choice_row("Type", g_happa, types, 3);
        ImGui::BeginDisabled(!st.in_game);
        if (ImGui::Button("Spawn next to Olimar")) enh::cheat_spawn(g_color, kCounts[g_count], g_happa);
        ImGui::SameLine();
        if (ImGui::Button("All pikmin to flowers")) enh::cheat_all_flowers();
        ImGui::EndDisabled();
        ImGui::TextColored(dim, "The field holds 100 pikmin at most.");

        ImGui::SeparatorText("Olimar and the day");
        ImGui::Checkbox("Invincible Olimar", &ch.invincible);
        ImGui::Checkbox("Freeze the day timer", &ch.freeze_time);
        ImGui::Checkbox("Fly (D-pad up / down: height)", &ch.fly);

        ImGui::Separator();
        ImGui::TextColored(dim, "Close: B, Esc, F1 or L3 + R3");
        if (!ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
            keep = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    if (!keep) cheat_menu_toggle();
}
#endif

}  // namespace ui
