# ADR-0001 — Linguagem do núcleo: C++20

Status: **aceito** · Data: 2026-09-13 · Decisor: definido no enunciado do projeto

## Contexto

O núcleo é numérico, de longa duração, precisa interoperar com bibliotecas
científicas escritas em C (CSPICE, potencialmente REBOUND) e precisa ser
consumido por uma GDExtension do Godot 4 (ABI C++).

## Decisão

**C++20** para todo o núcleo. C apenas na fronteira com bibliotecas nativas.

Recursos usados deliberadamente: RAII, ownership explícito, `std::unique_ptr`,
`std::span`, `std::chrono`, `constexpr`, `<numbers>`, `concepts` onde
simplificam, tipos fortes para unidades onde pagam. `std::shared_ptr` apenas
quando a posse é genuinamente compartilhada.

Exclusões conscientes: **sem** módulos C++20 (suporte ainda irregular entre
CMake/Clang/GCC/MSVC e frágil com godot-cpp), **sem** exceções em caminhos
quentes do integrador, **sem** RTTI dependente para lógica de domínio,
**sem** estado global mutável.

## Alternativas consideradas

| Alternativa | Por que não |
|---|---|
| C puro | sem RAII e sem tipos fortes; a gestão de kernels e de modelos de força viraria disciplina manual |
| Rust | excelente para o núcleo, mas a integração com godot-cpp e com CSPICE (2229 arquivos C gerados por f2c) passaria a custar mais do que rende; o ecossistema de efemérides maduro é C |
| C++17 | perde `std::span`, `concepts`, `<numbers>`, `constexpr` estendido, comparação `<=>` |
| C++23 | ainda não uniformemente disponível nos toolchains alvo (Apple Clang, MSVC) |
| Python + NumPy no núcleo | inviável para integração adaptativa em tempo interativo |

## Consequências

* Compilamos com `-Wall -Wextra -Wpedantic` e tratamos aviso como erro no CI.
* `double` em toda parte; `float` só na fronteira de renderização.
* A ponte com CSPICE exige `extern "C"` e conversão de erro do SPICE em exceções
  tipadas, isolada em `core/ephemeris/` (ver ADR-0003).
* Padrão mínimo exigido no CMake: `cxx_std_20`, sem extensões de compilador
  (`CXX_EXTENSIONS OFF`).
