#pragma once

#include "core/interfaces/IConnection.hpp"

#include <QMainWindow>

#include <memory>

class QLabel;

namespace mydb::ui {

/// Janela principal. Criada só depois de conectar (CLAUDE.md §1.4). Por enquanto mostra a sessão
/// conectada; árvore de objetos, abas e log entram na Fase 4, com menus vindos do CommandRegistry.
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;
    ~MainWindow() override;

    /// Passa a ser dona da conexão da sessão `sessionName`.
    void attachConnection(std::unique_ptr<core::IConnection> connection, const QString& sessionName);

private:
    QLabel* m_summary{nullptr};
    std::unique_ptr<core::IConnection> m_connection;
};

} // namespace mydb::ui
