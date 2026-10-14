#pragma once

#include <QMainWindow>

namespace mydb::ui {

/// Janela principal. Na Fase 0 é só a casca vazia usada para medir a inicialização (§2.1);
/// árvore de objetos, abas e log entram na Fase 4, com menus vindos do CommandRegistry.
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace mydb::ui
