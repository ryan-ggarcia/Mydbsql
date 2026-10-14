#pragma once

#include <compare>
#include <cstdint>
#include <string>

namespace mydb::sdk {

struct Version {
    std::uint16_t major{0};
    std::uint16_t minor{0};

    [[nodiscard]] std::string toString() const { return std::to_string(major) + "." + std::to_string(minor); }

    friend constexpr auto operator<=>(const Version&, const Version&) = default;
};

/// Versão da API pública do sdk/. O major muda quando algo deixa de ser compatível (ex.: um método virtual
/// novo no ModuleContext); o minor, quando só há acréscimos que módulos antigos não usam.
inline constexpr Version kApiVersion{.major = 1, .minor = 0};

/// Um módulo compilado contra `module` roda no host `host` se o major for igual e o minor não for maior.
[[nodiscard]] constexpr bool isCompatible(Version host, Version module) noexcept {
    return host.major == module.major && module.minor <= host.minor;
}

} // namespace mydb::sdk
