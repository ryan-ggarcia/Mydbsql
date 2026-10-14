#include "ui/main/MainWindow.hpp"

#include <QString>

namespace mydb::ui {

namespace {

constexpr int kInitialWidth = 1024;
constexpr int kInitialHeight = 680;

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Mydbsql"));
    resize(kInitialWidth, kInitialHeight);
}

} // namespace mydb::ui
