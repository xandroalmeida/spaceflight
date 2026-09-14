# Kernels SPICE

Kernels **não** são versionados no Git (dezenas de MB, versionados pela NAIF).
Baixe com:

```bash
./scripts/fetch_kernels.sh
# opcional: DE440 completo (1550-2650, 114 MB)
SPACEFLIGHT_FULL_EPHEMERIS=1 ./scripts/fetch_kernels.sh
```

Origem: <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/>

| Arquivo | Tipo | Papel | Cobertura |
|---|---|---|---|
| `lsk/naif0012.tls` | LSK | leap seconds, UTC ↔ TDB | desde 1972 |
| `pck/pck00011.tpc` | PCK texto | raios, achatamento, orientação dos corpos | — |
| `pck/gm_de440.tpc` | PCK texto | `GM` consistentes com DE440 | — |
| `spk/de440s.bsp` | SPK binário | efemérides planetárias DE440 (span curto) | 1849-12-26 a 2150-01-21 |
| `spk/de440.bsp` | SPK binário | DE440 completo (opcional) | 1550-01-01 a 2650-01-25 |
| `spk/mar099s.bsp` | SPK binário | Marte (499), Fobos (401), Deimos (402) | 1995-01-01 a 2050-01-01 |

A ordem de carregamento importa quando há sobreposição: o **último** kernel
carregado tem precedência. `SpiceKernelSet` carrega em ordem determinística
(LSK, PCK, SPK) e registra o que carregou.

Dentro dos SPK a ordem é alfabética, então `mar099s.bsp` carrega **depois** de
`de440s.bsp` e tem precedência sobre os segmentos que os dois compartilham (Sol,
Terra, baricentros 3 e 4). Isso foi **medido** e não presumido: a posição
baricêntrica da Terra em 2026-01-01 sai idêntica ao último dígito impresso com e
sem o `mar099s.bsp` carregado — MAR099 é ajustado ao DE440. O DE440 continua
sendo a efeméride planetária.

Sem `mar099s.bsp` o corpo 499 não existe para o SPICE (`SPKINSUFFDATA`) e Marte
deixa de ser um destino: o baricentro 4 é um ponto, não um lugar. A distância
entre os dois, medida no mesmo instante, é de **10 cm**.

## Checksums

Gere/verifique com:

```bash
shasum -a 256 kernels/spice/*/* > kernels/SHA256SUMS
shasum -a 256 -c kernels/SHA256SUMS
```
