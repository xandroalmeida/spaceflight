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

A ordem de carregamento importa quando há sobreposição: o **último** kernel
carregado tem precedência. `SpiceKernelSet` carrega em ordem determinística
(LSK, PCK, SPK) e registra o que carregou.

## Checksums

Gere/verifique com:

```bash
shasum -a 256 kernels/spice/*/* > kernels/SHA256SUMS
shasum -a 256 -c kernels/SHA256SUMS
```
