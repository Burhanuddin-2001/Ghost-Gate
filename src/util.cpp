// util.cpp

#include "util.hpp"

#include <windows.h>

#include <climits>
#include <cstdio>

namespace gh {

std::string to_utf8(std::wstring_view text) {
    if (text.empty() || text.size() > INT_MAX) {
        return {};
    }
    const int len = static_cast<int>(text.size());
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), len,
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), len,
                        out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring from_utf8(std::string_view text) {
    if (text.empty() || text.size() > INT_MAX) {
        return {};
    }
    const int len = static_cast<int>(text.size());
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), len,
                                           nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), len, out.data(), needed);
    return out;
}

std::string json_string(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size() + 2);
    out.push_back('"');
    for (const char ch : utf8) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(ch));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string trim(std::string_view text) {
    constexpr std::string_view ws = " \t\r\n";
    const size_t first = text.find_first_not_of(ws);
    if (first == std::string_view::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(ws);
    return std::string(text.substr(first, last - first + 1));
}

std::string to_lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

} // namespace gh
