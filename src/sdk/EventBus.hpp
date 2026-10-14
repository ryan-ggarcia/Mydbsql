#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::sdk {

/// Um evento declara um id estável. Por ele, e não por RTTI, o barramento casa publicadores e assinantes:
/// type_info não é confiável entre bibliotecas dinâmicas com símbolos ocultos.
template <typename Event>
concept BusEvent = requires {
    { Event::kEventId } -> std::convertible_to<std::string_view>;
};

/// Comunicação entre módulos sem dependência direta entre eles (CLAUDE.md §2.4). Os handlers rodam
/// na thread de quem publica; quem precisa da UI deve repassar o trabalho para a thread dela.
class EventBus {
public:
    /// Cancela a assinatura ao ser destruída. O EventBus precisa viver mais que as suas assinaturas.
    class Subscription {
    public:
        Subscription() = default;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        ~Subscription();

        void reset() noexcept;
        [[nodiscard]] bool active() const noexcept { return m_bus != nullptr; }

    private:
        friend class EventBus;
        Subscription(EventBus* bus, std::uint64_t token) noexcept : m_bus(bus), m_token(token) {}

        EventBus* m_bus{nullptr};
        std::uint64_t m_token{0};
    };

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;
    ~EventBus() = default;

    template <BusEvent Event>
    [[nodiscard]] Subscription subscribe(std::function<void(const Event&)> handler) {
        return subscribeRaw(Event::kEventId, [handler = std::move(handler)](const void* event) {
            handler(*static_cast<const Event*>(event));
        });
    }

    template <BusEvent Event>
    void publish(const Event& event) const {
        publishRaw(Event::kEventId, &event);
    }

    [[nodiscard]] std::size_t subscriberCount() const;

private:
    struct Handler {
        std::uint64_t token;
        std::string eventId;
        std::function<void(const void*)> callback;
    };

    Subscription subscribeRaw(std::string_view eventId, std::function<void(const void*)> callback);
    void publishRaw(std::string_view eventId, const void* event) const;
    void unsubscribe(std::uint64_t token) noexcept;

    mutable std::mutex m_mutex;
    std::vector<Handler> m_handlers;
    std::uint64_t m_nextToken{1};
};

} // namespace mydb::sdk
