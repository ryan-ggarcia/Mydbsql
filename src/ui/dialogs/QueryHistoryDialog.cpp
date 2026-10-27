#include "ui/dialogs/QueryHistoryDialog.hpp"

#include "services/query/QueryHistoryStore.hpp"
#include "ui/common/ErrorText.hpp"
#include "ui/common/QtText.hpp"
#include "ui/editors/EditorPreferences.hpp"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <chrono>
#include <exception>
#include <utility>

namespace mydb::ui {

namespace {

constexpr int kSqlRole = Qt::UserRole + 1;
constexpr int kMaxKeepDays = 3650;
constexpr int kInitialWidth = 820;
constexpr int kInitialHeight = 480;

} // namespace

QueryHistoryDialog::QueryHistoryDialog(services::query::QueryHistoryStore& history, core::ISettingsStore& settings,
                                       SessionName sessionName, QWidget* parent)
    : QDialog(parent),
      m_history(&history),
      m_settings(&settings),
      m_sessionName(std::move(sessionName)),
      m_search(new QLineEdit(this)),
      m_list(new QTreeWidget(this)),
      m_keepDays(new QSpinBox(this)),
      m_status(new QLabel(this)) {
    setWindowTitle(tr("Histórico de queries"));
    setObjectName(QStringLiteral("queryHistoryDialog"));
    resize(kInitialWidth, kInitialHeight);

    m_search->setPlaceholderText(tr("Procurar no histórico"));
    m_search->setClearButtonEnabled(true);
    m_list->setHeaderLabels({tr("Quando"), tr("Sessão"), tr("Banco"), tr("Duração"), tr("SQL")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->header()->setStretchLastSection(true);
    m_keepDays->setRange(1, kMaxKeepDays);
    m_keepDays->setSuffix(tr(" dias"));
    m_keepDays->setValue(EditorPreferences::load(settings).historyKeepDays);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* use = buttons->addButton(tr("&Usar no editor"), QDialogButtonBox::AcceptRole);
    QPushButton* clear = buttons->addButton(tr("&Limpar histórico"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(use, &QPushButton::clicked, this, &QueryHistoryDialog::chooseCurrent);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, windowTitle(), tr("Apagar todo o histórico de queries?")) == QMessageBox::Yes) {
            clearHistory();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_search, &QLineEdit::textChanged, this, &QueryHistoryDialog::reload);
    connect(m_list, &QTreeWidget::itemActivated, this, &QueryHistoryDialog::chooseCurrent);
    connect(m_keepDays, &QSpinBox::editingFinished, this, [this] { setKeepDays(m_keepDays->value()); });

    auto* retention = new QHBoxLayout();
    retention->addWidget(new QLabel(tr("Manter por:"), this));
    retention->addWidget(m_keepDays);
    retention->addStretch(1);
    retention->addWidget(m_status);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
    layout->addLayout(retention);
    layout->addWidget(buttons);
    reload();
}

void QueryHistoryDialog::setSearchText(const QString& text) {
    m_search->setText(text);
}

void QueryHistoryDialog::reload() {
    m_list->clear();
    const QLocale locale;
    for (const services::query::HistoryEntry& entry : m_history->search(toStd(m_search->text()))) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(entry.time.time_since_epoch());
        const QString sql = toQt(entry.sql);
        auto* item = new QTreeWidgetItem(m_list); // a lista fica dona
        item->setText(0, locale.toString(QDateTime::fromMSecsSinceEpoch(ms.count()), QLocale::ShortFormat));
        item->setText(1, m_sessionName ? m_sessionName(entry.sessionId) : toQt(entry.sessionId));
        item->setText(2, toQt(entry.database));
        item->setText(3, tr("%1 ms").arg(entry.duration.count()));
        item->setText(4, sql.simplified());
        item->setToolTip(4, sql);
        item->setData(0, kSqlRole, sql);
        if (entry.failed) {
            item->setForeground(4, palette().brush(QPalette::Disabled, QPalette::Text));
        }
    }
    m_status->setText(tr("%n query(s)", nullptr, m_list->topLevelItemCount()));
}

void QueryHistoryDialog::setKeepDays(int days) {
    m_keepDays->setValue(days);
    try {
        m_history->setKeepDays(days);
        EditorPreferences preferences = EditorPreferences::load(*m_settings);
        preferences.historyKeepDays = days;
        preferences.save(*m_settings);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, windowTitle(), errorText(error));
    }
    reload();
}

void QueryHistoryDialog::clearHistory() {
    try {
        m_history->clear();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, windowTitle(), errorText(error));
    }
    reload();
}

void QueryHistoryDialog::chooseCurrent() {
    const QTreeWidgetItem* item = m_list->currentItem();
    if (item != nullptr) {
        Q_EMIT sqlChosen(item->data(0, kSqlRole).toString());
    }
}

} // namespace mydb::ui
