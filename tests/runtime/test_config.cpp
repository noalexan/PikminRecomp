#include "check.hpp"
#include "rt/config.hpp"

int main() {
    rt::Config cfg = rt::parse_config(
        "# Pikmin recomp settings\n"
        "disc = \"/games/pikmin.iso\"   # trailing comment\n"
        "language = 2\n"
        "headless = true\n"
        "\n"
        "mystery = 7\n");
    CHECK(cfg.disc == "/games/pikmin.iso");
    CHECK(cfg.language == 2);
    CHECK(cfg.headless == true);
    CHECK(cfg.warnings.size() == 1 && cfg.warnings[0].find("mystery") != std::string::npos);

    rt::Config empty = rt::parse_config("");
    CHECK(empty.disc.empty() && empty.language == 0 && !empty.headless && empty.warnings.empty());

    rt::Config bad = rt::parse_config("language = \"fr\"\n");
    CHECK(bad.language == 0 && bad.warnings.size() == 1);
    rt::Config enh = rt::parse_config("aspect = \"4:3\"\ntarget_fps = \"original\"\nhud_scale = 0.6\ncamera = \"original\"\n");
    CHECK(enh.warnings.empty());
    CHECK(enh.aspect > 1.33f && enh.aspect < 1.34f && enh.target_fps == -1 && enh.hud_scale == 0.6f && !enh.modern_camera);
    rt::Config def = rt::parse_config("target_fps = 144\naspect = \"auto\"\n");
    CHECK(def.target_fps == 144 && def.aspect == 0.f && def.hud_scale == 0.5f && def.modern_camera);
    CHECK(rt::parse_fps("5") == -2 && rt::parse_aspect("wide") < 0.f);

    CHECK(rt::parse_config("debug_hud = true\n").debug_hud && !rt::parse_config("").debug_hud);
    // Writer: only the edited key changes; comments, unknown keys and order are kept.
    const std::string base = "# mine\ndisc = \"/x.iso\"  # keep\nmystery = 7\nhud_scale = 0.5\n";
    CHECK(rt::write_config_value(base, "hud_scale", "0.7") ==
          "# mine\ndisc = \"/x.iso\"  # keep\nmystery = 7\nhud_scale = 0.7\n");
    CHECK(rt::write_config_value(base, "aspect", "\"4:3\"") == base + "aspect = \"4:3\"\n");
    CHECK(rt::write_config_value(base, "disc", "\"/y.iso\"") ==
          "# mine\ndisc = \"/y.iso\"  # keep\nmystery = 7\nhud_scale = 0.5\n");
    CHECK(rt::write_config_value("vsync = true", "vsync", "false") == "vsync = false\n");
    return check_result();
}
