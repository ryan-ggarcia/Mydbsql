// Árvore de objetos com 10 mil tabelas (CLAUDE.md §2.1 e §5, Fase 4): carregar ≤ 500 ms e filtrar ≤ 50 ms.
// Mede o lado da UI (modelo, filtro e QTreeView na plataforma offscreen); a consulta ao servidor é medida no
// teste de integração MySqlScaleTest.

#include "ui/models/ObjectTreeFilter.hpp"
#include "ui/models/ObjectTreeModel.hpp"

#include <QApplication>
#include <QTreeView>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using mydb::core::model::DbObject;
using mydb::core::model::ObjectRef;
using mydb::core::model::ObjectType;
using mydb::ui::ObjectTreeFilter;
using mydb::ui::ObjectTreeModel;

constexpr int kObjects = 10'000;

std::vector<DbObject> makeObjects() {
    std::vector<DbObject> objects;
    objects.reserve(kObjects);
    for (int i = 0; i < kObjects; ++i) {
        std::string name = std::to_string(i);
        name.insert(0, 5 - name.size(), '0');
        DbObject object;
        object.ref = ObjectRef{.database = "big", .schema = {}, .name = "table_" + name, .type = ObjectType::Table};
        object.rows = i;
        object.sizeBytes = std::int64_t{16384} * (i % 100 + 1);
        object.engine = "InnoDB";
        object.collation = "utf8mb4_general_ci";
        objects.push_back(std::move(object));
    }
    return objects;
}

/// Sessão "s" com o banco "big" ainda não carregado, numa QTreeView visível.
struct Tree {
    ObjectTreeModel model;
    ObjectTreeFilter filter;
    QTreeView view;

    Tree() {
        filter.setSourceModel(&model);
        view.setModel(&filter);
        view.setUniformRowHeights(true);
        constexpr int kWidth = 400;
        constexpr int kHeight = 900;
        view.resize(kWidth, kHeight);
        view.show();
        model.addSession("s", QStringLiteral("Sessão"), {});
        model.setDatabases("s", {"big"});
        view.expand(filter.mapFromSource(model.sessionIndex("s")));
        QCoreApplication::processEvents();
    }

    /// Expande o banco e entrega os objetos, como a MainWindow faz quando a consulta volta.
    void load(std::vector<DbObject> objects) {
        view.expand(filter.mapFromSource(model.databaseIndex("s", "big")));
        model.setObjects("s", "big", std::move(objects));
        view.doItemsLayout();
        QCoreApplication::processEvents();
    }
};

void BM_ObjectTreeLoad10k(benchmark::State& state) {
    const std::vector<DbObject> objects = makeObjects();
    for (auto _ : state) {
        state.PauseTiming();
        auto tree = std::make_unique<Tree>();
        std::vector<DbObject> copy = objects;
        state.ResumeTiming();

        tree->load(std::move(copy));
        benchmark::DoNotOptimize(
            tree->filter.rowCount(tree->filter.mapFromSource(tree->model.databaseIndex("s", "big"))));

        state.PauseTiming();
        tree.reset();
        state.ResumeTiming();
    }
}
BENCHMARK(BM_ObjectTreeLoad10k)->Unit(benchmark::kMillisecond);

void BM_ObjectTreeFilter10k(benchmark::State& state) {
    Tree tree;
    tree.load(makeObjects());
    bool filtered = false;
    for (auto _ : state) {
        // Alterna entre um filtro que casa com 1% das tabelas e nenhum filtro: cada iteração é uma digitação.
        filtered = !filtered;
        tree.filter.setObjectFilter(filtered ? QStringLiteral("table_001") : QString());
        tree.view.doItemsLayout();
        QCoreApplication::processEvents();
        benchmark::DoNotOptimize(tree.filter.rowCount(tree.filter.mapFromSource(tree.model.databaseIndex("s", "big"))));
    }
}
BENCHMARK(BM_ObjectTreeFilter10k)->Unit(benchmark::kMillisecond);

} // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    const QApplication app(argc, argv);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
