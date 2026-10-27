#pragma once

#include "services/query/QueryTypes.hpp"

#include <QAbstractTableModel>

namespace mydb::ui {

/// Grade somente leitura sobre um ResultTable da aba de query (Fase 5). A grade compacta em streaming
/// (ResultBuffer) é da Fase 6.
class ResultTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit ResultTableModel(services::query::ResultTable table, QObject* parent = nullptr);

    [[nodiscard]] const services::query::ResultTable& table() const noexcept { return m_table; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

private:
    services::query::ResultTable m_table;
};

} // namespace mydb::ui
