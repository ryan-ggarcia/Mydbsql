#include "services/session/FavoritesStore.hpp"

#include "services/session/SessionRepository.hpp"

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <utility>

namespace mydb::services::session {

namespace {

using Json = nlohmann::json;
using core::model::ObjectRef;
using core::model::ObjectType;

constexpr std::array<std::pair<ObjectType, std::string_view>, 8> kTypeNames{{
    {ObjectType::Table, "table"},
    {ObjectType::View, "view"},
    {ObjectType::Procedure, "procedure"},
    {ObjectType::Function, "function"},
    {ObjectType::Trigger, "trigger"},
    {ObjectType::Event, "event"},
    {ObjectType::Sequence, "sequence"},
    {ObjectType::Other, "other"},
}};

std::string_view typeName(ObjectType type) {
    for (const auto& [value, name] : kTypeNames) {
        if (value == type) {
            return name;
        }
    }
    return "other";
}

ObjectType typeFromName(std::string_view name) {
    for (const auto& [value, text] : kTypeNames) {
        if (text == name) {
            return value;
        }
    }
    return ObjectType::Other;
}

} // namespace

void FavoritesStore::load() {
    m_favorites.clear();
    const auto text = m_settings->read(kDocument);
    if (!text) {
        return;
    }
    try {
        const Json document = Json::parse(*text);
        if (document.value("schemaVersion", 0) > kSchemaVersion) {
            throw SettingsError("Os favoritos foram salvos por uma versão mais nova do Mydbsql");
        }
        for (const auto& [sessionId, objects] : document.at("sessions").items()) {
            auto& list = m_favorites[sessionId];
            for (const Json& item : objects) {
                list.push_back(ObjectRef{.database = item.at("database").get<std::string>(),
                                         .schema = item.value("schema", std::string()),
                                         .name = item.at("name").get<std::string>(),
                                         .type = typeFromName(item.at("type").get<std::string>())});
            }
        }
    } catch (const Json::exception& error) {
        m_favorites.clear();
        throw SettingsError(std::string("Arquivo de favoritos inválido: ") + error.what());
    }
}

bool FavoritesStore::contains(std::string_view sessionId, const ObjectRef& object) const {
    const auto found = m_favorites.find(sessionId);
    return found != m_favorites.end() && std::ranges::find(found->second, object) != found->second.end();
}

std::vector<ObjectRef> FavoritesStore::list(std::string_view sessionId) const {
    const auto found = m_favorites.find(sessionId);
    return found == m_favorites.end() ? std::vector<ObjectRef>{} : found->second;
}

bool FavoritesStore::set(std::string_view sessionId, const ObjectRef& object, bool favorite) {
    if (contains(sessionId, object) == favorite) {
        return false;
    }
    auto session = m_favorites.find(sessionId);
    if (favorite) {
        if (session == m_favorites.end()) {
            session = m_favorites.emplace(std::string(sessionId), std::vector<ObjectRef>{}).first;
        }
        session->second.push_back(object);
    } else {
        std::erase(session->second, object);
        if (session->second.empty()) {
            m_favorites.erase(session);
        }
    }
    save();
    return true;
}

void FavoritesStore::removeSession(std::string_view sessionId) {
    if (const auto found = m_favorites.find(sessionId); found != m_favorites.end()) {
        m_favorites.erase(found);
        save();
    }
}

void FavoritesStore::save() const {
    Json sessions = Json::object();
    for (const auto& [sessionId, objects] : m_favorites) {
        Json list = Json::array();
        for (const ObjectRef& object : objects) {
            list.push_back(Json{{"database", object.database},
                                {"schema", object.schema},
                                {"name", object.name},
                                {"type", typeName(object.type)}});
        }
        sessions[sessionId] = std::move(list);
    }
    const Json document{{"schemaVersion", kSchemaVersion}, {"sessions", std::move(sessions)}};
    m_settings->write(kDocument, document.dump(2));
}

} // namespace mydb::services::session
