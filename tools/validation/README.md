# Ferramentas de validação

Comparações em lote que não cabem na suíte de testes. O que **é** feito em
`tests/` é o que precisa passar em toda build. O que vive aqui é o que se roda
deliberadamente, demora, e produz um relatório.

* varredura de estados contra o JPL Horizons/DE441 ao longo de anos, saída CSV
  — `horizons_cross_check.py`;
* comparação contra REBOUND/REBOUNDx no mesmo problema (§29) — **feito**,
  `reboundx_cross_check.py`;
* estudos de convergência (erro × tolerância × custo) (pendente).

A campanha Terra–Lua **saiu daqui** no Milestone 6.1. `lunar_mission_campaign.py`
dirigia `orbit-cli intercept` e lia a PROSA dele com expressões regulares, o que
fazia o veredicto da campanha depender da redação de um `print` e limitava o que
podia ser registrado ao que a CLI por acaso imprimia. Ela agora é
`tools/lunar-campaign`, em C++, chamando `core/navigation/lunar_transfer.hpp`
direto, com o cabeçalho do CSV morando dentro de `TransferRecord` para que a
ferramenta não possa discordar dos dados:

```bash
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --epochs 100 \
    --csv docs/validation/lunar-navigation-campaign-v2.csv
scripts/lunar_campaign.sh 365 8
```

Ver [`docs/validation/lunar-navigation-hardening.md`](../../docs/validation/lunar-navigation-hardening.md).

## `horizons_cross_check.py`

Consulta sequencialmente Mercury–Neptune, Terra e Lua em três épocas (1900,
presente e 2099), sempre como estado geométrico SSB/ICRF. Compara Horizons/DE441
contra o `de440s` carregado pelo simulador e grava o resíduo por caso:

```bash
./tools/validation/horizons_cross_check.py
```

O script usa a API pública do Horizons e portanto requer rede. A diferença
DE440–DE441 é sinal físico entre ajustes independentes, não tolerância do
integrador.

---

## `reboundx_cross_check.py` — validação cruzada contra outro código

Dois códigos independentes recebem a mesma pergunta — uma partícula de teste em
torno de uma massa pontual, com correções relativísticas de primeira ordem — e
são obrigados a discordar em público.

* **eles**: REBOUND/IAS15 com o operador `gr` do REBOUNDx, a força 1PN de
  Anderson *et al.* (1975) usada em efemérides do sistema solar;
* **nós**: Dormand–Prince 5(4) integrando a geodésica da métrica de campo fraco
  (`docs/physics/relativistic-gravity.md`).

Mesma física, formulações diferentes, nenhuma linha de código em comum.

```bash
cmake --build build --target gr-reference
./tools/validation/reboundx_cross_check.py                    # 40 órbitas de Mercúrio
./tools/validation/reboundx_cross_check.py --orbits 80 --json /tmp/x.json
```

A §29 proíbe tornar o REBOUND dependência da arquitetura, então isto mora em
`tools/`, cria o próprio virtualenv na primeira execução e **nunca** faz parte
de uma build. `gr-reference` é um binário de 200 linhas que só emite CSV: ele
não sabe que o REBOUND existe.

### O resultado (40 órbitas, 9,6 anos)

```
1. controle newtoniano (relatividade desligada nos dois)      98,3 m
   -- isto são os dois integradores, e o piso de tudo abaixo

2. separação relativística                                2,62·10⁶ m
   o próprio sinal da RG (nós RG − nós newtoniano)        3,52·10⁶ m

3. avanço do periélio por órbita
     forma fechada 6πGM/(c²a(1−e²))                    5,018663·10⁻⁷ rad
     nós − REBOUNDx                                   −1,349854·10⁻¹² rad
     discordância relativa                                 2,7·10⁻⁶
     ou seja  −0,0001 arcsec/século  contra 43

4. onde estão os 2,62·10⁶ m
     apsidal                                             −5,4·10⁻¹¹ rad
     temporal                                             3,7·10⁻⁵ rad  → 2,13·10⁶ m
     uma única constante, reescala de tempo de +1,461522·10⁻⁷
       = 5,732 × GM/(ac²), absorve tudo:
     residual                                              1,17·10⁴ m
     (223× menor; o controle é 98 m)
```

### Como ler isso

A separação bruta de **2 600 km** parece um desastre até você perguntar *onde*
ela está.

**O que é observável concorda.** O avanço do periélio por órbita — a quantidade
invariante de gauge, a que Le Verrier mediu — bate em **2,7 partes por milhão**,
ou −0,0001″/século contra 43″. Os dois códigos concordam sobre a física.

**O que difere não é observável.** Todo o resto é *quando* a partícula está
onde, não *onde* ela vai. Uma única constante — uma diferença relativa de
movimento médio de 1,46·10⁻⁷, que é 5,73 × `GM/(ac²)` — absorve 99,6 % da
separação. E o que sobra, 1,17·10⁴ m, é **limitado**: 1,173·10⁴ a 10 órbitas,
1,179·10⁴ a 80. Não é secular, é periódico. E vale **7,96 × GM/c²**, onde
`GM_☉/c² = 1 477 m`.

Essa é a assinatura de uma diferença de **coordenadas**, não de física. Duas
formulações 1PN não precisam usar o mesmo `r`: elas podem diferir em `O(GM/c²)`,
e é exatamente o tamanho do que sobra. "Onde o planeta está no tempo coordenado
`t`" não é uma afirmação invariante; "quanto o periélio avançou por órbita" é —
e essa concorda.

> **O que isto não prova.** Que as duas concordem a 1PN não diz nada sobre uma
> nave a `β = 0,9`, e a §29 avisa disso na última linha: *"não assuma
> automaticamente que correções pós-newtonianas para sistemas planetários
> resolvem o caso de uma nave viajando próxima a `c`"*. O REBOUNDx `gr` é uma
> força 1PN para uma massa dominante; a nossa métrica é `g₀ᵢ = 0`. Onde as duas
> deixam de valer está em `relativistic-gravity.md` §7, e é o mesmo lugar.

### O controle newtoniano é a parte importante

Sem a linha 1 as outras três não significam nada. Com relatividade desligada nos
dois códigos, as trajetórias ficam a **98 m** uma da outra depois de 9,6 anos e
40 órbitas — sobre um raio orbital de 5,8·10¹⁰ m, ou 1,7·10⁻⁹ relativos. Esse é
o piso: qualquer discordância desse tamanho é aritmética, não modelo.

(Ele cresce com `n²`: 6,6 m em 10 órbitas, 421 m em 80. É erro de integração
acumulando na direção ao longo da trajetória, nos dois códigos.)

---

## `plot_trajectory.py`

Desenha uma trajetória já calculada. Só biblioteca padrão do Python — uma
ferramenta de inspeção que exige `pip install` é uma ferramenta que não se roda.
(O `reboundx_cross_check.py` acima é a exceção deliberada: ele *é* a comparação
com outro código, e resolve isso criando o próprio venv.)
Saída: um HTML único, com SVG inline, sem rede e sem assets.

```bash
./build/bin/orbit-cli propagate tests/scenarios/leo-circular.json \
    --samples 600 --csv /tmp/leo.csv
./tools/validation/plot_trajectory.py /tmp/leo.csv --body Earth --title "LEO 400 km"
open /tmp/leo.html
```

Mostra a órbita projetada no **próprio plano orbital** (uma projeção XY de uma
órbita inclinada aparece achatada e convida a concluir que a excentricidade está
errada), o corpo central em escala, e as derivas de energia e momento angular.

Isto **não** é o renderizador: não sabe o que é câmera, floating origin ou renderizador
(ADR-0002). Serve para um humano olhar para números e reconhecer uma trajetória
errada.

---

## `gr-reference`

Binário auxiliar de `reboundx_cross_check.py`, em `tools/gr-reference/`. Propaga
uma partícula de teste em torno de uma massa pontual fixa na origem e escreve
`t,x,y,z,vx,vy,vz` em CSV para a saída padrão. `--mode geodesic` usa a métrica,
`--mode newtonian` usa a força newtoniana com o mesmo integrador.

```bash
./build/bin/gr-reference --gm 1.32712440041e20 --x 4.6001009e10 \
    --vy 58976 --duration 1e6 --samples 100
```

Ele não sabe o que é REBOUND. A comparação é feita por quem tem o outro código.
