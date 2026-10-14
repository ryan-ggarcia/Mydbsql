#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mydb::core::model {

enum class DataTypeCategory : std::uint8_t {
    Integer,
    Real,
    Text,
    Binary,
    Temporal,
    Spatial,
    Other,
};

[[nodiscard]] std::string_view toString(DataTypeCategory category) noexcept;

/// Um tipo do catálogo de um dialeto (ISqlDialect::dataTypes()). Descreve o que o designer de tabela
/// deve oferecer: se o tipo aceita tamanho, unsigned etc.
struct DataType {
    std::string name;                                 ///< em maiúsculas, como o dialeto o escreve
    DataTypeCategory category{DataTypeCategory::Other};
    bool lengthAllowed{false};                        ///< aceita "(n)" ou "(n,m)"
    bool lengthRequired{false};                       ///< ex.: VARCHAR no MySQL
    bool unsignedAllowed{false};
    bool charsetAllowed{false};
    std::string defaultLength;                        ///< sugestão para o designer; vazio se não houver
    std::string description;

    friend bool operator==(const DataType&, const DataType&) = default;
};

} // namespace mydb::core::model
