// Minimal, strict HTTP/1.1 request parsing and response serialisation.
// Pure functions with no sockets, so they can be unit-tested directly.

#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gh::http {

struct Request {
    std::string method;    // "GET", "POST", ...
    std::string path;      // "/api/status" (no query string)
    std::string query;     // "width=800" (without the '?')
    std::string version;   // "HTTP/1.1"
    std::map<std::string, std::string> headers;   // keys are lowercase
    std::string body;
    std::string remote_ip; // filled in by the server

    // Header value by (case-insensitive) name, or "" if absent.
    std::string header(std::string_view name) const;
};

struct Response {
    int         status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::vector<std::pair<std::string, std::string>> headers;   // extra headers
    std::string body;
};

struct Limits {
    size_t max_header_bytes = 8 * 1024;
    size_t max_body_bytes   = 4 * 1024;
};

enum class ParseResult {
    Incomplete,   // need more bytes
    Ok,
    Bad,          // malformed or unsupported -> 400
    TooLarge,     // exceeds Limits -> 413 / 431
};

ParseResult parse_request(std::string_view data, Request& out, const Limits& limits = {});

// Full response bytes. Always adds Content-Length, Connection: close and defensive security headers. `head_only` omits the body (for HEAD requests).
std::string serialize(const Response& response, bool head_only = false);

std::string_view reason_phrase(int status);

// Decodes %XX escapes and '+' (as space). Returns false on a malformed escape.
bool url_decode(std::string_view in, std::string& out);

// Parses "a=1&b=two" (query strings and x-www-form-urlencoded bodies).
// Pairs with malformed escapes are skipped. Later duplicates win.
std::map<std::string, std::string> parse_form(std::string_view text);

// Convenience constructors.
Response json(int status, std::string body);
Response text(int status, std::string body);

} // namespace gh::http
