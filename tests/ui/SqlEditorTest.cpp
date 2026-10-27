// Editor SQL (ADR-0013) com Qt Test na plataforma offscreen: realce, comentar, mover linhas, dobra, localizar e
// substituir com histórico, autocomplete e tempo de resposta num arquivo de 50 mil linhas.

#include "ui/editors/SqlEditor.hpp"

#include "infra/settings/MemorySettingsStore.hpp"
#include "services/query/CompletionIndex.hpp"
#include "ui/dialogs/FindReplaceDialog.hpp"
#include "ui/editors/EditorPreferences.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QElapsedTimer>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace {

using mydb::core::model::DbObject;
using mydb::core::model::ObjectRef;
using mydb::core::model::ObjectType;
using mydb::core::sql::SqlSyntax;
using mydb::services::query::CompletionIndex;
using mydb::ui::SqlEditor;

constexpr int kLargeFileLines = 50'000;

std::shared_ptr<const CompletionIndex> makeIndex(int extraTables = 0) {
    auto index = std::make_shared<CompletionIndex>(SqlSyntax::mysql());
    index->setKeywords(mydb::services::query::sqlKeywords("mysql"));
    index->setDatabases({"app", "logs"}, "app");
    std::vector<DbObject> objects;
    const auto add = [&objects](std::string name) {
        DbObject object;
        object.ref = ObjectRef{.database = "app", .schema = {}, .name = std::move(name), .type = ObjectType::Table};
        objects.push_back(std::move(object));
    };
    add("users");
    add("orders");
    for (int i = 0; i < extraTables; ++i) {
        add("table_" + std::to_string(i));
    }
    index->setObjects("app", objects);
    return index;
}

void prepare(SqlEditor& editor) {
    editor.setSyntax(SqlSyntax::mysql(), mydb::services::query::sqlKeywords("mysql"));
    editor.resize(800, 600);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));
}

void moveToEnd(SqlEditor& editor) {
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
}

QStringList completionTexts(const SqlEditor& editor) {
    QStringList texts;
    const QAbstractItemModel* model = editor.completer()->completionModel();
    for (int row = 0; row < model->rowCount(); ++row) {
        texts << model->index(row, 0).data().toString();
    }
    return texts;
}

const QTextLayout::FormatRange* formatAt(const QTextBlock& block, int position) {
    const auto& formats = block.layout()->formats();
    const auto found = std::ranges::find_if(formats, [position](const QTextLayout::FormatRange& range) {
        return range.start <= position && position < range.start + range.length;
    });
    return found == formats.end() ? nullptr : &*found;
}

} // namespace

class SqlEditorTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void highlightsKeywordsAndCommentsAcrossLines() {
        SqlEditor editor;
        prepare(editor);
        editor.setPlainText(QStringLiteral("/* começo\nfim */ SELECT 'texto\nainda' FROM t"));
        const QTextBlock second = editor.document()->findBlockByNumber(1);
        const auto* comment = formatAt(second, 0);
        QVERIFY(comment != nullptr);
        QVERIFY(comment->format.fontItalic()); // o comentário aberto na linha anterior continua
        const auto* keyword = formatAt(second, static_cast<int>(second.text().indexOf(QStringLiteral("SELECT"))));
        QVERIFY(keyword != nullptr);
        QCOMPARE(keyword->format.fontWeight(), static_cast<int>(QFont::Bold));
        const QTextBlock third = editor.document()->findBlockByNumber(2);
        const auto* string = formatAt(third, 0);
        QVERIFY(string != nullptr); // a string aberta na linha anterior continua
        QVERIFY(formatAt(third, static_cast<int>(third.text().indexOf(QStringLiteral("FROM")))) != nullptr);
        QVERIFY(formatAt(third, static_cast<int>(third.text().indexOf(QStringLiteral(" t")) + 1)) == nullptr);
    }

    void togglesLineComments() {
        SqlEditor editor;
        prepare(editor);
        editor.setPlainText(QStringLiteral("SELECT 1\n  FROM t"));
        editor.selectAll();
        editor.toggleComment();
        QCOMPARE(editor.toPlainText(), QStringLiteral("-- SELECT 1\n  -- FROM t"));
        editor.selectAll();
        editor.toggleComment();
        QCOMPARE(editor.toPlainText(), QStringLiteral("SELECT 1\n  FROM t"));
    }

    void movesLinesUpAndDown() {
        SqlEditor editor;
        prepare(editor);
        editor.setPlainText(QStringLiteral("a\nb\nc"));
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(2)));
        editor.moveLines(-1);
        QCOMPARE(editor.toPlainText(), QStringLiteral("a\nc\nb"));
        QCOMPARE(editor.textCursor().blockNumber(), 1);
        QTest::keyClick(&editor, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(editor.toPlainText(), QStringLiteral("a\nb\nc"));
        editor.moveLines(1); // já é a última linha
        QCOMPARE(editor.toPlainText(), QStringLiteral("a\nb\nc"));
    }

    void foldsIndentedBlocks() {
        SqlEditor editor;
        prepare(editor);
        editor.setPlainText(QStringLiteral("SELECT\n    a,\n    b\n\nFROM t"));
        const QTextBlock header = editor.document()->firstBlock();
        QVERIFY(editor.isFoldable(header));
        QVERIFY(!editor.isFoldable(header.next()));
        editor.toggleFold(header);
        QVERIFY(SqlEditor::isFolded(header));
        QVERIFY(!editor.document()->findBlockByNumber(2).isVisible());
        QVERIFY(editor.document()->findBlockByNumber(3).isVisible()); // a linha em branco do fim fica
        editor.toggleFold(header);
        QVERIFY(!SqlEditor::isFolded(header));
        QVERIFY(editor.document()->findBlockByNumber(2).isVisible());
    }

    void findAndReplaceRememberTheHistory() {
        mydb::infra::settings::MemorySettingsStore settings;
        SqlEditor editor;
        prepare(editor);
        editor.setPlainText(QStringLiteral("SELECT foo FROM foo"));
        {
            mydb::ui::FindReplaceDialog dialog([&editor] { return &editor; }, settings);
            dialog.setFindText(QStringLiteral("foo"));
            QVERIFY(dialog.findNext());
            QCOMPARE(editor.selectedSql(), QStringLiteral("foo"));
            dialog.setReplaceText(QStringLiteral("bar"));
            QCOMPARE(dialog.replaceAll(), 2);
            QCOMPARE(editor.toPlainText(), QStringLiteral("SELECT bar FROM bar"));
            dialog.setFindText(QStringLiteral("nada"));
            QVERIFY(!dialog.findNext());
        }
        const auto preferences = mydb::ui::EditorPreferences::load(settings);
        QCOMPARE(preferences.findHistory, QStringList({QStringLiteral("nada"), QStringLiteral("foo")}));
        QCOMPARE(preferences.replaceHistory, QStringList({QStringLiteral("bar")}));
        mydb::ui::FindReplaceDialog reopened([&editor] { return &editor; }, settings);
        auto* find = reopened.findChild<QComboBox*>(QStringLiteral("findText"));
        QVERIFY(find != nullptr);
        QCOMPARE(find->itemText(1), QStringLiteral("foo"));
    }

    void completionComesFromTheIndex() {
        SqlEditor editor;
        prepare(editor);
        std::vector<ObjectRef> missing;
        editor.setMissingTablesHandler([&missing](std::vector<ObjectRef> tables) { missing = std::move(tables); });
        editor.setCompletionIndex(makeIndex());
        editor.setPlainText(QStringLiteral("SELECT * FROM orders o JOIN u"));
        moveToEnd(editor);
        QTest::keyClick(&editor, Qt::Key_S);
        QTRY_VERIFY(editor.completer()->popup()->isVisible());
        QVERIFY(completionTexts(editor).contains(QStringLiteral("users")));
        QVERIFY(std::ranges::any_of(missing, [](const ObjectRef& ref) { return ref.name == "orders"; }));
        QTest::keyClick(editor.completer()->popup(), Qt::Key_Return);
        QCOMPARE(editor.toPlainText(), QStringLiteral("SELECT * FROM orders o JOIN users"));
        QVERIFY(!editor.completer()->popup()->isVisible());
    }

    void completionAnswersWithin10msOnALargeFile() {
        SqlEditor editor;
        prepare(editor);
        constexpr int kTables = 10'000;
        const auto index = makeIndex(kTables);
        editor.setCompletionIndex(index);
        QString text;
        for (int line = 0; line < kLargeFileLines; ++line) {
            text += QStringLiteral("SELECT id, name FROM users WHERE id = %1;\n").arg(line);
        }
        editor.setPlainText(text);
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(kLargeFileLines / 2)));
        editor.textCursor().insertText(QStringLiteral("SELECT * FROM table_1"));
        const SqlEditor::CompletionWindow window = editor.completionWindow();
        QVERIFY(window.text.size() < 2048); // só a sentença do cursor, não as 50 mil linhas
        std::vector<qint64> times;
        for (int run = 0; run < 15; ++run) {
            QElapsedTimer timer;
            timer.start();
            const auto items = index->complete(window.text, window.cursor);
            times.push_back(timer.nsecsElapsed());
            QVERIFY(!items.empty());
        }
        std::ranges::sort(times);
        const double medianMs = static_cast<double>(times[times.size() / 2]) / 1e6;
        qInfo("autocomplete com %d tabelas: mediana %.3f ms", kTables, medianMs);
        QVERIFY2(medianMs <= 10.0, qPrintable(QStringLiteral("%1 ms").arg(medianMs)));
    }

    void typingInA50kLineFileDoesNotStall() {
        SqlEditor editor;
        prepare(editor);
        editor.setCompletionIndex(makeIndex(1000));
        QString text;
        for (int line = 0; line < kLargeFileLines; ++line) {
            text += QStringLiteral("SELECT id, name FROM users WHERE id = %1; -- linha\n").arg(line);
        }
        editor.setPlainText(text);
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(kLargeFileLines / 2)));
        std::vector<qint64> times;
        for (const QChar c : QStringLiteral("SELECT * FROM users WHERE name = 'x';")) {
            QElapsedTimer timer;
            timer.start();
            QTest::keyClick(&editor, c.toLatin1());
            times.push_back(timer.nsecsElapsed());
        }
        QVERIFY(
            editor.document()->findBlockByNumber(kLargeFileLines / 2).text().startsWith(QStringLiteral("SELECT *")));
        std::ranges::sort(times);
        const double medianMs = static_cast<double>(times[times.size() / 2]) / 1e6;
        const double worstMs = static_cast<double>(times.back()) / 1e6;
        qInfo("tecla num arquivo de %d linhas: mediana %.3f ms, pior %.3f ms", kLargeFileLines, medianMs, worstMs);
        QVERIFY2(medianMs <= 16.0, qPrintable(QStringLiteral("%1 ms").arg(medianMs)));
        // Uma aspa abre uma string que vai até o fim do arquivo: o realce não pode refazer as 25 mil linhas.
        QVERIFY2(worstMs <= 100.0, qPrintable(QStringLiteral("%1 ms").arg(worstMs)));
    }
};

QTEST_MAIN(SqlEditorTest)
#include "SqlEditorTest.moc"
