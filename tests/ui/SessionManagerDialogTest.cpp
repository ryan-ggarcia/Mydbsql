// Gerenciador de sessões com Qt Test na plataforma offscreen: cria, salva, reabre e testa a conexão
// de uma sessão, sem banco de verdade (driver falso).

#include "ui/dialogs/SessionManagerDialog.hpp"

#include "infra/credentials/MemoryCredentialStore.hpp"
#include "infra/settings/MemorySettingsStore.hpp"
#include "services/session/SessionConnector.hpp"
#include "services/session/SessionRepository.hpp"
#include "support/FakeDriver.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimer>

namespace {

using mydb::services::session::Session;
using mydb::services::session::SessionConnector;
using mydb::services::session::SessionRepository;
using mydb::ui::SessionManagerDialog;

template <typename Widget>
Widget* find(SessionManagerDialog& dialog, const char* name) {
    auto* widget = dialog.findChild<Widget*>(QString::fromLatin1(name));
    if (widget == nullptr) {
        qFatal("widget não encontrado: %s", name);
    }
    return widget;
}

void type(SessionManagerDialog& dialog, const char* name, const QString& text) {
    // keyClicks só aceita ASCII; setText cobre os nomes acentuados.
    find<QLineEdit>(dialog, name)->setText(text);
}

} // namespace

class SessionManagerDialogTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void createsSavesAndReopensASession() {
        mydb::infra::settings::MemorySettingsStore settings;
        mydb::infra::credentials::MemoryCredentialStore credentials;
        std::string seenPassword;
        mydb::tests::FakeCatalog catalog(seenPassword);
        SessionConnector connector(catalog, credentials);

        std::string id;
        {
            SessionRepository repository(settings, credentials);
            repository.load();
            SessionManagerDialog dialog(repository, connector, catalog.availableDrivers(), true);
            dialog.show();
            QTest::mouseClick(find<QPushButton>(dialog, "newSessionButton"), Qt::LeftButton);
            QCOMPARE(repository.sessions().size(), std::size_t{1});
            id = dialog.selectedSessionId();
            QCOMPARE(QString::fromStdString(id), QString::fromStdString(repository.sessions().front().id));

            type(dialog, "nameEdit", QStringLiteral("Local ação"));
            type(dialog, "hostEdit", QStringLiteral("127.0.0.1"));
            find<QSpinBox>(dialog, "portSpin")->setValue(3307);
            type(dialog, "userEdit", QStringLiteral("root"));
            type(dialog, "passwordEdit", QStringLiteral("s3gredo"));
            find<QCheckBox>(dialog, "savePasswordCheck")->setChecked(true);
            QTest::mouseClick(find<QPushButton>(dialog, "saveButton"), Qt::LeftButton);

            const Session* saved = repository.find(id);
            QVERIFY(saved != nullptr);
            QCOMPARE(QString::fromStdString(saved->name), QStringLiteral("Local ação"));
            QCOMPARE(saved->params.port, std::uint16_t{3307});
            QCOMPARE(QString::fromStdString(saved->params.user), QStringLiteral("root"));
            QCOMPARE(QString::fromStdString(credentials.read(id + "/password").value_or("")),
                     QStringLiteral("s3gredo"));
            QVERIFY(find<QLineEdit>(dialog, "passwordEdit")->text().isEmpty()); // não fica na tela
        }

        // "Reabrir o app": repositório novo lendo o mesmo documento.
        SessionRepository reopened(settings, credentials);
        reopened.load();
        SessionManagerDialog dialog(reopened, connector, catalog.availableDrivers(), true);
        dialog.show();
        QCOMPARE(QString::fromStdString(dialog.selectedSessionId()), QString::fromStdString(id));
        QCOMPARE(find<QLineEdit>(dialog, "nameEdit")->text(), QStringLiteral("Local ação"));
        QCOMPARE(find<QLineEdit>(dialog, "hostEdit")->text(), QStringLiteral("127.0.0.1"));

        // Testar conexão usa a senha guardada, sem digitar de novo.
        QSignalSpy finished(&dialog, &SessionManagerDialog::testFinished);
        QTest::mouseClick(find<QPushButton>(dialog, "testButton"), Qt::LeftButton);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).toBool(), true);
        QCOMPARE(QString::fromStdString(seenPassword), QStringLiteral("s3gredo"));
        QCOMPARE(reopened.find(id)->statistics.connections, std::uint64_t{1});
    }

    void switchingSessionsSavesThePreviousOne() {
        mydb::infra::settings::MemorySettingsStore settings;
        mydb::infra::credentials::MemoryCredentialStore credentials;
        std::string seenPassword;
        mydb::tests::FakeCatalog catalog(seenPassword);
        SessionConnector connector(catalog, credentials);
        SessionRepository repository(settings, credentials);
        const std::string first = repository.create("Primeira").id;
        const std::string second = repository.create("Segunda").id;

        SessionManagerDialog dialog(repository, connector, catalog.availableDrivers(), true);
        dialog.show();
        dialog.selectSession(first);
        type(dialog, "hostEdit", QStringLiteral("servidor-1"));
        dialog.selectSession(second);
        QCOMPARE(QString::fromStdString(repository.find(first)->params.host), QStringLiteral("servidor-1"));
        QCOMPARE(find<QLineEdit>(dialog, "hostEdit")->text(), QString());
    }

    void failedConnectionIsReported() {
        mydb::infra::settings::MemorySettingsStore settings;
        mydb::infra::credentials::MemoryCredentialStore credentials;
        std::string seenPassword;
        mydb::tests::FakeCatalog catalog(seenPassword);
        SessionConnector connector(catalog, credentials);
        SessionRepository repository(settings, credentials);
        Session& session = repository.create("Errada");
        session.params.driverId = "fake";
        credentials.write(session.id + "/password", "errada");

        SessionManagerDialog dialog(repository, connector, catalog.availableDrivers(), true);
        dialog.show();
        QSignalSpy finished(&dialog, &SessionManagerDialog::testFinished);
        // A caixa de erro é modal; ela é fechada assim que aparecer.
        QMetaObject::Connection closer = connect(&dialog, &SessionManagerDialog::testFinished, &dialog, [] {
            QTimer::singleShot(0, [] {
                if (QWidget* box = QApplication::activeModalWidget()) {
                    box->close();
                }
            });
        });
        QTest::mouseClick(find<QPushButton>(dialog, "testButton"), Qt::LeftButton);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).toBool(), false);
        QVERIFY(finished.first().at(1).toString().contains(QStringLiteral("Access denied")));
        disconnect(closer);
    }
};

QTEST_MAIN(SessionManagerDialogTest)
#include "SessionManagerDialogTest.moc"
