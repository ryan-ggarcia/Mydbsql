#include "services/query/CompletionIndex.hpp"

#include "core/sql/SqlTokenizer.hpp"
#include "services/query/SqlText.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace mydb::services::query {

namespace {

using core::sql::SqlToken;
using core::sql::SqlTokenKind;

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

bool lessEntry(const std::string& aKey, CompletionKind aKind, const std::string& bKey, CompletionKind bKind) {
    return aKey != bKey ? aKey < bKey : aKind < bKind;
}

template <typename EntryT>
void sortEntries(std::vector<EntryT>& entries) {
    std::ranges::sort(entries, [](const EntryT& a, const EntryT& b) {
        return lessEntry(a.key, a.completion.kind, b.key, b.completion.kind);
    });
}

/// Os itens de `entries` (ordenado por key) cujo key começa com `prefix` (já em minúsculas).
template <typename EntryT, typename Out>
void collectPrefix(const std::vector<EntryT>& entries, std::string_view prefix, Out& out) {
    auto it = std::ranges::lower_bound(entries, prefix, {}, [](const EntryT& e) { return std::string_view(e.key); });
    for (; it != entries.end() && std::string_view(it->key).starts_with(prefix); ++it) {
        out.push_back(&it->completion);
    }
}

/// Palavras que encerram a lista de tabelas de FROM/JOIN/UPDATE/INTO (não são aliases).
constexpr auto kStopWords = std::to_array<std::string_view>(
    {"as",        "cross",     "except",    "fetch",   "for",       "force",  "from",          "full",
     "group",     "having",    "ignore",    "inner",   "intersect", "join",   "lateral",       "left",
     "limit",     "lock",      "minus",     "natural", "offset",    "on",     "order",         "outer",
     "partition", "procedure", "returning", "right",   "select",    "set",    "straight_join", "union",
     "use",       "using",     "value",     "values",  "where",     "window", "with",          "into"});

bool isStopWord(std::string_view word) {
    const std::string lower = lowerAscii(word);
    return std::ranges::any_of(kStopWords, [&lower](std::string_view stop) { return lower == stop; });
}

bool isIdentifier(const SqlToken& token) {
    return token.kind == SqlTokenKind::Word || token.kind == SqlTokenKind::QuotedIdentifier;
}

/// Um identificador que pode ser nome de tabela ou alias (não uma palavra como WHERE ou JOIN).
bool isTableName(std::string_view text, const SqlToken& token) {
    return isIdentifier(token) &&
           (token.kind != SqlTokenKind::Word || !isStopWord(text.substr(token.offset, token.length)));
}

/// Uma tabela citada em FROM/JOIN/UPDATE/INTO.
struct TableRef {
    std::string database;
    std::string name;
    std::string alias;
    std::size_t nameEnd{0}; ///< fim do nome no texto
};

/// Os tokens significativos (sem espaços nem comentários) da sentença que contém `cursor`, e o token que
/// termina no cursor ou o contém.
struct Statement {
    std::vector<SqlToken> tokens;
    std::optional<SqlToken> atCursor; ///< o token que contém o caractere antes do cursor
};

Statement statementAt(const core::sql::SqlTokenizer& tokenizer, std::string_view text, std::size_t cursor) {
    Statement statement;
    for (const SqlToken& token : tokenizer.tokenize(text)) {
        if (cursor > token.offset && cursor <= token.offset + token.length) {
            statement.atCursor = token;
        }
        if (token.kind == SqlTokenKind::Whitespace || token.kind == SqlTokenKind::LineComment ||
            token.kind == SqlTokenKind::BlockComment) {
            continue;
        }
        if (token.kind == SqlTokenKind::Operator && text[token.offset] == ';') {
            if (token.offset >= cursor) {
                break;
            }
            statement.tokens.clear();
            continue;
        }
        statement.tokens.push_back(token);
    }
    return statement;
}

bool isOperator(std::string_view text, const SqlToken& token, char c) {
    return token.kind == SqlTokenKind::Operator && text[token.offset] == c;
}

std::vector<TableRef> tableRefs(std::string_view text, const std::vector<SqlToken>& tokens) {
    std::vector<TableRef> refs;
    std::size_t i = 0;
    while (i < tokens.size()) {
        const SqlToken& token = tokens[i++];
        if (token.kind != SqlTokenKind::Word) {
            continue;
        }
        const std::string keyword = lowerAscii(text.substr(token.offset, token.length));
        if (keyword != "from" && keyword != "join" && keyword != "update" && keyword != "into") {
            continue;
        }
        const bool list = keyword == "from" || keyword == "update";
        while (i < tokens.size() && isTableName(text, tokens[i])) {
            TableRef ref;
            ref.name = identifierName(text, tokens[i]);
            ref.nameEnd = tokens[i].offset + tokens[i].length;
            ++i;
            if (i + 1 < tokens.size() && isOperator(text, tokens[i], '.') && isIdentifier(tokens[i + 1])) {
                ref.database = std::move(ref.name);
                ref.name = identifierName(text, tokens[i + 1]);
                ref.nameEnd = tokens[i + 1].offset + tokens[i + 1].length;
                i += 2;
            }
            if (i < tokens.size() && tokens[i].kind == SqlTokenKind::Word &&
                equalsIgnoreCase(text.substr(tokens[i].offset, tokens[i].length), "as")) {
                ++i;
            }
            if (i < tokens.size() && isTableName(text, tokens[i])) {
                ref.alias = identifierName(text, tokens[i]);
                ++i;
            }
            refs.push_back(std::move(ref));
            if (!list || i >= tokens.size() || !isOperator(text, tokens[i], ',')) {
                break;
            }
            ++i;
        }
    }
    return refs;
}

CompletionKind kindOf(core::model::ObjectType type) {
    switch (type) {
    case core::model::ObjectType::View:
        return CompletionKind::View;
    case core::model::ObjectType::Procedure:
    case core::model::ObjectType::Function:
        return CompletionKind::Routine;
    default:
        return CompletionKind::Table;
    }
}

} // namespace

CompletionIndex::CompletionIndex(core::sql::SqlSyntax syntax) : m_syntax(syntax) {}

namespace {

/// Troca as entradas de `kind` em `global` por `names`, mantendo a ordem.
template <typename EntryT>
void replaceGlobal(std::vector<EntryT>& global, CompletionKind kind, std::vector<EntryT> entries) {
    std::erase_if(global, [kind](const EntryT& e) { return e.completion.kind == kind; });
    for (EntryT& e : entries) {
        global.push_back(std::move(e));
    }
    sortEntries(global);
}

} // namespace

void CompletionIndex::setKeywords(std::vector<std::string> keywords) {
    std::vector<Entry> entries;
    entries.reserve(keywords.size());
    for (std::string& keyword : keywords) {
        entries.push_back(
            Entry{.key = lowerAscii(keyword),
                  .completion = {.text = std::move(keyword), .detail = {}, .kind = CompletionKind::Keyword}});
    }
    replaceGlobal(m_global, CompletionKind::Keyword, std::move(entries));
}

void CompletionIndex::setFunctions(std::vector<SqlFunction> functions) {
    std::vector<Entry> entries;
    entries.reserve(functions.size());
    for (SqlFunction& function : functions) {
        entries.push_back(Entry{.key = lowerAscii(function.name),
                                .completion = {.text = std::move(function.name),
                                               .detail = std::move(function.signature),
                                               .kind = CompletionKind::Function}});
    }
    replaceGlobal(m_global, CompletionKind::Function, std::move(entries));
}

void CompletionIndex::setDatabases(std::vector<std::string> databases, std::string currentDatabase) {
    std::vector<Entry> entries;
    entries.reserve(databases.size());
    for (std::string& database : databases) {
        entries.push_back(
            Entry{.key = lowerAscii(database),
                  .completion = {.text = std::move(database), .detail = {}, .kind = CompletionKind::Database}});
    }
    replaceGlobal(m_global, CompletionKind::Database, std::move(entries));
    m_currentDatabase = std::move(currentDatabase);
}

void CompletionIndex::setObjects(std::string_view database, const std::vector<core::model::DbObject>& objects) {
    std::vector<Entry> entries;
    entries.reserve(objects.size());
    for (const core::model::DbObject& object : objects) {
        if (object.ref.type == core::model::ObjectType::Trigger || object.ref.type == core::model::ObjectType::Event) {
            continue;
        }
        entries.push_back(Entry{
            .key = lowerAscii(object.ref.name),
            .completion = {.text = object.ref.name, .detail = object.ref.database, .kind = kindOf(object.ref.type)}});
    }
    sortEntries(entries);
    for (auto& [name, list] : m_objects) {
        if (name == database) {
            list = std::move(entries);
            return;
        }
    }
    m_objects.emplace_back(std::string(database), std::move(entries));
}

void CompletionIndex::setColumns(const core::model::TableDefinition& table) {
    Table entry{.database = table.ref.database, .name = table.ref.name, .columns = {}};
    entry.columns.reserve(table.columns.size());
    for (const core::model::Column& column : table.columns) {
        entry.columns.push_back(
            Entry{.key = lowerAscii(column.name),
                  .completion = {.text = column.name, .detail = column.fullType(), .kind = CompletionKind::Column}});
    }
    sortEntries(entry.columns);
    for (Table& existing : m_tables) {
        if (existing.database == entry.database && existing.name == entry.name) {
            existing = std::move(entry);
            return;
        }
    }
    m_tables.push_back(std::move(entry));
}

void CompletionIndex::setSnippets(std::vector<std::string> names) {
    std::vector<Entry> entries;
    entries.reserve(names.size());
    for (std::string& name : names) {
        entries.push_back(
            Entry{.key = lowerAscii(name),
                  .completion = {.text = std::move(name), .detail = {}, .kind = CompletionKind::Snippet}});
    }
    replaceGlobal(m_global, CompletionKind::Snippet, std::move(entries));
}

std::vector<Completion> CompletionIndex::complete(std::string_view text, std::size_t cursor, std::size_t limit) const {
    cursor = std::min(cursor, text.size());
    const core::sql::SqlTokenizer tokenizer(m_syntax);
    const Statement statement = statementAt(tokenizer, text, cursor);

    // O que está sendo digitado e o qualificador antes do ".".
    std::string typed;
    std::size_t typedStart = cursor;
    if (statement.atCursor) {
        const SqlToken& token = *statement.atCursor;
        switch (token.kind) {
        case SqlTokenKind::String:
        case SqlTokenKind::LineComment:
        case SqlTokenKind::BlockComment:
            return {};
        case SqlTokenKind::Word:
            typed = text.substr(token.offset, cursor - token.offset);
            typedStart = token.offset;
            break;
        case SqlTokenKind::QuotedIdentifier:
            typed = text.substr(token.offset + 1, cursor - token.offset - 1);
            if (token.complete && cursor == token.offset + token.length && !typed.empty()) {
                typed.pop_back();
            }
            typedStart = token.offset;
            break;
        default:
            break;
        }
    }
    const std::string prefix = lowerAscii(typed);

    // Qualificadores: "a." ou "a.b." imediatamente antes do que está sendo digitado.
    std::vector<std::string> qualifiers;
    std::size_t end = typedStart;
    for (;;) {
        const auto dot =
            std::ranges::find_if(statement.tokens, [&](const SqlToken& t) { return t.offset + t.length == end; });
        if (dot == statement.tokens.end() || !isOperator(text, *dot, '.') || dot == statement.tokens.begin()) {
            break;
        }
        const SqlToken& name = *std::prev(dot);
        if (!isIdentifier(name) || name.offset + name.length != dot->offset) {
            break;
        }
        qualifiers.insert(qualifiers.begin(), identifierName(text, name));
        end = name.offset;
        if (qualifiers.size() == 2) {
            break;
        }
    }

    const std::vector<TableRef> refs = tableRefs(text, statement.tokens);
    std::vector<const Completion*> found;
    std::vector<Completion> aliases; // não vivem no índice

    const auto columnsOf = [&](std::string_view database, std::string_view table) {
        const Table* match = nullptr;
        for (const Table& t : m_tables) {
            if (!equalsIgnoreCase(t.name, table)) {
                continue;
            }
            if (equalsIgnoreCase(t.database, database.empty() ? m_currentDatabase : database)) {
                match = &t;
                break;
            }
            if (database.empty() && match == nullptr) {
                match = &t;
            }
        }
        if (match != nullptr) {
            collectPrefix(match->columns, prefix, found);
        }
        return match != nullptr;
    };
    const auto objectsOf = [this](std::string_view database) -> const std::vector<Entry>* {
        for (const auto& [name, list] : m_objects) {
            if (equalsIgnoreCase(name, database)) {
                return &list;
            }
        }
        return nullptr;
    };

    if (qualifiers.size() == 2) {
        columnsOf(qualifiers[0], qualifiers[1]);
    } else if (qualifiers.size() == 1) {
        const std::string& qualifier = qualifiers[0];
        bool resolved = false;
        for (const TableRef& ref : refs) {
            if (!ref.alias.empty() && equalsIgnoreCase(ref.alias, qualifier)) {
                resolved = columnsOf(ref.database, ref.name);
                break;
            }
        }
        if (!resolved) {
            resolved = columnsOf({}, qualifier);
        }
        if (!resolved) {
            if (const std::vector<Entry>* objects = objectsOf(qualifier)) {
                collectPrefix(*objects, prefix, found);
            }
        }
    } else {
        collectPrefix(m_global, prefix, found);
        if (const std::vector<Entry>* objects = objectsOf(m_currentDatabase)) {
            collectPrefix(*objects, prefix, found);
        }
        for (const TableRef& ref : refs) {
            if (ref.nameEnd == cursor) {
                continue;
            }
            if (!ref.alias.empty() && lowerAscii(ref.alias).starts_with(prefix)) {
                aliases.push_back(Completion{.text = ref.alias, .detail = ref.name, .kind = CompletionKind::Alias});
            }
            columnsOf(ref.database, ref.name);
        }
    }

    for (const Completion& alias : aliases) {
        found.push_back(&alias);
    }
    // Os que começam exatamente como digitado (com as mesmas maiúsculas) primeiro; depois, ordem alfabética.
    struct Ranked {
        bool exact;
        std::string key;
        const Completion* completion;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(found.size());
    for (const Completion* completion : found) {
        ranked.push_back(Ranked{.exact = std::string_view(completion->text).starts_with(typed),
                                .key = lowerAscii(completion->text),
                                .completion = completion});
    }
    const auto order = [](const Ranked& a, const Ranked& b) {
        if (a.exact != b.exact) {
            return a.exact;
        }
        if (a.key != b.key) {
            return a.key < b.key;
        }
        if (a.completion->kind != b.completion->kind) {
            return a.completion->kind < b.completion->kind;
        }
        return a.completion->detail < b.completion->detail;
    };
    const std::size_t count = std::min(limit, ranked.size());
    std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(count), ranked.end(), order);

    std::vector<Completion> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0 && *ranked[i].completion == *ranked[i - 1].completion) {
            continue; // a mesma coluna vinda de duas citações da mesma tabela
        }
        result.push_back(*ranked[i].completion);
    }
    return result;
}

std::vector<core::model::ObjectRef> CompletionIndex::missingTables(std::string_view text, std::size_t cursor) const {
    cursor = std::min(cursor, text.size());
    const core::sql::SqlTokenizer tokenizer(m_syntax);
    const Statement statement = statementAt(tokenizer, text, cursor);
    std::vector<core::model::ObjectRef> missing;
    for (const TableRef& ref : tableRefs(text, statement.tokens)) {
        if (ref.nameEnd == cursor) {
            continue; // ainda sendo digitada
        }
        core::model::ObjectRef target{.database = ref.database.empty() ? m_currentDatabase : ref.database,
                                      .schema = {},
                                      .name = ref.name,
                                      .type = core::model::ObjectType::Table};
        if (target.database.empty()) {
            continue;
        }
        const bool known = std::ranges::any_of(m_tables, [&](const Table& t) {
            return equalsIgnoreCase(t.database, target.database) && equalsIgnoreCase(t.name, target.name);
        });
        if (known) {
            continue;
        }
        // Com a lista de objetos do banco carregada, só pede o que existe (com o nome e o tipo verdadeiros).
        bool listed = true;
        for (const auto& [database, objects] : m_objects) {
            if (!equalsIgnoreCase(database, target.database)) {
                continue;
            }
            const auto object = std::ranges::find_if(objects, [&](const Entry& e) {
                return e.key == lowerAscii(target.name) &&
                       (e.completion.kind == CompletionKind::Table || e.completion.kind == CompletionKind::View);
            });
            listed = object != objects.end();
            if (listed) {
                target.database = database;
                target.name = object->completion.text;
                target.type = object->completion.kind == CompletionKind::View ? core::model::ObjectType::View
                                                                              : core::model::ObjectType::Table;
            }
            break;
        }
        if (listed && std::ranges::find(missing, target) == missing.end()) {
            missing.push_back(std::move(target));
        }
    }
    return missing;
}

std::vector<SqlFunction> parseFunctionCatalog(std::string_view json) {
    try {
        const nlohmann::json root = nlohmann::json::parse(json);
        std::vector<SqlFunction> functions;
        for (const nlohmann::json& item : root.at("functions")) {
            functions.push_back(SqlFunction{.name = item.at("name").get<std::string>(),
                                            .signature = item.value("signature", std::string()),
                                            .description = item.value("description", std::string())});
        }
        return functions;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(std::string("Catálogo de funções inválido: ") + error.what());
    }
}

std::vector<std::string> sqlKeywords(std::string_view driverId) {
    // Lista própria: as palavras do SQL comum a todos os SGBDs e, por dialeto, as que o usuário mais digita.
    static constexpr auto kCommon = std::to_array<std::string_view>(
        {"ADD",       "ALL",        "ALTER",    "AND",      "ANY",          "AS",           "ASC",
         "BEGIN",     "BETWEEN",    "BY",       "CASCADE",  "CASE",         "CHECK",        "COLUMN",
         "COMMIT",    "CONSTRAINT", "CREATE",   "CROSS",    "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP",
         "DATABASE",  "DEFAULT",    "DELETE",   "DESC",     "DISTINCT",     "DROP",         "ELSE",
         "END",       "ESCAPE",     "EXCEPT",   "EXISTS",   "EXPLAIN",      "FALSE",        "FETCH",
         "FOREIGN",   "FROM",       "FULL",     "FUNCTION", "GRANT",        "GROUP",        "HAVING",
         "IN",        "INDEX",      "INNER",    "INSERT",   "INTERSECT",    "INTO",         "IS",
         "JOIN",      "KEY",        "LEFT",     "LIKE",     "LIMIT",        "NATURAL",      "NOT",
         "NULL",      "OFFSET",     "ON",       "OR",       "ORDER",        "OUTER",        "PRIMARY",
         "PROCEDURE", "REFERENCES", "RENAME",   "REVOKE",   "RIGHT",        "ROLLBACK",     "SAVEPOINT",
         "SCHEMA",    "SELECT",     "SET",      "SOME",     "TABLE",        "THEN",         "TRANSACTION",
         "TRIGGER",   "TRUE",       "TRUNCATE", "UNION",    "UNIQUE",       "UPDATE",       "USING",
         "VALUES",    "VIEW",       "WHEN",     "WHERE",    "WITH",         "RECURSIVE",    "OVER",
         "PARTITION", "WINDOW",     "ROWS",     "RANGE",    "TEMPORARY",    "IF",           "REPLACE",
         "NULLS",     "FIRST",      "LAST",     "ONLY",     "CALL"});
    static constexpr auto kMySql = std::to_array<std::string_view>({"AFTER",
                                                                    "ALGORITHM",
                                                                    "AUTO_INCREMENT",
                                                                    "BEFORE",
                                                                    "BINARY",
                                                                    "CHANGE",
                                                                    "CHARACTER",
                                                                    "CHARSET",
                                                                    "COLLATE",
                                                                    "COMMENT",
                                                                    "DATABASES",
                                                                    "DECLARE",
                                                                    "DEFINER",
                                                                    "DELAYED",
                                                                    "DELIMITER",
                                                                    "DETERMINISTIC",
                                                                    "DO",
                                                                    "DUPLICATE",
                                                                    "EACH",
                                                                    "ENGINE",
                                                                    "EVENT",
                                                                    "FIELDS",
                                                                    "FLUSH",
                                                                    "FORCE",
                                                                    "FULLTEXT",
                                                                    "HANDLER",
                                                                    "HIGH_PRIORITY",
                                                                    "IGNORE",
                                                                    "INFILE",
                                                                    "INOUT",
                                                                    "INTERVAL",
                                                                    "INVOKER",
                                                                    "ITERATE",
                                                                    "KILL",
                                                                    "LEAVE",
                                                                    "LINES",
                                                                    "LOAD",
                                                                    "LOCAL",
                                                                    "LOCK",
                                                                    "LOOP",
                                                                    "LOW_PRIORITY",
                                                                    "MODIFY",
                                                                    "OPTIMIZE",
                                                                    "OUT",
                                                                    "OUTFILE",
                                                                    "PROCESSLIST",
                                                                    "QUICK",
                                                                    "READS",
                                                                    "REGEXP",
                                                                    "REPEAT",
                                                                    "RETURN",
                                                                    "RETURNS",
                                                                    "SCHEDULE",
                                                                    "SECURITY",
                                                                    "SHOW",
                                                                    "SIGNAL",
                                                                    "SPATIAL",
                                                                    "SQL_CALC_FOUND_ROWS",
                                                                    "STATUS",
                                                                    "STRAIGHT_JOIN",
                                                                    "TABLES",
                                                                    "TERMINATED",
                                                                    "UNLOCK",
                                                                    "UNSIGNED",
                                                                    "USE",
                                                                    "VARIABLES",
                                                                    "WARNINGS",
                                                                    "WHILE",
                                                                    "ZEROFILL",
                                                                    "ANALYZE",
                                                                    "ENGINES",
                                                                    "XOR"});
    std::vector<std::string> keywords(kCommon.begin(), kCommon.end());
    if (driverId == "mysql") {
        keywords.insert(keywords.end(), kMySql.begin(), kMySql.end());
    }
    std::ranges::sort(keywords);
    const auto [first, last] = std::ranges::unique(keywords);
    keywords.erase(first, last);
    return keywords;
}

} // namespace mydb::services::query
