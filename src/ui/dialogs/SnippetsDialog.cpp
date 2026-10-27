#include "ui/dialogs/SnippetsDialog.hpp"

#include "services/query/QueryDocuments.hpp"
#include "ui/common/ErrorText.hpp"
#include "ui/common/QtText.hpp"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>

namespace mydb::ui {

namespace {

constexpr int kInitialWidth = 760;
constexpr int kInitialHeight = 440;

} // namespace

SnippetsDialog::SnippetsDialog(services::query::SnippetStore& snippets, QWidget* parent)
    : QDialog(parent),
      m_snippets(&snippets),
      m_list(new QListWidget(this)),
      m_name(new QLineEdit(this)),
      m_sql(new QPlainTextEdit(this)) {
    setWindowTitle(tr("Snippets"));
    setObjectName(QStringLiteral("snippetsDialog"));
    resize(kInitialWidth, kInitialHeight);
    m_sql->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_sql->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto* form = new QFormLayout();
    form->addRow(tr("&Nome:"), m_name);
    form->addRow(tr("&SQL:"), m_sql);
    auto* editor = new QHBoxLayout();
    editor->addWidget(m_list, 1);
    editor->addLayout(form, 2);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* fresh = buttons->addButton(tr("No&vo"), QDialogButtonBox::ActionRole);
    QPushButton* save = buttons->addButton(tr("&Salvar"), QDialogButtonBox::ActionRole);
    QPushButton* remove = buttons->addButton(tr("&Excluir"), QDialogButtonBox::DestructiveRole);
    QPushButton* insert = buttons->addButton(tr("&Inserir no editor"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(fresh, &QPushButton::clicked, this, [this] {
        m_list->setCurrentRow(-1);
        m_name->clear();
        m_sql->clear();
        m_name->setFocus();
    });
    connect(save, &QPushButton::clicked, this, [this] { (void)saveCurrent(m_name->text(), m_sql->toPlainText()); });
    connect(remove, &QPushButton::clicked, this, &SnippetsDialog::removeCurrent);
    connect(insert, &QPushButton::clicked, this, [this] {
        if (!m_sql->toPlainText().isEmpty()) {
            Q_EMIT sqlChosen(m_sql->toPlainText());
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_list, &QListWidget::currentRowChanged, this, &SnippetsDialog::showSelected);
    connect(m_list, &QListWidget::itemActivated, this, [this] { Q_EMIT sqlChosen(m_sql->toPlainText()); });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(editor, 1);
    layout->addWidget(buttons);
    reload();
}

void SnippetsDialog::reload(const QString& select) {
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const services::query::Snippet& snippet : m_snippets->snippets()) {
        m_list->addItem(toQt(snippet.name));
        if (toQt(snippet.name) == select) {
            m_list->setCurrentRow(m_list->count() - 1);
        }
    }
}

void SnippetsDialog::showSelected() {
    const int row = m_list->currentRow();
    const auto& all = m_snippets->snippets();
    if (row < 0 || static_cast<std::size_t>(row) >= all.size()) {
        return;
    }
    const services::query::Snippet& snippet = all[static_cast<std::size_t>(row)];
    m_name->setText(toQt(snippet.name));
    m_sql->setPlainText(toQt(snippet.sql));
}

bool SnippetsDialog::saveCurrent(const QString& name, const QString& sql) {
    if (name.trimmed().isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Dê um nome ao snippet."));
        return false;
    }
    try {
        m_snippets->put(services::query::Snippet{.name = toStd(name.trimmed()), .sql = toStd(sql)});
    } catch (const std::exception& error) {
        QMessageBox::warning(this, windowTitle(), errorText(error));
        return false;
    }
    reload(name.trimmed());
    m_name->setText(name.trimmed());
    m_sql->setPlainText(sql);
    return true;
}

void SnippetsDialog::removeCurrent() {
    const QListWidgetItem* item = m_list->currentItem();
    if (item == nullptr) {
        return;
    }
    try {
        m_snippets->remove(toStd(item->text()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, windowTitle(), errorText(error));
    }
    m_name->clear();
    m_sql->clear();
    reload();
}

} // namespace mydb::ui
