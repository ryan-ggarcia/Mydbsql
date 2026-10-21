#include "ui/panels/LogPanel.hpp"

#include "ui/common/QtText.hpp"

#include <QAction>
#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include <chrono>
#include <utility>

namespace mydb::ui {

namespace {

constexpr std::array<std::pair<core::LogCategory, const char*>, 6> kCategories{{
    {core::LogCategory::Info, QT_TRANSLATE_NOOP("LogPanel", "Informações")},
    {core::LogCategory::Sql, QT_TRANSLATE_NOOP("LogPanel", "SQL do aplicativo")},
    {core::LogCategory::UserSql, QT_TRANSLATE_NOOP("LogPanel", "SQL do usuário")},
    {core::LogCategory::Error, QT_TRANSLATE_NOOP("LogPanel", "Erros")},
    {core::LogCategory::Debug, QT_TRANSLATE_NOOP("LogPanel", "Depuração")},
    {core::LogCategory::Script, QT_TRANSLATE_NOOP("LogPanel", "Scripts")},
}};

std::size_t slot(core::LogCategory category) {
    return static_cast<std::size_t>(category);
}

} // namespace

LogPanel::LogPanel(services::log::LogBuffer& buffer, const services::Dispatcher& dispatcher, SessionName sessionName,
                   QWidget* parent)
    : QWidget(parent),
      m_buffer(buffer),
      m_sessionName(std::move(sessionName)),
      m_text(new QPlainTextEdit(this)),
      m_filterButton(new QToolButton(this)) {
    m_text->setReadOnly(true);
    m_text->setObjectName(QStringLiteral("logText"));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setMaximumBlockCount(static_cast<int>(m_buffer.capacity()));
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_text->setFont(mono);

    m_filterButton->setText(tr("Categorias"));
    m_filterButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_filterButton);
    for (const auto& [category, label] : kCategories) {
        QAction* action = menu->addAction(tr(label));
        action->setCheckable(true);
        action->setChecked(m_visible.at(slot(category)));
        connect(action, &QAction::toggled, this,
                [this, kind = category](bool visible) { setCategoryVisible(kind, visible); });
    }
    menu->addSeparator();
    QAction* clear = menu->addAction(tr("Limpar"));
    connect(clear, &QAction::triggered, this, [this] {
        m_entries.clear();
        m_text->clear();
    });
    m_filterButton->setMenu(menu);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    auto* bar = new QHBoxLayout();
    bar->addStretch(1);
    bar->addWidget(m_filterButton);
    layout->addLayout(bar);
    layout->addWidget(m_text, 1);

    // O LogBuffer avisa da thread de quem escreveu; a leitura acontece na thread da UI.
    QPointer<LogPanel> self(this);
    m_buffer.setListener([dispatcher, self] {
        dispatcher([self] {
            if (self) {
                self->pull();
            }
        });
    });
    pull();
}

LogPanel::~LogPanel() {
    m_buffer.setListener({});
}

void LogPanel::setCategoryVisible(core::LogCategory category, bool visible) {
    if (m_visible.at(slot(category)) != visible) {
        m_visible.at(slot(category)) = visible;
        rebuild();
    }
}

bool LogPanel::categoryVisible(core::LogCategory category) const {
    return m_visible.at(slot(category));
}

QString LogPanel::text() const {
    return m_text->toPlainText();
}

void LogPanel::pull() {
    std::vector<services::log::LogEntry> fresh = m_buffer.since(m_lastSequence);
    if (fresh.empty()) {
        return;
    }
    m_lastSequence = fresh.back().sequence;
    QStringList lines;
    for (auto& entry : fresh) {
        if (m_visible.at(slot(entry.category))) {
            lines.push_back(format(entry));
        }
        m_entries.push_back(std::move(entry));
    }
    while (m_entries.size() > m_buffer.capacity()) {
        m_entries.pop_front();
    }
    if (!lines.isEmpty()) {
        m_text->appendPlainText(lines.join(QLatin1Char('\n')));
    }
}

void LogPanel::rebuild() {
    QStringList lines;
    for (const auto& entry : m_entries) {
        if (m_visible.at(slot(entry.category))) {
            lines.push_back(format(entry));
        }
    }
    m_text->setPlainText(lines.join(QLatin1Char('\n')));
    m_text->moveCursor(QTextCursor::End);
}

QString LogPanel::format(const services::log::LogEntry& entry) const {
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(entry.time.time_since_epoch()).count();
    const QString time = QDateTime::fromMSecsSinceEpoch(milliseconds).toString(QStringLiteral("HH:mm:ss"));
    QString session;
    if (!entry.sessionId.empty() && m_sessionName) {
        session = m_sessionName(entry.sessionId);
    }
    QString text = toQt(entry.text);
    const bool comment = entry.category != core::LogCategory::Sql && entry.category != core::LogCategory::UserSql;
    if (comment) {
        text = QStringLiteral("/* %1 */").arg(text);
    } else if (!text.endsWith(QLatin1Char(';'))) {
        text += QLatin1Char(';');
    }
    return session.isEmpty() ? QStringLiteral("%1 %2").arg(time, text)
                             : QStringLiteral("%1 [%2] %3").arg(time, session, text);
}

} // namespace mydb::ui
