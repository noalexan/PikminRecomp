#include "ui/overlay.hpp"

#include "rt/log.hpp"

namespace ui {

void request(Request r) {
    if (r == Request::Settings) rt::log("menu", "settings requested");
    else if (r == Request::Quit) rt::log("menu", "quit requested");
}

}  // namespace ui
