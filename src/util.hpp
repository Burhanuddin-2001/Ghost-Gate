// util.hpp
// Small string helpers shared by the server and the CLI.

#pragma once

#include <string>
#include <string_view>

namespace gh {

// UTF-16 (Windows "wide") <-> UTF-8 conversion. Invalid input yields "".
std::string  to_utf8(std::wstring_view text);
std::wstring from_utf8(std::string_view text);

// Returns `text` as a quoted JSON string literal, with all required escapes.
std::string json_string(std::string_view utf8);

// Removes leading and trailing spaces, tabs, CR and LF.
std::string trim(std::string_view text);

// ASCII-only lowercase (HTTP header names are ASCII).
std::string to_lower_ascii(std::string_view text);

} // namespace gh
