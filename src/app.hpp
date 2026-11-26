// The GhostGate web application= routes and JSON API (no login).
// Independent of sockets: takes an http::Request, returns an http::Response.

#pragma once

#include "controller.hpp"
#include "http.hpp"

#include <optional>
#include <string>

namespace gh {

struct StaticFiles {
    std::string index_html;
    std::string app_js;
    std::string style_css;
};

// Loads index.html, app.js and style.css from `dir`.
std::optional<StaticFiles> load_static_files(const std::wstring& dir, std::string& error);

class App {
public:
    App(StaticFiles files, Controller& controller);

    http::Response handle(const http::Request& request);

    static constexpr const char* kCsrfHeader = "X-Requested-With";
    static constexpr const char* kCsrfValue  = "GhostGate";

private:
    http::Response serve_static(const http::Request& r) const;
    http::Response api(const http::Request& r);
    http::Response restart();
    http::Response screenshot(const http::Request& r);

    StaticFiles files_;
    Controller& controller_;
};

} // namespace gh
