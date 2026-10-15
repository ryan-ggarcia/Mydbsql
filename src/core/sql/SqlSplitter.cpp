#include "core/sql/SqlSplitter.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace mydb::core::sql {

namespace {

constexpr std::string_view kDelimiterKeyword = "DELIMITER";
constexpr std::size_t kMaxLeadWords = 12;

bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

char toUpperAscii(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix,
                              [](char a, char b) { return toUpperAscii(a) == toUpperAscii(b); });
}

template <std::size_t N>
bool isOneOf(std::string_view word, const std::array<std::string_view, N>& words) noexcept {
    return std::ranges::find(words, word) != words.end();
}

constexpr std::array<std::string_view, 5> kRoutineObjects{"PROCEDURE", "FUNCTION", "TRIGGER", "EVENT", "PACKAGE"};
constexpr std::array<std::string_view, 11> kPlainObjects{"TABLE", "VIEW",     "INDEX", "DATABASE",   "SCHEMA", "USER",
                                                         "ROLE",  "SEQUENCE", "TYPE",  "TABLESPACE", "SERVER"};
// "END IF", "END LOOP"…: fecham blocos que não são contados (só BEGIN e CASE abrem nível).
constexpr std::array<std::string_view, 5> kUncountedBlockEnds{"IF", "LOOP", "WHILE", "REPEAT", "FOR"};

bool isInsignificant(SqlTokenKind kind) noexcept {
    return kind == SqlTokenKind::Whitespace || kind == SqlTokenKind::LineComment || kind == SqlTokenKind::BlockComment;
}

} // namespace

const SqlStatement* statementAt(std::span<const SqlStatement> statements, std::uint64_t offset) noexcept {
    const auto after = std::ranges::upper_bound(statements, offset, {}, &SqlStatement::offset);
    return after == statements.begin() ? nullptr : &*std::prev(after);
}

SqlSplitter::SqlSplitter(SqlSyntax syntax) : m_tokenizer(syntax) {}

void SqlSplitter::feed(std::string_view chunk, const Sink& sink) {
    m_buffer.append(chunk);
    // Um token enorme (ex.: um BLOB literal de 100 MB) chega em muitos blocos. Reanalisá-lo a cada bloco
    // seria quadrático; só tentamos de novo quando os dados pendentes dobrarem.
    if (m_buffer.size() - m_scan >= m_retryWhenPending) {
        m_retryWhenPending = 0;
        process(false, sink);
    }
    compact();
}

void SqlSplitter::finish(const Sink& sink) {
    m_retryWhenPending = 0;
    process(true, sink);
    emit(m_buffer.size(), {}, sink);

    m_buffer.clear();
    m_bufferOffset = 0;
    m_scan = 0;
    m_statementStart = 0;
    m_trackPos = 0;
    m_trackLine = 1;
    m_trackColumn = 1;
    m_delimiter = ";";
    resetStatement();
}

std::vector<SqlStatement> SqlSplitter::split(std::string_view text, const SqlSyntax& syntax) {
    std::vector<SqlStatement> statements;
    SqlSplitter splitter(syntax);
    const Sink sink = [&statements](SqlStatement&& statement) {
        statements.push_back(std::move(statement));
    };
    splitter.feed(text, sink);
    splitter.finish(sink);
    return statements;
}

void SqlSplitter::process(bool finalInput, const Sink& sink) {
    const std::string_view text(m_buffer);

    while (m_scan < text.size()) {
        const bool customDelimiter = m_delimiter != ";";
        if (!m_hasCode && m_tokenizer.syntax().delimiterCommand) {
            bool needMore = false;
            if (tryDelimiterCommand(finalInput, needMore)) {
                continue;
            }
            if (needMore) {
                m_retryWhenPending = 1;
                return;
            }
        }

        const std::string_view rest = text.substr(m_scan);
        if (customDelimiter || effectiveDepth() <= 0) {
            if (rest.starts_with(m_delimiter)) {
                emit(m_scan, m_delimiter, sink);
                m_scan += m_delimiter.size();
                m_statementStart = m_scan;
                resetStatement();
                continue;
            }
            if (!finalInput && rest.size() < m_delimiter.size() && std::string_view(m_delimiter).starts_with(rest)) {
                m_retryWhenPending = 1;
                return;
            }
        }

        SqlToken token;
        const SqlTokenizer::Status status = m_tokenizer.next(text, m_scan, finalInput, token);
        if (status == SqlTokenizer::Status::NeedMore) {
            m_retryWhenPending = 2 * (text.size() - m_scan);
            return;
        }
        if (status == SqlTokenizer::Status::End) {
            return;
        }
        if (customDelimiter && !isInsignificant(token.kind) && token.kind != SqlTokenKind::String &&
            token.kind != SqlTokenKind::QuotedIdentifier) {
            // "END$$": a palavra não pode engolir o delimitador personalizado.
            const std::size_t inside = text.substr(token.offset, token.length).find(m_delimiter);
            if (inside != std::string_view::npos && inside > 0) {
                token.length = inside;
            }
        }
        if (!isInsignificant(token.kind)) {
            m_hasCode = true;
            onCodeToken(text.substr(token.offset, token.length), token.kind);
        }
        m_scan += token.length;
    }
}

bool SqlSplitter::tryDelimiterCommand(bool finalInput, bool& needMore) {
    const std::string_view rest = std::string_view(m_buffer).substr(m_scan);
    if (rest.empty() || toUpperAscii(rest.front()) != 'D') {
        return false;
    }
    if (rest.size() <= kDelimiterKeyword.size()) {
        needMore = !finalInput && startsWithNoCase(kDelimiterKeyword, rest);
        return false;
    }
    const char afterKeyword = rest[kDelimiterKeyword.size()];
    if (!startsWithNoCase(rest, kDelimiterKeyword) || (afterKeyword != ' ' && afterKeyword != '\t')) {
        return false;
    }
    const std::size_t newline = rest.find('\n');
    if (newline == std::string_view::npos && !finalInput) {
        needMore = true;
        return false;
    }
    const std::size_t lineEnd = newline == std::string_view::npos ? rest.size() : newline;
    std::string_view argument = rest.substr(kDelimiterKeyword.size(), lineEnd - kDelimiterKeyword.size());
    while (!argument.empty() && isSpace(argument.front())) {
        argument.remove_prefix(1);
    }
    const auto argumentLength =
        static_cast<std::size_t>(std::ranges::distance(argument.begin(), std::ranges::find_if(argument, isSpace)));
    argument = argument.substr(0, argumentLength);
    if (argument.empty()) {
        return false;
    }
    m_delimiter = std::string(argument);
    m_scan += newline == std::string_view::npos ? rest.size() : newline + 1;
    m_statementStart = m_scan;
    return true;
}

void SqlSplitter::onCodeToken(std::string_view text, SqlTokenKind kind) {
    if (!m_tokenizer.syntax().compoundBlocks) {
        return;
    }
    if (kind == SqlTokenKind::Word) {
        onWord(text);
        return;
    }
    if (m_pendingEnd) {
        m_pendingEnd = false;
        --m_depth;
    }
}

void SqlSplitter::onWord(std::string_view word) {
    std::string upper(word);
    std::ranges::transform(upper, upper.begin(), toUpperAscii);

    if (m_routine == Routine::Unknown) {
        m_leadWords.push_back(upper);
        const std::string& first = m_leadWords.front();
        if (first == "CREATE") {
            if (isOneOf(upper, kRoutineObjects)) {
                m_routine = Routine::Yes;
            } else if (isOneOf(upper, kPlainObjects) || m_leadWords.size() >= kMaxLeadWords) {
                m_routine = Routine::No;
            }
        } else if (first == "BEGIN") {
            // "BEGIN NOT ATOMIC" (bloco anônimo do MariaDB) é rotina; "BEGIN" / "BEGIN WORK" é transação.
            if (m_leadWords.size() >= 2) {
                m_routine = upper == "NOT" ? Routine::Yes : Routine::No;
                m_depth = m_routine == Routine::Yes ? 1 : 0;
            }
        } else {
            m_routine = Routine::No;
        }
        return;
    }
    if (m_routine != Routine::Yes) {
        return;
    }
    if (m_pendingEnd) {
        m_pendingEnd = false;
        if (isOneOf(std::string_view(upper), kUncountedBlockEnds)) {
            return;
        }
        --m_depth;
        if (upper == "CASE") {
            return; // END CASE
        }
    }
    if (upper == "BEGIN" || upper == "CASE") {
        ++m_depth;
    } else if (upper == "END") {
        m_pendingEnd = true;
    }
}

void SqlSplitter::emit(std::size_t end, std::string_view usedDelimiter, const Sink& sink) {
    if (!m_hasCode) {
        return;
    }
    std::size_t start = m_statementStart;
    while (start < end && isSpace(m_buffer[start])) {
        ++start;
    }
    std::size_t stop = end;
    while (stop > start && isSpace(m_buffer[stop - 1])) {
        --stop;
    }
    advanceLineTracking(start);
    SqlStatement statement{.text = m_buffer.substr(start, stop - start),
                           .offset = m_bufferOffset + start,
                           .line = m_trackLine,
                           .column = m_trackColumn,
                           .delimiter = std::string(usedDelimiter)};
    sink(std::move(statement));
}

void SqlSplitter::resetStatement() noexcept {
    m_hasCode = false;
    m_routine = Routine::Unknown;
    m_leadWords.clear();
    m_depth = 0;
    m_pendingEnd = false;
}

void SqlSplitter::advanceLineTracking(std::size_t to) noexcept {
    for (; m_trackPos < to; ++m_trackPos) {
        if (m_buffer[m_trackPos] == '\n') {
            ++m_trackLine;
            m_trackColumn = 1;
        } else {
            ++m_trackColumn;
        }
    }
}

void SqlSplitter::compact() {
    if (m_statementStart == 0) {
        return;
    }
    advanceLineTracking(m_statementStart);
    m_buffer.erase(0, m_statementStart);
    m_bufferOffset += m_statementStart;
    m_scan -= m_statementStart;
    m_trackPos -= m_statementStart;
    m_statementStart = 0;
}

} // namespace mydb::core::sql
