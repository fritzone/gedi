#pragma once
#include <string>
#include "nlohmann/json.hpp"

// A small text-template engine, roughly an inja / Jinja2 subset, backed by the
// nlohmann::json the rest of gedi already uses as its data model. It is enough to
// render the project build files (CMake / Make / Meson) from a json description
// without the string-concatenation mess it replaces.
//
// Supported syntax:
//   {{ path.to.value }}                     - substitute a value (dotted lookup)
//   {% for item in list %} ... {% endfor %} - iterate a json array; inside, `item`
//                                             is each element and `loop.index` /
//                                             `loop.index0` / `loop.first` /
//                                             `loop.last` / `loop.count` are set
//   {% if path %} ... {% else %} ... {% endif %}  - truthiness test
//
// Block tags ({% ... %}) that sit alone on a line are consumed with their line
// (Jinja2 trim_blocks + lstrip_blocks), so loops don't leave blank lines behind.
namespace tmpl {

// Render `source` against `data`. Unknown lookups render as empty text.
std::string render(const std::string& source, const nlohmann::json& data);

// Read a template file and render it. Sets ok=false (and returns "") if the file
// cannot be opened.
std::string renderFile(const std::string& path, const nlohmann::json& data, bool& ok);

} // namespace tmpl
