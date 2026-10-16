#pragma once

#include "core/interfaces/ISettingsStore.hpp"
#include "core/model/ObjectRef.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::services::session {

/// Objetos marcados como favoritos na árvore, por sessão. Persiste no documento "favorites" do ISettingsStore.
/// Não é thread-safe: usado pela thread da UI.
class FavoritesStore {
public:
    static constexpr int kSchemaVersion = 1;
    static constexpr std::string_view kDocument = "favorites";

    explicit FavoritesStore(core::ISettingsStore& settings) : m_settings(&settings) {}

    /// Lança SettingsError se o documento estiver corrompido ou vier de uma versão mais nova do app.
    void load();

    [[nodiscard]] bool contains(std::string_view sessionId, const core::model::ObjectRef& object) const;
    [[nodiscard]] std::vector<core::model::ObjectRef> list(std::string_view sessionId) const;

    /// Marca ou desmarca e grava na hora. Devolve se algo mudou.
    bool set(std::string_view sessionId, const core::model::ObjectRef& object, bool favorite);
    /// Esquece os favoritos de uma sessão apagada.
    void removeSession(std::string_view sessionId);

private:
    void save() const;

    core::ISettingsStore* m_settings;
    std::map<std::string, std::vector<core::model::ObjectRef>, std::less<>> m_favorites;
};

} // namespace mydb::services::session
