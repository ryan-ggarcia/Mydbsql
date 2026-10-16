#pragma once

#include "core/model/ObjectRef.hpp"

#include <QIcon>

#include <string_view>

namespace mydb::ui {

/// Ícone próprio de resources/icons (SVG monocromático num .qrc comprimido), desenhado no tamanho exato pedido,
/// com cache, na cor do texto da paleta atual: acompanha o tema claro ou escuro (CLAUDE.md §2.2, regra 9;
/// §2.6, regra 7). Um nome sem arquivo devolve um ícone vazio.
[[nodiscard]] QIcon appIcon(std::string_view name);

[[nodiscard]] QIcon objectIcon(core::model::ObjectType type);

} // namespace mydb::ui
