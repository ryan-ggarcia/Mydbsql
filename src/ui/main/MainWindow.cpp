#include "ui/main/MainWindow.hpp"

#include "ui/common/QtText.hpp"

#include <QLabel>
#include <QString>

namespace mydb::ui {

namespace {

constexpr int kInitialWidth = 1024;
constexpr int kInitialHeight = 680;

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_summary(new QLabel(this)) {
    setWindowTitle(QStringLiteral("Mydbsql"));
    resize(kInitialWidth, kInitialHeight);
    m_summary->setAlignment(Qt::AlignCenter);
    m_summary->setWordWrap(true);
    setCentralWidget(m_summary);
}

MainWindow::~MainWindow() = default;

void MainWindow::attachConnection(std::unique_ptr<core::IConnection> connection, const QString& sessionName) {
    m_connection = std::move(connection);
    const core::ServerInfo info = m_connection->serverInfo();
    const QString server = toQt(info.productName) + QLatin1Char(' ') + toQt(info.versionString);
    setWindowTitle(tr("%1 — %2 — Mydbsql").arg(sessionName, server));
    m_summary->setText(
        tr("Conectado a %1 (%2).\n\nA árvore de objetos, as abas e o log chegam na Fase 4.").arg(sessionName, server));
}

} // namespace mydb::ui
