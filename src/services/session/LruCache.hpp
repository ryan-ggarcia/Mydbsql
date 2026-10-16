#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace mydb::services::session {

/// Cache com limite de itens que descarta o usado há mais tempo (CLAUDE.md §2.2, regra 8). Não é thread-safe.
template <typename Value>
class LruCache {
public:
    explicit LruCache(std::size_t capacity) : m_capacity(capacity == 0 ? 1 : capacity) {}

    /// nullptr se não houver. O ponteiro vale até a próxima alteração do cache.
    [[nodiscard]] const Value* find(std::string_view key) {
        const auto found = m_index.find(key);
        if (found == m_index.end()) {
            return nullptr;
        }
        m_order.splice(m_order.begin(), m_order, found->second);
        return &found->second->second;
    }

    const Value& put(std::string key, Value value) {
        if (const auto found = m_index.find(key); found != m_index.end()) {
            found->second->second = std::move(value);
            m_order.splice(m_order.begin(), m_order, found->second);
            return found->second->second;
        }
        m_order.emplace_front(std::move(key), std::move(value));
        m_index.emplace(m_order.front().first, m_order.begin());
        if (m_order.size() > m_capacity) {
            m_index.erase(m_order.back().first);
            m_order.pop_back();
        }
        return m_order.front().second;
    }

    void erase(std::string_view key) {
        if (const auto found = m_index.find(key); found != m_index.end()) {
            m_order.erase(found->second);
            m_index.erase(found);
        }
    }

    /// Remove as entradas cuja chave começa com `prefix`.
    void eraseWithPrefix(std::string_view prefix) {
        for (auto it = m_index.lower_bound(prefix); it != m_index.end() && it->first.starts_with(prefix);) {
            m_order.erase(it->second);
            it = m_index.erase(it);
        }
    }

    void clear() noexcept {
        m_index.clear();
        m_order.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_order.size(); }

private:
    using Entry = std::pair<std::string, Value>;

    std::size_t m_capacity;
    std::list<Entry> m_order; // mais recente na frente
    std::map<std::string, typename std::list<Entry>::iterator, std::less<>> m_index;
};

} // namespace mydb::services::session
