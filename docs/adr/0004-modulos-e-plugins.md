# ADR-0004: Módulos e plugins

- **Status:** aceito (a implementação começa na Fase 1)
- **Data:** 2026-10-14

## Contexto

No HeidiSQL, adicionar um formato de exportação ou um SGBD exige editar `case` centrais e formulários (§1.4). O objetivo 3 do Mydbsql pede que uma funcionalidade nova entre como um módulo novo, sem mexer no que já existe.

## Decisão

- Pontos de extensão são **registries** no `src/sdk/` (`DriverRegistry`, `ExporterRegistry`, `CommandRegistry`…, lista completa na CLAUDE.md §2.4).
- Um módulo implementa `sdk::IModule` e, em `registerInto(ModuleContext&)`, só registra fábricas. Nada pesado é criado no registro.
- Há duas formas de módulo: **estático**, registrado em `app/BuiltinModules.cpp` (padrão para funcionalidades pequenas), e **dinâmico** (`.so`/`.dll`/`.dylib` em `IPlatform::pluginDirectory()`), com um único símbolo exportado `mydb_module_entry` e checagem de `apiVersion()`.
- Um módulo depende só de `mydb_core` e `mydb_sdk`, nunca de outro módulo. O CMake confere isso.
- Símbolos ficam ocultos por padrão (`CMAKE_CXX_VISIBILITY_PRESET hidden`). Só a entrada do plugin é exportada.

## Consequências

- Plugins dinâmicos exigem o mesmo compilador e a mesma versão do Qt do executável. O carregador recusa versões incompatíveis com uma mensagem clara.
- Na Fase 0 já existem a base de carregamento (`DynamicLibrary`, RAII) e o diretório de plugins por SO. `IModule`, `ModuleContext` e os registries entram na Fase 1.
