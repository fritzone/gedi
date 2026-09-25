#pragma once
#include <string>
#include "nlohmann/json.hpp"

// A small YAML reader, in the same spirit as TemplateEngine: just enough of the
// language to express an install script, parsed into the nlohmann::json the rest
// of gedi already speaks. No third-party dependency is pulled in for it.
//
// Supported:
//   key: value                  scalars (plain, 'single' or "double" quoted)
//   key:                        nested block mapping (by indentation)
//     child: value
//   key:                        block sequence
//     - item
//     - name: a                 (a mapping as a sequence item)
//       path: b
//   key: [a, b, c]              flow sequence of scalars
//   key: |                      literal block scalar (keeps newlines)
//   key: |-                     ... with the trailing newline stripped
//   key: >                      folded block scalar (newlines become spaces)
//   # comment                   to end of line, outside quotes
//
// Scalars become bool for true/false/yes/no, integer where they parse as one,
// and string otherwise. Tabs are rejected as indentation, as in real YAML.
namespace yaml {

// Parse `text`. On failure returns a discarded json value, and `error` holds a
// "line N: message" description.
nlohmann::json parse(const std::string& text, std::string& error);

// Read and parse a file. Sets `error` when the file cannot be opened.
nlohmann::json parseFile(const std::string& path, std::string& error);

} // namespace yaml
