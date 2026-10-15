#include "services/session/SessionRepository.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <random>
#include <set>

namespace mydb::services::session {

namespace {

using nlohmann::json;
using TimePoint = SessionStatistics::TimePoint;

std::string newId() {
    // 128 bits aleatórios em hexadecimal: estável, único e sem relação com o nome da sessão.
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    constexpr std::array<char, 16> kHex{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string id;
    id.reserve(32);
    for (int part = 0; part < 2; ++part) {
        std::uint64_t value = engine();
        for (int i = 0; i < 16; ++i) {
            id += kHex.at(value & 0xFU);
            value >>= 4U;
        }
    }
    return id;
}

json timeToJson(const std::optional<TimePoint>& time) {
    if (!time) {
        return nullptr;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(time->time_since_epoch()).count();
}

std::optional<TimePoint> timeFromJson(const json& value) {
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    return TimePoint{std::chrono::seconds{value.get<std::int64_t>()}};
}

std::string_view sslModeName(core::SslOptions::Mode mode) {
    using Mode = core::SslOptions::Mode;
    switch (mode) {
    case Mode::Disabled:
        return "disabled";
    case Mode::Required:
        return "required";
    case Mode::VerifyCa:
        return "verify-ca";
    case Mode::VerifyIdentity:
        return "verify-identity";
    case Mode::Preferred:
        break;
    }
    return "preferred";
}

core::SslOptions::Mode sslModeFrom(std::string_view name) {
    using Mode = core::SslOptions::Mode;
    if (name == "disabled") {
        return Mode::Disabled;
    }
    if (name == "required") {
        return Mode::Required;
    }
    if (name == "verify-ca") {
        return Mode::VerifyCa;
    }
    if (name == "verify-identity") {
        return Mode::VerifyIdentity;
    }
    return Mode::Preferred;
}

std::string pathText(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path pathFrom(const json& value) {
    const auto text = value.get<std::string>();
    return {std::u8string(text.begin(), text.end())};
}

json paramsToJson(const core::ConnectionParams& p) {
    json j{{"driver", p.driverId},
           {"host", p.host},
           {"port", p.port},
           {"user", p.user},
           {"database", p.defaultDatabase},
           {"queryTimeoutSeconds", p.queryTimeout.count()},
           {"keepAliveSeconds", p.keepAlive.count()},
           {"startupScript", p.startupScript},
           {"ignoreDatabasePattern", p.ignoreDatabasePattern},
           {"compressed", p.compressed},
           {"localTimeZone", p.localTimeZone},
           {"ssl", nullptr},
           {"ssh", nullptr}};
    if (p.ssl) {
        j["ssl"] = {{"mode", sslModeName(p.ssl->mode)},
                    {"privateKey", pathText(p.ssl->privateKey)},
                    {"certificate", pathText(p.ssl->certificate)},
                    {"certificateAuthority", pathText(p.ssl->certificateAuthority)},
                    {"cipher", p.ssl->cipher}};
    }
    if (p.ssh) {
        j["ssh"] = {{"host", p.ssh->host},
                    {"port", p.ssh->port},
                    {"user", p.ssh->user},
                    {"privateKey", pathText(p.ssh->privateKey)},
                    {"localPort", p.ssh->localPort ? json(*p.ssh->localPort) : json(nullptr)},
                    {"timeoutSeconds", p.ssh->timeout.count()}};
    }
    return j;
}

core::ConnectionParams paramsFromJson(const json& j) {
    core::ConnectionParams p;
    p.driverId = j.value("driver", std::string("mysql"));
    p.host = j.value("host", std::string());
    p.port = j.value("port", std::uint16_t{0});
    p.user = j.value("user", std::string());
    p.defaultDatabase = j.value("database", std::string());
    p.queryTimeout = std::chrono::seconds{j.value("queryTimeoutSeconds", std::int64_t{0})};
    p.keepAlive = std::chrono::seconds{j.value("keepAliveSeconds", std::int64_t{0})};
    p.startupScript = j.value("startupScript", std::string());
    p.ignoreDatabasePattern = j.value("ignoreDatabasePattern", std::string());
    p.compressed = j.value("compressed", false);
    p.localTimeZone = j.value("localTimeZone", false);
    if (const auto ssl = j.find("ssl"); ssl != j.end() && ssl->is_object()) {
        core::SslOptions options;
        options.mode = sslModeFrom(ssl->value("mode", std::string("preferred")));
        options.privateKey = pathFrom(ssl->value("privateKey", json("")));
        options.certificate = pathFrom(ssl->value("certificate", json("")));
        options.certificateAuthority = pathFrom(ssl->value("certificateAuthority", json("")));
        options.cipher = ssl->value("cipher", std::string());
        p.ssl = std::move(options);
    }
    if (const auto ssh = j.find("ssh"); ssh != j.end() && ssh->is_object()) {
        core::SshTunnelOptions options;
        options.host = ssh->value("host", std::string());
        options.port = ssh->value("port", std::uint16_t{22});
        options.user = ssh->value("user", std::string());
        options.privateKey = pathFrom(ssh->value("privateKey", json("")));
        if (const auto local = ssh->find("localPort"); local != ssh->end() && local->is_number_unsigned()) {
            options.localPort = local->get<std::uint16_t>();
        }
        options.timeout = std::chrono::seconds{ssh->value("timeoutSeconds", std::int64_t{10})};
        p.ssh = std::move(options);
    }
    return p;
}

json sessionToJson(const Session& s) {
    return {{"id", s.id},
            {"name", s.name},
            {"folder", s.folder},
            {"color", s.color},
            {"comment", s.comment},
            {"savePassword", s.savePassword},
            {"connection", paramsToJson(s.params)},
            {"statistics",
             {{"connections", s.statistics.connections},
              {"created", timeToJson(s.statistics.created)},
              {"lastConnected", timeToJson(s.statistics.lastConnected)}}}};
}

Session sessionFromJson(const json& j) {
    Session s;
    s.id = j.at("id").get<std::string>();
    s.name = j.value("name", std::string());
    s.folder = j.value("folder", std::string());
    s.color = j.value("color", std::string());
    s.comment = j.value("comment", std::string());
    s.savePassword = j.value("savePassword", true);
    s.params = paramsFromJson(j.value("connection", json::object()));
    s.params.sessionId = s.id;
    if (const auto stats = j.find("statistics"); stats != j.end() && stats->is_object()) {
        s.statistics.connections = stats->value("connections", std::uint64_t{0});
        s.statistics.created = timeFromJson(stats->value("created", json()));
        s.statistics.lastConnected = timeFromJson(stats->value("lastConnected", json()));
    }
    return s;
}

/// Leva o documento da versão `from` até kSchemaVersion. Cada mudança de formato ganha um passo aqui,
/// para que configurações antigas continuem abrindo.
void migrate(json& document, int from) {
    (void)document;
    if (from < 1) {
        throw SettingsError("Documento de sessões sem versão de esquema");
    }
}

bool isSameOrInside(std::string_view folder, std::string_view root) {
    return folder == root || (folder.size() > root.size() && folder.starts_with(root) && folder[root.size()] == '/');
}

} // namespace

void SessionRepository::load() {
    m_sessions.clear();
    m_folders.clear();
    const std::optional<std::string> text = m_settings->read(kDocument);
    if (!text) {
        return;
    }
    try {
        json document = json::parse(*text);
        const int version = document.value("schemaVersion", 0);
        if (version > kSchemaVersion) {
            throw SettingsError("As sessões foram salvas por uma versão mais nova do Mydbsql (esquema " +
                                std::to_string(version) + "); atualize o aplicativo");
        }
        if (version < kSchemaVersion) {
            migrate(document, version);
        }
        for (const json& folder : document.value("folders", json::array())) {
            m_folders.push_back(folder.get<std::string>());
        }
        for (const json& session : document.value("sessions", json::array())) {
            m_sessions.push_back(sessionFromJson(session));
        }
    } catch (const json::exception& error) {
        m_sessions.clear();
        m_folders.clear();
        throw SettingsError(std::string("Arquivo de sessões inválido: ") + error.what());
    }
}

void SessionRepository::save() const {
    json document{{"schemaVersion", kSchemaVersion}, {"folders", m_folders}, {"sessions", json::array()}};
    for (const Session& session : m_sessions) {
        document["sessions"].push_back(sessionToJson(session));
    }
    m_settings->write(kDocument, document.dump(2));
}

const Session* SessionRepository::find(std::string_view id) const noexcept {
    const auto found = std::ranges::find(m_sessions, id, &Session::id);
    return found == m_sessions.end() ? nullptr : &*found;
}

Session& SessionRepository::at(std::string_view id) {
    const auto found = std::ranges::find(m_sessions, id, &Session::id);
    if (found == m_sessions.end()) {
        throw std::out_of_range("Sessão inexistente: " + std::string(id));
    }
    return *found;
}

std::vector<std::string> SessionRepository::folders() const {
    std::set<std::string> all(m_folders.begin(), m_folders.end());
    for (const Session& session : m_sessions) {
        std::string_view folder = session.folder;
        while (!folder.empty()) {
            all.emplace(folder);
            const std::size_t slash = folder.rfind('/');
            folder = slash == std::string_view::npos ? std::string_view() : folder.substr(0, slash);
        }
    }
    return {all.begin(), all.end()};
}

Session& SessionRepository::create(std::string name, std::string folder) {
    Session session;
    session.id = newId();
    session.name = std::move(name);
    session.folder = std::move(folder);
    session.params.sessionId = session.id;
    session.params.driverId = "mysql";
    session.statistics.created = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    m_sessions.push_back(std::move(session));
    return m_sessions.back();
}

void SessionRepository::update(const Session& session) {
    Session& existing = at(session.id);
    existing = session;
    existing.params.sessionId = existing.id;
}

void SessionRepository::remove(std::string_view id) {
    const auto found = std::ranges::find(m_sessions, id, &Session::id);
    if (found == m_sessions.end()) {
        return;
    }
    m_credentials->erase(passwordKey(id));
    m_credentials->erase(sshPasswordKey(id));
    m_sessions.erase(found);
}

Session& SessionRepository::duplicate(std::string_view id) {
    Session copy = at(id);
    const std::string originalId = copy.id;
    copy.id = newId();
    copy.name += " (cópia)";
    copy.params.sessionId = copy.id;
    copy.statistics = SessionStatistics{};
    copy.statistics.created = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    for (const auto& keyFor : {&passwordKey, &sshPasswordKey}) {
        if (const auto secret = m_credentials->read(keyFor(originalId))) {
            m_credentials->write(keyFor(copy.id), *secret);
        }
    }
    m_sessions.push_back(std::move(copy));
    return m_sessions.back();
}

void SessionRepository::addFolder(std::string path) {
    if (!path.empty() && std::ranges::find(m_folders, path) == m_folders.end()) {
        m_folders.push_back(std::move(path));
    }
}

void SessionRepository::renameFolder(std::string_view from, std::string_view to) {
    const auto rename = [from, to](std::string& folder) {
        if (isSameOrInside(folder, from)) {
            folder = std::string(to) + folder.substr(from.size());
        }
    };
    for (std::string& folder : m_folders) {
        rename(folder);
    }
    for (Session& session : m_sessions) {
        rename(session.folder);
    }
}

void SessionRepository::removeFolder(std::string_view path) {
    if (std::ranges::any_of(m_sessions, [path](const Session& s) { return isSameOrInside(s.folder, path); })) {
        throw std::logic_error("A pasta " + std::string(path) + " ainda tem sessões");
    }
    std::erase_if(m_folders, [path](const std::string& folder) { return isSameOrInside(folder, path); });
}

void SessionRepository::setPassword(std::string_view id, std::string_view password) {
    m_credentials->write(passwordKey(at(id).id), password);
}

void SessionRepository::clearPassword(std::string_view id) {
    m_credentials->erase(passwordKey(id));
}

void SessionRepository::setSshPassword(std::string_view id, std::string_view password) {
    m_credentials->write(sshPasswordKey(at(id).id), password);
}

void SessionRepository::clearSshPassword(std::string_view id) {
    m_credentials->erase(sshPasswordKey(id));
}

void SessionRepository::recordConnection(std::string_view id, SessionStatistics::TimePoint when) {
    Session& session = at(id);
    ++session.statistics.connections;
    session.statistics.lastConnected = std::chrono::time_point_cast<std::chrono::seconds>(when);
}

std::string SessionRepository::exportJson(std::span<const std::string> ids) const {
    json document{{"format", "mydbsql-sessions"}, {"schemaVersion", kSchemaVersion}, {"sessions", json::array()}};
    for (const Session& session : m_sessions) {
        if (ids.empty() || std::ranges::find(ids, session.id) != ids.end()) {
            json item = sessionToJson(session);
            item.erase("statistics");
            document["sessions"].push_back(std::move(item));
        }
    }
    return document.dump(2);
}

std::size_t SessionRepository::importJson(std::string_view text) {
    std::vector<Session> imported;
    try {
        const json document = json::parse(text);
        if (document.value("format", std::string()) != "mydbsql-sessions") {
            throw SettingsError("O arquivo não é uma exportação de sessões do Mydbsql");
        }
        if (document.value("schemaVersion", 0) > kSchemaVersion) {
            throw SettingsError("A exportação veio de uma versão mais nova do Mydbsql; atualize o aplicativo");
        }
        for (const json& item : document.value("sessions", json::array())) {
            Session session = sessionFromJson(item);
            session.id = newId();
            session.params.sessionId = session.id;
            session.statistics = SessionStatistics{};
            session.statistics.created =
                std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            imported.push_back(std::move(session));
        }
    } catch (const json::exception& error) {
        throw SettingsError(std::string("Arquivo de sessões inválido: ") + error.what());
    }
    m_sessions.insert(m_sessions.end(), imported.begin(), imported.end());
    return imported.size();
}

} // namespace mydb::services::session
