#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QStringList>
#include <QVariant>

#include <vector>

namespace mydb::ui {

/// Tabela simples, em memória, para listas pequenas (variáveis, processos, bancos da aba Host). Cada célula tem
/// o texto exibido e o valor usado para ordenar (kSortRole), para que "1,2 MB" ordene como número.
class RowsModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    static constexpr int kSortRole = Qt::UserRole + 1;

    struct Cell {
        QVariant display;
        QVariant sort; ///< inválido: ordena pelo display
    };
    using Row = std::vector<Cell>;

    explicit RowsModel(QStringList headers, QObject* parent = nullptr);

    void setRows(std::vector<Row> rows);
    [[nodiscard]] const Row& rowAt(int row) const { return m_rows.at(static_cast<std::size_t>(row)); }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

private:
    QStringList m_headers;
    std::vector<Row> m_rows;
};

/// Célula com o mesmo valor para exibir e ordenar.
[[nodiscard]] inline RowsModel::Cell cell(QVariant value) {
    return RowsModel::Cell{.display = std::move(value), .sort = {}};
}

} // namespace mydb::ui
