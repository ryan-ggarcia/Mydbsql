#include "sdk/EventBus.hpp"

#include <algorithm>
#include <utility>

namespace mydb::sdk {

EventBus::Subscription::Subscription(Subscription&& other) noexcept
    : m_bus(std::exchange(other.m_bus, nullptr)), m_token(std::exchange(other.m_token, 0)) {}

EventBus::Subscription& EventBus::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        m_bus = std::exchange(other.m_bus, nullptr);
        m_token = std::exchange(other.m_token, 0);
    }
    return *this;
}

EventBus::Subscription::~Subscription() { reset(); }

void EventBus::Subscription::reset() noexcept {
    if (m_bus != nullptr) {
        m_bus->unsubscribe(m_token);
        m_bus = nullptr;
        m_token = 0;
    }
}

EventBus::Subscription EventBus::subscribeRaw(std::string_view eventId, std::function<void(const void*)> callback) {
    const std::scoped_lock lock(m_mutex);
    const std::uint64_t token = m_nextToken++;
    m_handlers.push_back(Handler{.token = token, .eventId = std::string(eventId), .callback = std::move(callback)});
    return {this, token};
}

void EventBus::publishRaw(std::string_view eventId, const void* event) const {
    // Copia os handlers e chama fora da trava: um handler pode assinar, cancelar ou publicar.
    std::vector<std::function<void(const void*)>> targets;
    {
        const std::scoped_lock lock(m_mutex);
        for (const Handler& handler : m_handlers) {
            if (handler.eventId == eventId) {
                targets.push_back(handler.callback);
            }
        }
    }
    for (const auto& target : targets) {
        target(event);
    }
}

void EventBus::unsubscribe(std::uint64_t token) noexcept {
    const std::scoped_lock lock(m_mutex);
    std::erase_if(m_handlers, [token](const Handler& handler) { return handler.token == token; });
}

std::size_t EventBus::subscriberCount() const {
    const std::scoped_lock lock(m_mutex);
    return m_handlers.size();
}

} // namespace mydb::sdk
