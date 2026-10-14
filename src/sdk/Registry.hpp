#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mydb::sdk {

class RegistryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Ponto de extensão (CLAUDE.md §2.4): guarda fábricas, não objetos. Nada é criado no registro;
/// create() é chamado no primeiro uso, o que mantém a inicialização leve (§2.2).
/// Não é thread-safe: o registro acontece na inicialização, na thread principal.
template <typename Interface>
class Registry {
public:
    using Factory = std::function<std::unique_ptr<Interface>()>;

    struct Entry {
        std::string id;
        std::string displayName;
        Factory factory;
    };

    /// Lança RegistryError se o id estiver vazio, repetido ou sem fábrica.
    void add(std::string id, std::string displayName, Factory factory) {
        if (id.empty()) {
            throw RegistryError("Registro sem id");
        }
        if (!factory) {
            throw RegistryError("Registro sem fábrica: " + id);
        }
        if (find(id) != nullptr) {
            throw RegistryError("Id já registrado: " + id);
        }
        m_entries.push_back(Entry{std::move(id), std::move(displayName), std::move(factory)});
    }

    [[nodiscard]] const Entry* find(std::string_view id) const noexcept {
        const auto found = std::ranges::find(m_entries, id, &Entry::id);
        return found == m_entries.end() ? nullptr : &*found;
    }

    /// Lança RegistryError se o id não existir ou se a fábrica devolver nullptr.
    [[nodiscard]] std::unique_ptr<Interface> create(std::string_view id) const {
        const Entry* entry = find(id);
        if (entry == nullptr) {
            throw RegistryError("Id não registrado: " + std::string(id));
        }
        std::unique_ptr<Interface> instance = entry->factory();
        if (!instance) {
            throw RegistryError("A fábrica de " + entry->id + " não criou o objeto");
        }
        return instance;
    }

    /// Na ordem de registro.
    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

private:
    std::vector<Entry> m_entries;
};

} // namespace mydb::sdk
