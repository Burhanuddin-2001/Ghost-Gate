#include "http.hpp"
#include "util.hpp"

#include <charconv>

namespace gh::http {
namespace {

bool is_token_char(char c) {
    // visible ASCII except delimiters.
    if (c >= '0' && c <= '9') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    return std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

bool is_token(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!is_token_char(c)) return false;
    }
    return true;
}

bool has_control_chars(std::string_view s) {
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) return true;
    }
    return false;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::string Request::header(std::string_view name) const {
    auto it = headers.find(to_lower_ascii(name));
    return it == headers.end() ? std::string() : it->second;
}

ParseResult parse_request(std::string_view data, Request& out, const Limits& limits) {
    const size_t head_end = data.find("\r\n\r\n");
    if (head_end == std::string_view::npos) {
        return data.size() > limits.max_header_bytes ? ParseResult::TooLarge
                                                     : ParseResult::Incomplete;
    }
    if (head_end > limits.max_header_bytes) {
        return ParseResult::TooLarge;
    }

    Request req;
    const std::string_view head = data.substr(0, head_end);

    // ---- Request line: METHOD SP TARGET SP VERSION ----
    size_t line_end = head.find("\r\n");
    const std::string_view line = head.substr(0, line_end);
    const size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) return ParseResult::Bad;
    const size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return ParseResult::Bad;
    if (line.find(' ', sp2 + 1) != std::string_view::npos) return ParseResult::Bad;

    const std::string_view method  = line.substr(0, sp1);
    const std::string_view target  = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = line.substr(sp2 + 1);

    if (!is_token(method)) return ParseResult::Bad;
    if (target.empty() || target.front() != '/' || has_control_chars(target)) {
        return ParseResult::Bad;
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0") return ParseResult::Bad;

    req.method  = std::string(method);
    req.version = std::string(version);
    const size_t q = target.find('?');
    req.path  = std::string(target.substr(0, q));
    req.query = q == std::string_view::npos ? std::string() : std::string(target.substr(q + 1));

    // ---- Header fields ----
    size_t pos = line_end == std::string_view::npos ? head.size() : line_end + 2;
    while (pos < head.size()) {
        size_t end = head.find("\r\n", pos);
        if (end == std::string_view::npos) end = head.size();
        const std::string_view field = head.substr(pos, end - pos);
        pos = end + 2;

        const size_t colon = field.find(':');
        if (colon == std::string_view::npos) return ParseResult::Bad;
        const std::string_view name = field.substr(0, colon);
        if (!is_token(name)) return ParseResult::Bad;   // also rejects obsolete line folding
        const std::string value = trim(field.substr(colon + 1));
        if (has_control_chars(value)) return ParseResult::Bad;

        const std::string key = to_lower_ascii(name);
        auto [it, inserted] = req.headers.emplace(key, value);
        if (!inserted) {
            if (key == "content-length" || key == "host") {
                return ParseResult::Bad;   // ambiguous duplicates enable smuggling
            }
            it->second += (key == "cookie" ? "; " : ", ") + value;
        }
    }

    // We never accept chunked bodies; a simple server should reject rather than guess.
    if (req.headers.count("transfer-encoding")) return ParseResult::Bad;

    size_t content_length = 0;
    if (auto it = req.headers.find("content-length"); it != req.headers.end()) {
        const std::string& v = it->second;
        if (v.empty() || v.size() > 12) return ParseResult::Bad;
        auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), content_length);
        if (ec != std::errc() || ptr != v.data() + v.size()) return ParseResult::Bad;
        if (content_length > limits.max_body_bytes) return ParseResult::TooLarge;
    }

    const size_t body_start = head_end + 4;
    if (data.size() - body_start < content_length) {
        return ParseResult::Incomplete;
    }
    req.body = std::string(data.substr(body_start, content_length));
    out = std::move(req);
    return ParseResult::Ok;
}

std::string_view reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Content Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

std::string serialize(const Response& r, bool head_only) {
    std::string out;
    out.reserve(512 + (head_only ? 0 : r.body.size()));
    out += "HTTP/1.1 " + std::to_string(r.status) + " " + std::string(reason_phrase(r.status)) + "\r\n";
    out += "Content-Type: " + r.content_type + "\r\n";
    out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    out += "Connection: close\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "X-Content-Type-Options: nosniff\r\n";
    out += "X-Frame-Options: DENY\r\n";
    out += "Referrer-Policy: no-referrer\r\n";
    out += "Content-Security-Policy: default-src 'self'; img-src 'self' blob:; connect-src 'self'; "
           "frame-ancestors 'none'; base-uri 'none'; form-action 'self'\r\n";
    for (const auto& [name, value] : r.headers) {
        out += name + ": " + value + "\r\n";
    }
    out += "\r\n";
    if (!head_only) {
        out += r.body;
    }
    return out;
}

bool url_decode(std::string_view in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%') {
            if (i + 2 >= in.size()) return false;
            const int hi = hex_value(in[i + 1]);
            const int lo = hex_value(in[i + 2]);
            if (hi < 0 || lo < 0) return false;
            out.push_back(static_cast<char>(hi * 16 + lo));
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return true;
}

std::map<std::string, std::string> parse_form(std::string_view text) {
    std::map<std::string, std::string> result;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t amp = text.find('&', pos);
        if (amp == std::string_view::npos) amp = text.size();
        const std::string_view pair = text.substr(pos, amp - pos);
        pos = amp + 1;
        if (pair.empty()) continue;

        const size_t eq = pair.find('=');
        std::string key, value;
        if (!url_decode(pair.substr(0, eq), key)) continue;
        if (eq != std::string_view::npos && !url_decode(pair.substr(eq + 1), value)) continue;
        if (key.empty()) continue;
        result[key] = value;
    }
    return result;
}

Response json(int status, std::string body) {
    Response r;
    r.status = status;
    r.content_type = "application/json; charset=utf-8";
    r.body = std::move(body);
    return r;
}

Response text(int status, std::string body) {
    Response r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

} // namespace gh::http
