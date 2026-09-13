# Catálogos estelares

Catálogos **não** são versionados no Git, pela mesma razão que os kernels SPICE:
são dados publicados e versionados por terceiros. Baixe com:

```bash
./scripts/fetch_star_catalog.sh
```

| Arquivo | Origem | Conteúdo |
|---|---|---|
| `bsc5.dat` | VizieR **V/50** (espelho: `tdc-www.harvard.edu`) | Yale Bright Star Catalogue, 5ª ed., 9 110 registros |
| `bsc5.readme` | VizieR V/50 `ReadMe` | descrição byte a byte — as colunas do parser vieram daqui |

Referência: Hoffleit D. & Warren W.H. Jr. (1991), *The Bright Star Catalogue*,
5th Revised Edition, Yale University Observatory.

## O que o parser usa

Formato de largura fixa. Colunas (1-based, como no `ReadMe`):

| Colunas | Campo |
|---|---|
| 1–4 | `HR`, número Harvard Revised |
| 5–14 | nome (Bayer/Flamsteed) |
| 76–83 | ascensão reta J2000 (h, min, s) |
| 84–90 | declinação J2000 (sinal, °, ′, ″) |
| 103–107 | `Vmag` |
| 110–114 | `B−V` |

## Registros que não viram estrelas

| Quantos | Por quê |
|---|---|
| 14 | sem coordenadas — entradas que o próprio BSC5 mantém para objetos que se revelaram inexistentes (novas, erros históricos) |
| 310 | sem `B−V` — sem cor não há temperatura, e sem temperatura não há deslocamento Doppler a aplicar (`docs/physics/relativistic-rendering.md` §12) |
| **8 786** | **usadas** |

O `StarCatalog::report()` conta as três categorias, e `describe()` as imprime: um
carregador que devolve 8 786 de 9 110 tem que dizer para onde foram os outros 324,
ou o próximo leitor vai supor um bug de parser.

## Checksums

```bash
shasum -a 256 catalogs/bsc5.dat > catalogs/SHA256SUMS
shasum -a 256 -c catalogs/SHA256SUMS
```
