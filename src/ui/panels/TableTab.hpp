#pragma once

#include "ui/models/RowsModel.hpp"

#include <QSortFilterProxyModel>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QTableView;

namespace mydb::ui {

/// Uma tabela ordenável com filtro de texto opcional e uma linha de status: o bloco das abas Host e Database.
class TableTab final : public QWidget {
    Q_OBJECT

public:
    TableTab(const QStringList& headers, bool filterable, QWidget* parent = nullptr);

    [[nodiscard]] RowsModel& model() noexcept { return *m_model; }
    [[nodiscard]] QTableView& view() noexcept { return *m_view; }
    /// Linha de cima, ao lado do filtro, para botões e opções da aba.
    [[nodiscard]] QHBoxLayout& toolbar() noexcept { return *m_toolbar; }

    void setRows(std::vector<RowsModel::Row> rows);
    void setStatus(const QString& text);
    /// Linha do modelo correspondente a um índice da view (-1 se inválido).
    [[nodiscard]] int sourceRow(const QModelIndex& viewIndex) const;
    [[nodiscard]] int currentSourceRow() const;

private:
    RowsModel* m_model;
    QSortFilterProxyModel* m_proxy;
    QTableView* m_view;
    QLineEdit* m_filter{nullptr};
    QHBoxLayout* m_toolbar;
    QLabel* m_status;
};

} // namespace mydb::ui
