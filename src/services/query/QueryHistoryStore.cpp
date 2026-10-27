#include "services/query/QueryHistoryStore.hpp"

#include "services/session/SessionRepository.hpp"

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <utility>

namespace mydb::services::query {

namespace {

using Json = nlohmann::json;
using session::SettingsError;

std::string lowerAscii(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return result;
}

std::int64_t toMillis(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

} // namespace

QueryHistoryStore::QueryHistoryStore(core::ISettingsStore& settings, int keepDays, std::size_t maxEntries)
    : m_settings(&settings), m_keepDays(keepDays), m_maxEntries(maxEntries) {}

void QueryHistoryStore::load() {
    m_entries.clear();
    m_writable = false; // só volta a gravar se a leitura terminar bem
    const auto text = m_settings->read(kDocument);
    if (!text) {
        m_writable = true;
        return;
    }
    try {
        const Json document = Json::parse(*text);
        if (document.value("schemaVersion", 0) > kSchemaVersion) {
            throw SettingsError("O histórico de queries foi salvo por uma versão mais nova do Mydbsql");
        }
        for (const Json& item : document.at("entries")) {
            HistoryEntry entry;
            entry.time =
                std::chrono::system_clock::time_point{std::chrono::milliseconds{item.at("time").get<std::int64_t>()}};
            entry.sessionId = item.value("session", std::string());
            entry.database = item.value("database", std::string());
            entry.sql = item.at("sql").get<std::string>();
            entry.duration = std::chrono::milliseconds{item.value("durationMs", std::int64_t{0})};
            entry.failed = item.value("failed", false);
            m_entries.push_back(std::move(entry));
        }
    } catch (const Json::exception& error) {
        m_entries.clear();
        throw SettingsError(std::string("Arquivo de histórico de queries inválido: ") + error.what());
    }
    std::ranges::stable_sort(m_entries, {}, &HistoryEntry::time);
    prune();
    m_writable = true;
}

void QueryHistoryStore::add(HistoryEntry entry) {
    m_entries.push_back(std::move(entry));
    prune();
    save();
}

std::vector<HistoryEntry> QueryHistoryStore::search(std::string_view text, std::string_view sessionId) const {
    const std::string needle = lowerAscii(text);
    std::vector<HistoryEntry> found;
    for (const HistoryEntry& entry : m_entries) {
        if (!sessionId.empty() && entry.sessionId != sessionId) {
            continue;
        }
        if (needle.empty() || lowerAscii(entry.sql).find(needle) != std::string::npos) {
            found.push_back(entry);
        }
    }
    std::ranges::reverse(found); // mais recente primeiro
    return found;
}

void QueryHistoryStore::clear() {
    m_entries.clear();
    m_writable = true;
    save();
}

void QueryHistoryStore::setKeepDays(int days) {
    m_keepDays = days;
    const std::size_t before = m_entries.size();
    prune();
    if (m_entries.size() != before) {
        save();
    }
}

void QueryHistoryStore::prune() {
    if (m_keepDays > 0) {
        const auto oldest = std::chrono::system_clock::now() - std::chrono::hours{24} * m_keepDays;
        std::erase_if(m_entries, [oldest](const HistoryEntry& entry) { return entry.time < oldest; });
    }
    if (m_entries.size() > m_maxEntries) {
        m_entries.erase(m_entries.begin(),
                        m_entries.begin() + static_cast<std::ptrdiff_t>(m_entries.size() - m_maxEntries));
    }
}

void QueryHistoryStore::save() const {
    if (!m_writable) {
        return;
    }
    Json entries = Json::array();
    for (const HistoryEntry& entry : m_entries) {
        entries.push_back(Json{{"time", toMillis(entry.time)},
                               {"session", entry.sessionId},
                               {"database", entry.database},
                               {"sql", entry.sql},
                               {"durationMs", entry.duration.count()},
                               {"failed", entry.failed}});
    }
    const Json document{{"schemaVersion", kSchemaVersion}, {"entries", std::move(entries)}};
    m_settings->write(kDocument, document.dump());
}

} // namespace mydb::services::query
