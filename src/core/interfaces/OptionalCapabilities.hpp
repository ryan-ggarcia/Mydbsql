#pragma once

// Interfaces opcionais (CLAUDE.md §4.3, ISP): o driver só implementa as que o SGBD suporta e as expõe
// por IConnection::capabilities(). As assinaturas são o mínimo para cada fase e crescem quando a
// funcionalidade correspondente for implementada (designers na Fase 7, importação na 9, admin na 10).

#include "core/model/ObjectRef.hpp"
#include "core/model/TableDefinition.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::core {

/// Geração de DDL (Fase 7).
class IDdlGenerator {
public:
    virtual ~IDdlGenerator() = default;
    [[nodiscard]] virtual std::string createTable(const model::TableDefinition& table) const = 0;
    /// ALTER mínimo para ir de `before` a `after`; vazio se forem iguais.
    [[nodiscard]] virtual std::string alterTable(const model::TableDefinition& before,
                                                 const model::TableDefinition& after) const = 0;
    [[nodiscard]] virtual std::string dropObject(const model::ObjectRef& object) const = 0;

protected:
    IDdlGenerator() = default;
    IDdlGenerator(const IDdlGenerator&) = default;
    IDdlGenerator(IDdlGenerator&&) = default;
    IDdlGenerator& operator=(const IDdlGenerator&) = default;
    IDdlGenerator& operator=(IDdlGenerator&&) = default;
};

struct ServerVariable {
    std::string name;
    std::string value;
    bool global{false};
};

struct ServerProcess {
    std::uint64_t id{0};
    std::string user;
    std::string host;
    std::string database;
    std::string command;
    std::int64_t seconds{0};
    std::string state;
    std::string info; ///< a consulta em execução, se houver
};

/// Aba Host: variáveis, status e processos (Fase 4).
class IServerAdmin {
public:
    virtual ~IServerAdmin() = default;
    [[nodiscard]] virtual std::vector<ServerVariable> variables(bool global) = 0;
    virtual void setVariable(const ServerVariable& variable) = 0;
    [[nodiscard]] virtual std::vector<ServerVariable> status() = 0;
    [[nodiscard]] virtual std::vector<ServerProcess> processes() = 0;
    virtual void killProcess(std::uint64_t id) = 0;

protected:
    IServerAdmin() = default;
    IServerAdmin(const IServerAdmin&) = default;
    IServerAdmin(IServerAdmin&&) = default;
    IServerAdmin& operator=(const IServerAdmin&) = default;
    IServerAdmin& operator=(IServerAdmin&&) = default;
};

struct UserAccount {
    std::string name;
    std::string host; ///< vazio nos SGBDs sem host na conta (PostgreSQL)

    friend bool operator==(const UserAccount&, const UserAccount&) = default;
};

/// Usuários, roles e privilégios (Fase 10).
class IUserAdmin {
public:
    virtual ~IUserAdmin() = default;
    [[nodiscard]] virtual std::vector<UserAccount> accounts() = 0;
    [[nodiscard]] virtual std::vector<std::string> grants(const UserAccount& account) = 0;
    virtual void changePassword(const UserAccount& account, std::string_view newPassword) = 0;

protected:
    IUserAdmin() = default;
    IUserAdmin(const IUserAdmin&) = default;
    IUserAdmin(IUserAdmin&&) = default;
    IUserAdmin& operator=(const IUserAdmin&) = default;
    IUserAdmin& operator=(IUserAdmin&&) = default;
};

enum class MaintenanceOperation : std::uint8_t {
    Check,
    Analyze,
    Checksum,
    Optimize,
    Repair,
};

struct MaintenanceMessage {
    model::ObjectRef table;
    std::string level; ///< como o SGBD informa: "status", "error", "warning", "note"
    std::string text;
};

/// CHECK/ANALYZE/OPTIMIZE/REPAIR (Fase 10).
class ITableMaintenance {
public:
    virtual ~ITableMaintenance() = default;
    [[nodiscard]] virtual bool supports(MaintenanceOperation operation) const = 0;
    [[nodiscard]] virtual std::vector<MaintenanceMessage> run(MaintenanceOperation operation,
                                                              std::span<const model::ObjectRef> tables) = 0;

protected:
    ITableMaintenance() = default;
    ITableMaintenance(const ITableMaintenance&) = default;
    ITableMaintenance(ITableMaintenance&&) = default;
    ITableMaintenance& operator=(const ITableMaintenance&) = default;
    ITableMaintenance& operator=(ITableMaintenance&&) = default;
};

struct CsvLoadRequest {
    model::ObjectRef target;
    std::vector<std::string> columns;
    std::filesystem::path file;
    std::string fieldSeparator{","};
    std::string enclosure{"\""};
    std::string lineTerminator{"\n"};
    std::uint32_t skipLines{0};
};

/// Carga em lote do lado do servidor: LOAD DATA, COPY FROM (Fase 9).
class IBulkLoader {
public:
    virtual ~IBulkLoader() = default;
    /// Devolve o número de linhas carregadas.
    virtual std::uint64_t loadCsv(const CsvLoadRequest& request) = 0;

protected:
    IBulkLoader() = default;
    IBulkLoader(const IBulkLoader&) = default;
    IBulkLoader(IBulkLoader&&) = default;
    IBulkLoader& operator=(const IBulkLoader&) = default;
    IBulkLoader& operator=(IBulkLoader&&) = default;
};

/// O que uma conexão oferece além do básico. Ponteiros nulos indicam capacidade ausente (LSP: nada de
/// lançar NotImplemented). Os objetos pertencem à conexão e valem enquanto ela existir.
struct Capabilities {
    IDdlGenerator* ddl{nullptr};
    IServerAdmin* serverAdmin{nullptr};
    IUserAdmin* userAdmin{nullptr};
    ITableMaintenance* maintenance{nullptr};
    IBulkLoader* bulkLoader{nullptr};
};

} // namespace mydb::core
