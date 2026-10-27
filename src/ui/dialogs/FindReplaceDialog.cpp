#include "ui/dialogs/FindReplaceDialog.hpp"

#include "ui/common/ErrorText.hpp"
#include "ui/editors/EditorPreferences.hpp"
#include "ui/editors/SqlEditor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

#include <exception>
#include <utility>

namespace mydb::ui {

FindReplaceDialog::FindReplaceDialog(EditorProvider editor, core::ISettingsStore& settings, QWidget* parent)
    : QDialog(parent),
      m_editor(std::move(editor)),
      m_settings(&settings),
      m_find(new QComboBox(this)),
      m_replace(new QComboBox(this)),
      m_caseSensitive(new QCheckBox(tr("Diferenciar &maiúsculas"), this)),
      m_wholeWords(new QCheckBox(tr("Palavras &inteiras"), this)),
      m_backward(new QCheckBox(tr("Para &trás"), this)),
      m_status(new QLabel(this)) {
    setWindowTitle(tr("Localizar e substituir"));
    setObjectName(QStringLiteral("findReplaceDialog"));
    const EditorPreferences preferences = EditorPreferences::load(settings);
    for (QComboBox* box : {m_find, m_replace}) {
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);
        box->setMinimumContentsLength(30);
    }
    m_find->setObjectName(QStringLiteral("findText"));
    m_replace->setObjectName(QStringLiteral("replaceText"));
    m_find->addItems(preferences.findHistory);
    m_replace->addItems(preferences.replaceHistory);

    auto* form = new QFormLayout();
    form->addRow(tr("&Localizar:"), m_find);
    form->addRow(tr("&Substituir por:"), m_replace);
    auto* buttons = new QDialogButtonBox(Qt::Vertical, this);
    QPushButton* next = buttons->addButton(tr("Localizar &próximo"), QDialogButtonBox::ActionRole);
    QPushButton* replace = buttons->addButton(tr("Substituir"), QDialogButtonBox::ActionRole);
    QPushButton* all = buttons->addButton(tr("Substituir &tudo"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    next->setDefault(true);
    connect(next, &QPushButton::clicked, this, [this] { (void)findNext(); });
    connect(replace, &QPushButton::clicked, this, [this] { (void)replaceNext(); });
    connect(all, &QPushButton::clicked, this, [this] { (void)replaceAll(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto* left = new QVBoxLayout();
    left->addLayout(form);
    left->addWidget(m_caseSensitive);
    left->addWidget(m_wholeWords);
    left->addWidget(m_backward);
    left->addWidget(m_status);
    left->addStretch(1);
    auto* layout = new QHBoxLayout(this);
    layout->addLayout(left, 1);
    layout->addWidget(buttons);

    if (SqlEditor* target = m_editor ? m_editor() : nullptr; target != nullptr && target->textCursor().hasSelection()) {
        setFindText(target->selectedSql().section(QLatin1Char('\n'), 0, 0));
    }
}

void FindReplaceDialog::setFindText(const QString& text) {
    m_find->setCurrentText(text);
}

void FindReplaceDialog::setReplaceText(const QString& text) {
    m_replace->setCurrentText(text);
}

QTextDocument::FindFlags FindReplaceDialog::flags() const {
    QTextDocument::FindFlags result;
    if (m_caseSensitive->isChecked()) {
        result |= QTextDocument::FindCaseSensitively;
    }
    if (m_wholeWords->isChecked()) {
        result |= QTextDocument::FindWholeWords;
    }
    if (m_backward->isChecked()) {
        result |= QTextDocument::FindBackward;
    }
    return result;
}

void FindReplaceDialog::remember(bool replacing) {
    EditorPreferences preferences = EditorPreferences::load(*m_settings);
    rememberText(preferences.findHistory, m_find->currentText());
    if (replacing) {
        rememberText(preferences.replaceHistory, m_replace->currentText());
    }
    try {
        preferences.save(*m_settings);
    } catch (const std::exception& error) {
        m_status->setText(tr("Histórico não gravado: %1").arg(errorText(error)));
    }
    const QString find = m_find->currentText();
    const QString replace = m_replace->currentText();
    m_find->clear();
    m_find->addItems(preferences.findHistory);
    m_find->setCurrentText(find);
    m_replace->clear();
    m_replace->addItems(preferences.replaceHistory);
    m_replace->setCurrentText(replace);
}

bool FindReplaceDialog::findNext() {
    SqlEditor* editor = m_editor ? m_editor() : nullptr;
    const QString text = m_find->currentText();
    if (editor == nullptr || text.isEmpty()) {
        return false;
    }
    remember(false);
    bool found = editor->find(text, flags());
    if (!found) {
        // Volta ao começo (ou ao fim, para trás) e tenta de novo.
        QTextCursor cursor = editor->textCursor();
        cursor.movePosition(m_backward->isChecked() ? QTextCursor::End : QTextCursor::Start);
        editor->setTextCursor(cursor);
        found = editor->find(text, flags());
    }
    m_status->setText(found ? QString() : tr("\"%1\" não foi encontrado.").arg(text));
    return found;
}

bool FindReplaceDialog::replaceNext() {
    SqlEditor* editor = m_editor ? m_editor() : nullptr;
    if (editor == nullptr) {
        return false;
    }
    remember(true);
    QTextCursor cursor = editor->textCursor();
    const Qt::CaseSensitivity sensitivity = m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection() && editor->selectedSql().compare(m_find->currentText(), sensitivity) == 0) {
        cursor.insertText(m_replace->currentText());
        editor->setTextCursor(cursor);
    }
    return findNext();
}

int FindReplaceDialog::replaceAll() {
    SqlEditor* editor = m_editor ? m_editor() : nullptr;
    if (editor == nullptr) {
        return 0;
    }
    remember(true);
    const int count = editor->replaceAll(m_find->currentText(), m_replace->currentText(), flags());
    m_status->setText(tr("%n ocorrência(s) substituída(s).", nullptr, count));
    return count;
}

} // namespace mydb::ui
