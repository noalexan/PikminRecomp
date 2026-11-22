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
    return check_result();
}
