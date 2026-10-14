#include "app/StartupReport.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace mydb::app {

namespace {

void appendEscaped(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                std::array<char, 8> buffer{};
                std::snprintf(buffer.data(), buffer.size(), "\\u%04x", static_cast<unsigned>(c));
                out += buffer.data();
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

} // namespace

std::string toJson(const StartupReport& report) {
    std::string out = "{\"platform\":";
    appendEscaped(out, report.platform);
    out += ",\"version\":";
    appendEscaped(out, report.version);
    // Um décimo de milissegundo basta e mantém o JSON estável entre plataformas.
    const double startupMs = std::round(report.startupMs * 10.0) / 10.0;
    std::array<char, 32> number{};
    std::snprintf(number.data(), number.size(), "%.1f", startupMs);
    out += ",\"startup_ms\":";
    out += number.data();
    out += ",\"rss_kb\":" + std::to_string(report.rssKb);
    out += ",\"anon_kb\":" + std::to_string(report.anonKb);
    out += ",\"libs_loaded\":" + std::to_string(report.libsLoaded);
    out += ",\"debug_build\":";
    out += report.debugBuild ? "true" : "false";
    out += '}';
    return out;
}

} // namespace mydb::app
