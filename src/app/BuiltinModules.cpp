#include "app/BuiltinModules.hpp"

#include "app/ModuleHost.hpp"
#include "modules/query/QueryModule.hpp"
#include "modules/workbench/WorkbenchModule.hpp"

namespace mydb::app {

void registerBuiltinModules(ModuleHost& host) {
    host.addBuiltin(modules::workbench::module());
    host.addBuiltin(modules::query::module());
}

} // namespace mydb::app
