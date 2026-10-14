#include "core/errors/DbError.hpp"

#include <utility>

namespace mydb::core {

DbError::DbError(DbErrorInfo info) : std::runtime_error(info.message), m_info(std::move(info)) {}

DbError::DbError(std::string message)
    : DbError(DbErrorInfo{.message = std::move(message), .code = 0, .sqlState = {}, .hint = {}}) {}

CancelledError::CancelledError() : DbError("Operação cancelada") {}

} // namespace mydb::core
