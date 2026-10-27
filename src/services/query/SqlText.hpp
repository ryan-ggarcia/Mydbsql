#pragma once

// Utilidades de texto SQL internas de services/query (autocomplete e execução).

#include "core/sql/SqlTokenizer.hpp"

#include <string>
#include <string_view>

namespace mydb::services::query {

[[nodiscard]] inline std::string lowerAscii(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

/// Nome de um identificador, sem aspas e com o fechamento duplicado desfeito (`a``b` → a`b).
[[nodiscard]] inline std::string identifierName(std::string_view text, const core::sql::SqlToken& token) {
    const std::string_view raw = text.substr(token.offset, token.length);
    if (token.kind != core::sql::SqlTokenKind::QuotedIdentifier || raw.size() < 2) {
        return std::string(raw);
    }
    const char close = raw.front() == '[' ? ']' : raw.front();
    const std::string_view body = raw.substr(1, token.complete ? raw.size() - 2 : raw.size() - 1);
    std::string name;
    for (std::size_t i = 0; i < body.size(); ++i) {
        name += body[i];
        if (body[i] == close && i + 1 < body.size() && body[i + 1] == close) {
            ++i;
        }
    }
    return name;
}

} // namespace mydb::services::query
