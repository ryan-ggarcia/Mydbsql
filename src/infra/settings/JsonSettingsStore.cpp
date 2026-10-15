#include "infra/settings/JsonSettingsStore.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace mydb::infra::settings {

namespace {

/// Nomes de documento viram nomes de arquivo: só letras, dígitos, "-", "_" e ".", sem "..".
bool isValidDocumentName(std::string_view name) {
    if (name.empty() || name.find("..") != std::string_view::npos || name.front() == '.') {
        return false;
    }
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.';
    });
}

} // namespace

JsonSettingsStore::JsonSettingsStore(std::filesystem::path directory) : m_directory(std::move(directory)) {}

std::filesystem::path JsonSettingsStore::fileFor(std::string_view document) const {
    if (!isValidDocumentName(document)) {
        throw std::invalid_argument("Nome de documento de configuração inválido: " + std::string(document));
    }
    return m_directory / (std::string(document) + ".json");
}

std::optional<std::string> JsonSettingsStore::read(std::string_view document) {
    const std::filesystem::path file = fileFor(document);
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        std::error_code error;
        if (!std::filesystem::exists(file, error)) {
            return std::nullopt;
        }
        throw std::runtime_error("Não foi possível ler " + file.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void JsonSettingsStore::write(std::string_view document, std::string_view json) {
    const std::filesystem::path file = fileFor(document);
    std::filesystem::create_directories(m_directory);
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(json.data(), static_cast<std::streamsize>(json.size()));
        output.flush();
        if (!output) {
            throw std::runtime_error("Não foi possível gravar " + temporary.string());
        }
    }
    // Troca atômica: no POSIX, rename(2); no Windows, a STL usa MoveFileEx com MOVEFILE_REPLACE_EXISTING.
    std::filesystem::rename(temporary, file);
}

} // namespace mydb::infra::settings
