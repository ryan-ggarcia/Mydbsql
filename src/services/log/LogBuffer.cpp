#include "services/log/LogBuffer.hpp"

#include <algorithm>
#include <utility>

namespace mydb::services::log {

namespace {

/// Corta sem partir um caractere UTF-8 ao meio.
std::string truncated(std::string_view text) {
    if (text.size() <= LogBuffer::kMaxTextLength) {
        return std::string(text);
    }
    std::size_t end = LogBuffer::kMaxTextLength;
    constexpr unsigned char kContinuationMask = 0xC0;
    constexpr unsigned char kContinuation = 0x80;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & kContinuationMask) == kContinuation) {
        --end;
    }
    std::string result(text.substr(0, end));
    result += "…";
    return result;
}

} // namespace

LogBuffer::LogBuffer(std::size_t capacity) : m_capacity(std::max<std::size_t>(capacity, 1)) {}

void LogBuffer::append(core::LogCategory category, std::string_view sessionId, std::string_view text) {
    std::function<void()> notify;
    {
        const std::scoped_lock lock(m_mutex);
        m_entries.push_back(LogEntry{.sequence = m_nextSequence++,
                                     .time = std::chrono::system_clock::now(),
                                     .category = category,
                                     .sessionId = std::string(sessionId),
                                     .text = truncated(text)});
        if (m_entries.size() > m_capacity) {
            m_entries.pop_front();
        }
        if (!m_notified && m_listener) {
            m_notified = true;
            notify = m_listener;
        }
    }
    if (notify) {
        notify();
    }
}

std::vector<LogEntry> LogBuffer::since(std::uint64_t after) const {
    const std::scoped_lock lock(m_mutex);
    m_notified = false;
    const auto first = std::ranges::upper_bound(m_entries, after, {}, &LogEntry::sequence);
    return {first, m_entries.end()};
}

void LogBuffer::setListener(std::function<void()> listener) {
    const std::scoped_lock lock(m_mutex);
    m_listener = std::move(listener);
    m_notified = false;
}

void LogBuffer::clear() {
    const std::scoped_lock lock(m_mutex);
    m_entries.clear();
}

std::size_t LogBuffer::size() const {
    const std::scoped_lock lock(m_mutex);
    return m_entries.size();
}

void SessionLogger::log(core::LogCategory category, std::string_view text) noexcept {
    if (category == core::LogCategory::Sql && m_userThread.load() == std::this_thread::get_id()) {
        category = core::LogCategory::UserSql;
    }
    try {
        m_buffer->append(category, m_sessionId, text);
    } catch (...) { // NOLINT(bugprone-empty-catch): sem memória para o log, a linha se perde
    }
}

void SessionLogger::setUserSql(bool on) noexcept {
    m_userThread = on ? std::this_thread::get_id() : std::thread::id();
}

} // namespace mydb::services::log
