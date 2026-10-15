# ADR-0009: Cofre de senhas com as APIs nativas de cada SO, sem QtKeychain

- **Status:** aceito (decisão do usuário em 2026-10-15)
- **Data:** 2026-10-15

## Contexto

A CLAUDE.md §3 previa o QtKeychain para guardar senhas no cofre do SO. O pacote do QtKeychain no vcpkg depende do `qtbase` do próprio vcpkg, o que conflita com o Qt dos binários oficiais (ADR-0006) e exigiria compilar o Qt inteiro (horas e 10–15 GB). Além disso, o QtKeychain usa Qt, e o `ICredentialStore` é uma interface do core, usada também pelos drivers e services, que não dependem de Qt.

## Decisão

- `infra/platform` implementa o `ICredentialStore` com a API de cada SO, escolhida pelo CMake:
  - Windows: Credential Manager (`CredWriteW`, `CredReadW`, `CredDeleteW`), alvo `Mydbsql/<chave>`, persistência só no usuário e na máquina;
  - macOS: Keychain (`SecItemAdd`, `SecItemCopyMatching`, `SecItemUpdate`, `SecItemDelete`), serviço `Mydbsql`;
  - Linux: libsecret (secret service do GNOME Keyring, KWallet etc.), aberta com `dlopen` só quando usada, sem exigir os headers no build.
- `createSystemCredentialStore()` devolve `nullptr` quando o SO não tem cofre utilizável (ex.: Linux sem secret service). O app usa então um cofre só em memória (`ICredentialStore::persistent() == false`), avisa o usuário e pede a senha a cada conexão. A senha nunca é gravada em texto puro (§0).

## Consequências

- Nenhuma dependência nova e nada de Qt no caminho das senhas.
- Cerca de 100 linhas por SO, dentro de `src/infra/platform/` (§2.6, regra 1). O teste `SystemCredentialStoreTest` grava, lê e apaga uma chave de teste no cofre real; é pulado quando não há cofre.
- No Linux, o teste da CI é pulado (o runner não tem secret service). A integração com o GNOME Keyring precisa ser conferida numa máquina com sessão gráfica.
