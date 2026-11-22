// User configuration: $XDG_CONFIG_HOME/pikminrecomp/config.toml (a small TOML subset).
#pragma once
#include <string>
#include <vector>

namespace rt {

struct Config {
    std::string disc;
    int language = 0;
    bool headless = false;
    std::vector<std::string> warnings;
};

Config parse_config(const std::string& text);
std::string default_config_path();
// Missing file -> defaults (no error).
Config load_config(const std::string& path);

}  // namespace rt
