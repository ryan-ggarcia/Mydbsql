#pragma once

#include <functional>

namespace mydb::services {

/// Leva um callback para a thread dona do estado de quem pediu o trabalho (na UI, a thread principal do Qt).
/// Os services não conhecem o Qt (CLAUDE.md §6): o app/ injeta a implementação.
using Dispatcher = std::function<void(std::function<void()>)>;

} // namespace mydb::services
