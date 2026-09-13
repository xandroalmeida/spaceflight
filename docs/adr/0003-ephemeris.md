# ADR-0003 — Efemérides: NASA/JPL CSPICE + DE440

Status: **aceito** · Data: 2026-09-13

## Contexto

Precisamos das posições e velocidades dos corpos massivos do Sistema Solar em
qualquer instante, com precisão de referência e sem integrar as órbitas
planetárias nós mesmos (§7 do enunciado).

## Decisão

1. **CSPICE (NAIF toolkit N0067)** como biblioteca de efemérides, compilada a
   partir do fonte como alvo CMake próprio (`cspice`), estático.
2. **JPL DE440** como efeméride planetária padrão, no kernel `de440s.bsp`
   (cobertura 1849-12-26 a 2150-01-21).
3. SPICE é a **fonte autoritativa** dos estados dos corpos massivos. A nave é
   partícula-teste e não altera nada (ver `docs/physics/gravity-model.md` §2).
4. Todo acesso passa pela interface `EphemerisProvider`; CSPICE só aparece em
   `core/ephemeris/*.cpp`.

Kernels do conjunto mínimo:

| Kernel | Papel |
|---|---|
| `lsk/naif0012.tls` | leap seconds (UTC ↔ TDB) |
| `pck/pck00011.tpc` | constantes planetárias, raios, orientações |
| `pck/gm_de440.tpc` | parâmetros gravitacionais `GM` consistentes com DE440 |
| `spk/de440s.bsp` | efemérides planetárias DE440 (versão curta) |

## Alternativas consideradas

| Alternativa | Por que não |
|---|---|
| Integrar as órbitas planetárias | erro acumulado nosso, sem ganho; JPL faz melhor e é a referência contra a qual seríamos julgados |
| Elementos keplerianos + taxas seculares (ex. VSOP87 truncado, "Standish") | erro de ~10⁴–10⁶ km em séculos; inadequado para navegação |
| DE441 (cobertura −13200 a +17191) | 3,1 GB; desnecessário para o escopo temporal do simulador |
| `de440.bsp` completo (1550–2650, 114 MB) | suportado como opção; `de440s` cobre folgadamente o período jogável |
| SPICE via wrapper de terceiros (cspyce, SpiceyPy) | linguagem errada; e a dependência real continua sendo o CSPICE |
| Horizons API (rede) | simulação não pode depender de rede nem de latência |

## Consequências

* Existe uma **janela temporal válida** (1849–2150 com `de440s`). Pedir um estado
  fora dela é erro explícito (`EphemerisUnavailable`), nunca extrapolação
  silenciosa. Isso é testado.
* O CSPICE tem estado global e não é thread-safe → `SpiceEphemerisProvider`
  serializa o acesso com mutex e documenta isso.
* O tratamento de erro do SPICE é posto em modo `RETURN` (`erract_c("SET","RETURN")`)
  e o *device* de mensagens em `NULL`: o toolkit **nunca** aborta o processo nem
  escreve em `stdout`; `failed_c()` é verificado e convertido em exceção C++.
* `GM` vem do kernel, não de constantes escritas à mão — evita divergência entre
  a massa usada na dinâmica e a massa usada na geração da efeméride.
* Kernels são dados grandes e versionados externamente: ficam fora do Git
  (`.gitignore`), baixados por `scripts/fetch_kernels.sh`, com origem e checksum
  registrados em `kernels/MANIFEST.md`.
* Substituir a fonte de efemérides (por exemplo, um provider analítico em testes)
  é trocar a implementação da interface, não mexer na física.
