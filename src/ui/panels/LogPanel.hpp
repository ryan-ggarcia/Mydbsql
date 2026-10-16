#pragma once

#include "services/common/Dispatcher.hpp"
#include "services/log/LogBuffer.hpp"

#include <QWidget>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

class QPlainTextEdit;
class QToolButton;

namespace mydb::ui {

/// Painel de log (CLAUDE.md §1.3): o SQL enviado e as mensagens das sessões, com filtro por categoria. Mostra no
/// máximo a capacidade do LogBuffer; o texto antigo sai da tela junto com o buffer.
class LogPanel final : public QWidget {
    Q_OBJECT

public:
    /// Nome de exibição de uma sessão (vazio se ela não existir mais).
    using SessionName = std::function<QString(const std::string& sessionId)>;

    LogPanel(services::log::LogBuffer& buffer, const services::Dispatcher& dispatcher, SessionName sessionName,
             QWidget* parent = nullptr);
    LogPanel(const LogPanel&) = delete;
    LogPanel& operator=(const LogPanel&) = delete;
    LogPanel(LogPanel&&) = delete;
    LogPanel& operator=(LogPanel&&) = delete;
    ~LogPanel() override;

    void setCategoryVisible(core::LogCategory category, bool visible);
    [[nodiscard]] bool categoryVisible(core::LogCategory category) const;
    /// Texto exibido (útil para testes).
    [[nodiscard]] QString text() const;

private:
    void pull();
    void rebuild();
    [[nodiscard]] QString format(const services::log::LogEntry& entry) const;

    services::log::LogBuffer& m_buffer;
    SessionName m_sessionName;
    std::uint64_t m_lastSequence{0};
    std::deque<services::log::LogEntry> m_entries;
    std::array<bool, 6> m_visible{true, true, true, true, false, true};
    QPlainTextEdit* m_text;
    QToolButton* m_filterButton;
};

} // namespace mydb::ui
