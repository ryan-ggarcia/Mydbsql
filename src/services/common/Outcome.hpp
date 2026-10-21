#pragma once

#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace mydb::services {

/// Resultado de um trabalho assíncrono: o valor ou a exceção que ele lançou. Para trabalhos sem valor,
/// use Outcome<std::monostate>.
template <typename T>
class Outcome {
public:
    [[nodiscard]] static Outcome success(T value) { return Outcome(std::move(value), nullptr); }
    [[nodiscard]] static Outcome failure(const std::exception_ptr& error) { return Outcome(std::nullopt, error); }

    [[nodiscard]] bool ok() const noexcept { return m_value.has_value(); }
    /// Lança std::logic_error se o trabalho falhou.
    [[nodiscard]] const T& value() const& {
        if (!m_value) {
            throw std::logic_error("Outcome sem valor");
        }
        return *m_value;
    }
    [[nodiscard]] T&& value() && {
        if (!m_value) {
            throw std::logic_error("Outcome sem valor");
        }
        return std::move(*m_value);
    }
    [[nodiscard]] std::exception_ptr error() const noexcept { return m_error; }

private:
    Outcome(std::optional<T> value, std::exception_ptr error) : m_value(std::move(value)), m_error(std::move(error)) {}

    std::optional<T> m_value;
    std::exception_ptr m_error;
};

template <typename T>
using Completion = std::function<void(Outcome<T>)>;

using Done = std::monostate;

} // namespace mydb::services
