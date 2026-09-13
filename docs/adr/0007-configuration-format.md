# ADR-0007 — Configuração: subconjunto estrito de JSON, parser no core

Status: **aceito** · Data: 2026-09-13

## Contexto

§18 do enunciado exige que os parâmetros do motor fiquem **fora do código**, para
que se possa experimentar tecnologias diferentes sem recompilar. O mesmo vale
para cenários (`orbit-cli propagate scenario.json`) e, mais tarde, para naves
inteiras e planos de missão.

O enunciado ilustra com YAML. YAML completo é uma linguagem grande — âncoras,
tags, múltiplos documentos, oito maneiras de escrever uma string, `NO` que vira
`false` — e nenhum parser de YAML de qualidade cabe em "sem dependências".

No Milestone 0 já existia um leitor de JSON mínimo dentro de `tools/orbit-cli/`,
escrito para os cenários. Com o motor a configurar, o **core** passa a precisar
ler configuração, e o core não pode depender de uma ferramenta.

## Decisão

1. Formato: **JSON**, com uma única extensão — comentários de linha `//`.
2. O parser vive em `core/config/`, é próprio, usa só a biblioteca padrão, é
   somente-leitura e **estrito**: recusa o que não entende em vez de adivinhar.
3. Erros citam **linha e coluna** e o nome do campo. Uma configuração errada é a
   forma mais provável de um usuário encontrar este projeto pela primeira vez.
4. Toda estrutura carregada valida seus invariantes físicos na construção
   (`0 < w ≤ c`, `0 < η ≤ 1`, massas positivas), e falha alto.

Comentários são a extensão porque um arquivo de configuração de física sem
espaço para explicar de onde veio um número é um arquivo que vai acumular números
inexplicáveis.

## Alternativas consideradas

| Alternativa | Por que não |
|---|---|
| YAML completo (libyaml, yaml-cpp) | dependência externa grande para ler ~20 campos; e a superfície do YAML convida a erros silenciosos (`exhaust_velocity: 1e-1` vira string em YAML 1.1) |
| Subconjunto de YAML escrito por nós | a indentação significativa é justamente a parte difícil de acertar; ganharíamos a sintaxe do enunciado e perderíamos a robustez |
| TOML | bom formato, mas exigiria escrever outro parser; JSON já estava escrito e funcionando |
| Parâmetros em código com recompilação | proibido por §18 |
| Variáveis de ambiente / flags de CLI | não compõem: um motor tem ~6 parâmetros acoplados que precisam ser versionados juntos |

## Consequências

* `tools/orbit-cli/json.{hpp,cpp}` move para `core/config/json.{hpp,cpp}`
  (namespace `sf::config`); a CLI passa a usar o do core. Um parser, um
  comportamento de erro, um conjunto de testes.
* Os exemplos em `docs/physics/propulsion-model.md` §6 estão em YAML porque o
  enunciado os escreveu assim; o formato real é o JSON equivalente, e o documento
  passa a mostrar os dois.
* Arquivos de configuração são **dados versionados**: ficam em `config/` no
  repositório, com comentários explicando a origem de cada número, como os
  kernels têm `MANIFEST.md`.
* O parser não é um formato de intercâmbio: se um dia for preciso ler JSON de
  terceiros (efemérides Horizons em JSON, por exemplo), a decisão de trazer uma
  biblioteca completa é separada desta.
