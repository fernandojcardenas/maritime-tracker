// The live map's files (web/), built into the binary by cmake/embed_web.cmake.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "maritime/serve/server.hpp"

namespace maritime::serve {

// Path ("/index.html", "/app.js", ...) -> file. Built once, never freed.
[[nodiscard]] const std::map<std::string, Asset>& web_assets();

}  // namespace maritime::serve
