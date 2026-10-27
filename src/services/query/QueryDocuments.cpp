#include "services/query/QueryDocuments.hpp"

#include "services/session/SessionRepository.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <utility>

namespace mydb::services::query {

namespace {

using Json = nlohmann::json;
using session::SettingsError;

constexpr int kSchemaVersion = 1;

/// A lista `key` do documento, validando a versão; vazia se o documento não existe.
Json readList(core::ISettingsStore& settings, std::string_view document, const char* key, std::string_view what) {
    const auto text = settings.read(document);
    if (!text) {
        return Json::array();
    }
    try {
        Json parsed = Json::parse(*text);
        if (parsed.value("schemaVersion", 0) > kSchemaVersion) {
            throw SettingsError(std::string(what) + " foram salvos por uma versão mais nova do Mydbsql");
        }
        Json list = std::move(parsed.at(key));
        if (!list.is_array()) {
            throw SettingsError(std::string(what) + ": formato inválido");
        }
        return list;
    } catch (const Json::exception& error) {
        throw SettingsError(std::string(what) + ": arquivo inválido: " + error.what());
    }
}

void writeList(core::ISettingsStore& settings, std::string_view document, const char* key, Json list) {
    const Json root{{"schemaVersion", kSchemaVersion}, {key, std::move(list)}};
    settings.write(document, root.dump(2));
}

} // namespace

void SnippetStore::load() {
    m_snippets.clear();
    const Json list = readList(*m_settings, kDocument, "snippets", "Os snippets");
    try {
        for (const Json& item : list) {
            m_snippets.push_back(
                Snippet{.name = item.at("name").get<std::string>(), .sql = item.at("sql").get<std::string>()});
        }
    } catch (const Json::exception& error) {
        m_snippets.clear();
        throw SettingsError(std::string("Os snippets: arquivo inválido: ") + error.what());
    }
}

void SnippetStore::put(Snippet snippet) {
    const auto found = std::ranges::find(m_snippets, snippet.name, &Snippet::name);
    if (found != m_snippets.end()) {
        *found = std::move(snippet);
    } else {
        m_snippets.push_back(std::move(snippet));
    }
    save();
}

void SnippetStore::remove(std::string_view name) {
    if (std::erase_if(m_snippets, [name](const Snippet& snippet) { return snippet.name == name; }) > 0) {
        save();
    }
}

void SnippetStore::save() const {
    Json list = Json::array();
    for (const Snippet& snippet : m_snippets) {
        list.push_back(Json{{"name", snippet.name}, {"sql", snippet.sql}});
    }
    writeList(*m_settings, kDocument, "snippets", std::move(list));
}

std::vector<QueryTabState> QueryTabsStore::load() const {
    const Json list = readList(*m_settings, kDocument, "tabs", "As abas de query");
    std::vector<QueryTabState> tabs;
    try {
        for (const Json& item : list) {
            tabs.push_back(QueryTabState{.title = item.value("title", std::string()),
                                         .sessionId = item.value("session", std::string()),
                                         .sql = item.value("sql", std::string()),
                                         .file = item.value("file", std::string())});
        }
    } catch (const Json::exception& error) {
        throw SettingsError(std::string("As abas de query: arquivo inválido: ") + error.what());
    }
    return tabs;
}

void QueryTabsStore::save(const std::vector<QueryTabState>& tabs) const {
    Json list = Json::array();
    for (const QueryTabState& tab : tabs) {
        list.push_back(Json{{"title", tab.title}, {"session", tab.sessionId}, {"sql", tab.sql}, {"file", tab.file}});
    }
    writeList(*m_settings, kDocument, "tabs", std::move(list));
}

} // namespace mydb::services::query
