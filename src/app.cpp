#include "app.hpp"
#include "util.hpp"

#include <charconv>
#include <fstream>
#include <sstream>

namespace gh {
namespace {

using http::Response;

bool read_file(const std::wstring& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

Response error_json(int status, const std::string& message) {
    return http::json(status, "{\"error\":" + json_string(message) + "}");
}

} // namespace

std::optional<StaticFiles> load_static_files(const std::wstring& dir, std::string& error) {
    StaticFiles f;
    const std::pair<const wchar_t*, std::string*> files[] = {
        {L"index.html", &f.index_html}, {L"app.js", &f.app_js}, {L"style.css", &f.style_css}};
    for (const auto& [name, target] : files) {
        const std::wstring path = dir + L"\\" + name;
        if (!read_file(path, *target)) {
            error = "missing web file: " + to_utf8(path);
            return std::nullopt;
        }
    }
    return f;
}

App::App(StaticFiles files, Controller& controller)
    : files_(std::move(files)), controller_(controller) {}

Response App::handle(const http::Request& r) {
    if (r.path.rfind("/api/", 0) == 0) {
        return api(r);
    }
    return serve_static(r);
}

Response App::serve_static(const http::Request& r) const {
    struct Asset { const char* path; const std::string* body; const char* type; };
    const Asset assets[] = {
        {"/",           &files_.index_html, "text/html; charset=utf-8"},
        {"/index.html", &files_.index_html, "text/html; charset=utf-8"},
        {"/app.js",     &files_.app_js,     "text/javascript; charset=utf-8"},
        {"/style.css",  &files_.style_css,  "text/css; charset=utf-8"},
    };
    // A fixed allow-list: no filesystem paths are built from user input.
    for (const Asset& a : assets) {
        if (r.path == a.path) {
            if (r.method != "GET" && r.method != "HEAD") {
                Response res = http::text(405, "method not allowed\n");
                res.headers.emplace_back("Allow", "GET, HEAD");
                return res;
            }
            Response res;
            res.content_type = a.type;
            res.body = *a.body;
            return res;
        }
    }
    return http::text(404, "not found\n");
}

Response App::api(const http::Request& r) {
    struct Route { const char* path; const char* method; };
    static constexpr Route routes[] = {
        {"/api/status", "GET"}, {"/api/restart", "POST"}, {"/api/screenshot", "GET"},
    };
    const Route* route = nullptr;
    for (const Route& candidate : routes) {
        if (r.path == candidate.path) route = &candidate;
    }
    if (!route) {
        return error_json(404, "not found");
    }
    if (r.method != route->method) {
        Response res = error_json(405, "method not allowed");
        res.headers.emplace_back("Allow", route->method);
        return res;
    }

    // CSRF defence: browsers cannot attach a custom header to a cross-site
    // request without a CORS preflight, which this server never approves.
    if (r.method == "POST" && r.header(kCsrfHeader) != kCsrfValue) {
        return error_json(403, "missing X-Requested-With header");
    }

    if (r.path == std::string_view("/api/status"))  return http::json(200, controller_.status_json());
    if (r.path == std::string_view("/api/restart")) return restart();
    return screenshot(r);
}

Response App::restart() {
    const Controller::RestartResult result = controller_.restart();
    if (result.status != 200) {
        return error_json(result.status, result.error);
    }
    return http::json(200, "{\"ok\":true,\"pid\":" + std::to_string(result.pid) + "}");
}

Response App::screenshot(const http::Request& r) {
    int width = Controller::kDefaultWidth;
    const auto query = http::parse_form(r.query);
    if (auto it = query.find("width"); it != query.end()) {
        int parsed = 0;
        const std::string& v = it->second;
        auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), parsed);
        if (ec != std::errc() || ptr != v.data() + v.size()) {
            return error_json(400, "width must be a number");
        }
        width = parsed;
    }
    const auto jpeg = controller_.screenshot(width);
    if (!jpeg) {
        return error_json(500, "screen capture failed (is the desktop locked?)");
    }
    Response res;
    res.content_type = "image/jpeg";
    res.body.assign(jpeg->begin(), jpeg->end());
    return res;
}

} // namespace gh
