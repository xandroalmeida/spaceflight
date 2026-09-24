# AGENTS.md

Guia para agentes de código neste repositório. O contexto completo está no
[`README.md`](README.md) e em [`docs/architecture/system-architecture.md`](docs/architecture/system-architecture.md);
aqui fica só o que é preciso para trabalhar sem quebrar nada.

## O projeto

Simulador científico de voo espacial no Sistema Solar, em C++20.

- `core/` — núcleo científico (biblioteca `spaceflight::core`, namespace `sf::`).
  Nada gráfico: compila e é testado sem GPU.
- `app/` — o jogo (SDL3 + SDL_GPU + Dear ImGui, ADR-0009). Só desenha o que o
  core entrega; ver [`app/README.md`](app/README.md).
- `tools/` — `orbit-cli` e ferramentas de validação.
- `tests/` — suíte própria (sem framework externo), ver abaixo.
- `docs/` — ADRs, derivações de física, arquitetura, relatórios de validação, manual.
- `scripts/` — download de dados, campanhas, screenshots, manual.

## Build e testes

```bash
./scripts/fetch_kernels.sh && ./scripts/fetch_star_catalog.sh   # dados, fora do Git
cmake -S . -B build
cmake --build build -j
cmake --build build --target ctest-headless   # física + apresentação, sem GPU
cmake --build build --target gpu-validation   # pixels; precisa de GPU (off-screen)
ctest --test-dir build -R unit.vec3           # um teste só (<categoria>.<nome>)
```

- Teste que depende de algo ausente (kernels, GPU) sai com **77** e aparece como
  `Skipped`. Skipped não é aprovado: se o seu teste foi pulado, diga isso.
- `scientific.full_mission` leva de ~15 min a horas; não rode à toa.
- Use `-DSPACEFLIGHT_WERROR=ON` para garantir que não entrou warning novo.
- `-fno-fast-math` é deliberado; não remova.

## Regras invioláveis

1. **Nenhuma esfera de influência.** Todas as fontes gravitacionais contribuem sempre.
2. **Nenhum clamp** (`if (v > c) v = c`). O limite tem de sair da formulação.
3. **Documento → teste analítico → implementação → teste numérico.** Nunca efeito
   visual primeiro. Cada modelo tem derivação em `docs/physics/`.
4. **Toda tolerância é justificada.** `CHECK_NEAR_ABS/REL` exigem a justificativa
   como último argumento; o número vai para `docs/validation/tolerances.md`.
5. **O core não conhece a apresentação.** Nenhum include de `app/` no core, nenhuma
   conta de física em `app/`; shaders recebem grandezas prontas do core.
6. **Uma única implementação.** Jogo, CLI e campanhas usam as mesmas funções
   públicas do core.
7. **Dívida conhecida é registrada** no documento correspondente, com motivo e
   caminho para pagar.
8. **ADRs em `docs/adr/` não se rediscutem** sem razão técnica forte e demonstrável.

## Testes

- Um arquivo por teste: `tests/<categoria>/test_<nome>.cpp`, registrado em
  `tests/CMakeLists.txt` com `spaceflight_add_test(<categoria> <nome>)`.
- Categorias: `unit` (aritmética), `integration` (peças conversando), `scientific`
  (contra referência externa), `regression`, `presentation`, `gpu`.
- Harness em `tests/support/test_harness.hpp` (`TEST(...)`, `CHECK_*`, `REQUIRE_*`,
  `SKIP`); fixtures de kernels, catálogo e fonte ao lado.

## Convenções

- Código e comentários em inglês; documentação, manual e mensagens de commit em
  português.
- Estado em SSB / J2000 (ICRF), unidades SI, tempo TDB em duas partes (ADR-0004).
  Quaternions escalar-primeiro, Hamilton, corpo→inercial (ADR-0008).
- Comentários explicam o *porquê* (origem de um número, decisão recusada), no
  estilo do código ao redor.
- Dados de terceiros (kernels, catálogo, CSPICE, texturas brutas) são baixados
  pelos scripts com checksum e ficam fora do Git (`.gitignore`); não os versione.

## Ao mudar algo

- Física ou arquitetura: atualize o documento em `docs/physics/` ou
  `docs/architecture/`.
- Tecla ou controle: rode `scripts/dump_controls.sh` (regera
  `docs/gameplay/controls.md`) e `scripts/build_manual.sh` (manual e folha de atalhos).
- Renderizador: rode também `gpu-validation`.
- Commits: assunto em português descrevendo o efeito, corpo explicando o porquê e
  o que foi medido/testado.
