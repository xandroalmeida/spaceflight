# Cenários do `orbit-cli`

Cada arquivo é um estado inicial, um modelo de forças e, quando há, uma lista de
queimas, em JSON com comentários (ADR-0007). O comentário no topo de cada um diz o
que observar na saída.

| Cenário | Comando | O que mostra |
|---|---|---|
| `leo-circular.json` | `orbit-cli propagate tests/scenarios/leo-circular.json` | uma órbita de 400 km / 51,6° fechando após um período |
| `leo-j2-nodal-regression.json` | `orbit-cli propagate tests/scenarios/leo-j2-nodal-regression.json` | um dia com J2: o nodo regride ~5 °/dia |
| `leo-raise-apoapsis.json` | `orbit-cli propagate tests/scenarios/leo-raise-apoapsis.json` | uma queima: apoastro de 6 778 km → 19 950 km, com a perda gravitacional |
| `earth-escape.json` | `orbit-cli propagate tests/scenarios/earth-escape.json` | a energia específica cruza zero |
| `lunar-transfer-ish.json` | `orbit-cli propagate tests/scenarios/lunar-transfer-ish.json` | apogeu na distância da Lua, sem mirar nela |
| `lunar-intercept.json` | `orbit-cli intercept tests/scenarios/lunar-intercept.json --to Moon --tof 4.5` | Lambert + targeting até a região da Lua |

O mesmo `lunar-intercept.json` alimenta a campanha Terra–Lua:

```bash
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --epochs 100
scripts/lunar_campaign.sh 365 8        # um ano, em processos paralelos
```

Os binários ficam em `build/bin/`.
