#pragma once

#include <chrono>
#include <string>

namespace mydb::core::model {

/// Diferença entre a hora local e o UTC no instante `at` (ex.: -180 min em Brasília). Usada para a opção
/// "usar o fuso horário local" da sessão. Thread-safe.
[[nodiscard]] std::chrono::minutes localUtcOffset(std::chrono::system_clock::time_point at);

/// Formata como o SQL espera: "+05:30", "-03:00".
[[nodiscard]] std::string formatUtcOffset(std::chrono::minutes offset);

} // namespace mydb::core::model
