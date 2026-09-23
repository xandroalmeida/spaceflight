# Sistema de Coordenadas, Referenciais e Tempo

Status: vigente a partir do Milestone 0
Decisão formal: ADR-0004
Última revisão: 2026-09-13

## 1. Referencial primário

O **estado autoritativo** da simulação é expresso em:

```
origem  : Solar System Barycenter (NAIF id 0, "SSB")
eixos   : frame SPICE "J2000"  (≡ EME2000, alinhado ao ICRF)
unidades: metros, metros/segundo
tempo   : TDB (Barycentric Dynamical Time), segundos desde J2000
```

Notação usada no código e nos documentos: `SSB/J2000`.

### Por que a origem é o baricentro e não o Sol

O baricentro do Sistema Solar é (por construção das efemérides DE) o ponto em
torno do qual as efemérides JPL são definidas e o referencial em que elas são
mais precisas. O Sol **orbita** o baricentro com amplitude de ~1,3·10⁹ m
(aproximadamente dois raios solares), dominada por Júpiter. Adotar o Sol como
origem de um estado dito "inercial" significaria adotar uma origem acelerada e
introduzir termos de força fictícia que teriam de ser reintroduzidos à mão.

Coordenadas heliocêntricas continuam disponíveis — como **derivação**, nunca
como estado absoluto (requisito §8 do enunciado).

### Por que os eixos são J2000/ICRF e não eclípticos

* É o frame nativo das efemérides DE440 e o frame padrão de saída do SPICE
  (`spkezr_c(..., "J2000", ...)`), portanto não há rotação alguma entre o dado
  bruto e o estado da simulação — zero erro introduzido, zero custo.
* É inercial para os fins desta simulação (não gira com nada).
* `ECLIPJ2000` é conveniente para a *interface* (o plano da eclíptica é o plano
  natural de um mapa do Sistema Solar) e será oferecido como transformação de
  apresentação.

Nota de rigor: o frame SPICE `J2000` é, na prática, o EME2000; a diferença entre
ele e o ICRF é uma rotação fixa da ordem de 0,02 arcsec (~10⁻⁷ rad), o que a
1 UA corresponde a ~1,5·10⁴ m. Isso é **irrelevante** para dinâmica (é uma
rotação rígida: não altera distâncias, energias nem períodos) mas seria relevante
para apontamento astrométrico de alta precisão. Registrado aqui para não ser
"descoberto" mais tarde como bug.

## 2. Referenciais derivados

Todos obtidos por transformação a partir de `SSB/J2000`, nunca armazenados como
verdade paralela:

| Frame derivado | Transformação |
|---|---|
| heliocêntrico | translação: `r' = r − r_Sun(t)` (e `v' = v − v_Sun(t)`) |
| geocêntrico | translação: `r' = r − r_Earth(t)` |
| body-centered | translação: `r' = r − r_body(t)` |
| spacecraft-centered | translação pelo estado da nave |
| ECLIPJ2000 | rotação fixa (obliquidade ε = 84381,448 arcsec no SPICE) |
| body-fixed (IAU_EARTH, ITRF93) | rotação dependente do tempo, via `pxform_c` |

Duas observações que o código respeita:

1. **Translação de origem não é rotação**: mudar de SSB para geocêntrico
   preserva os eixos. A velocidade muda porque a nova origem se move.
2. Um frame *body-fixed* é **não inercial**. Ele nunca será usado como frame de
   integração. Serve para entrada/saída (latitude, longitude, altitude).

## 3. Representação em código

```cpp
struct Origin      { BodyId body; };              // SSB = BodyId{0}
enum class FrameAxes { J2000, ECLIPJ2000, ... };  // orientação
struct Frame       { Origin origin; FrameAxes axes; };
struct StateVector { Vec3 position; Vec3 velocity; };  // SI, sem frame embutido
struct BodyState   { StateVector state; CoordinateTime epoch; Frame frame; };
```

`StateVector` deliberadamente **não** carrega o frame: é um par de vetores. Quem
carrega frame e época é `BodyState`. Isso evita que funções numéricas internas
(integrador) paguem o custo de carregar metadados a cada avaliação, e mantém a
conversão explícita nas fronteiras.

## 4. Escalas de tempo

| Escala | Papel | Relação |
|---|---|---|
| **TDB** | tempo coordenado da simulação; argumento das efemérides | é o "ET" do SPICE |
| TT | tempo terrestre | TDB − TT é periódico, amplitude ≈ 1,7 ms |
| TAI | tempo atômico | TT = TAI + 32,184 s (exato, por definição) |
| UTC | entrada/saída humana | UTC = TAI − (leap seconds), via kernel LSK |
| **tempo próprio** | relógio da nave | variável de estado integrada, ver §6 |

Época de referência: **J2000 = 2000-01-01 12:00:00 TDB = JD 2451545,0 (TDB)**.

Conversões UTC↔TDB são feitas **exclusivamente** pelo SPICE (`str2et_c`,
`timout_c`, `et2utc_c`) com o kernel `naif0012.tls` carregado. Não há tabela de
leap seconds escrita à mão no repositório.

## 5. `CoordinateTime`: representação em duas partes

Um `double` armazenando segundos TDB desde J2000 tem resolução de
`|t| · 2⁻⁵³`. Em 2026, `t ≈ 8,4·10⁸ s`, portanto a resolução é ≈ 1,9·10⁻⁷ s.

Consequência:

| Regime | Deslocamento durante 1 ulp de tempo |
|---|---|
| LEO (7,8 km/s) | 1,5·10⁻³ m — irrelevante |
| 0,99 c | ≈ 56 m — **inaceitável** no Milestone 4 |

Por isso `CoordinateTime` é armazenado como par:

```cpp
class CoordinateTime {
    double whole_;  // segundos inteiros TDB desde J2000 (exato até 2^53)
    double frac_;   // resíduo, |frac_| <= 0.5
};
```

Resolução resultante ≈ 10⁻¹⁶ s, independente da época. A soma usa
*two-sum* (Knuth) e renormaliza, de modo que somar N passos pequenos não perde
os passos pequenos. Isso é testado em `tests/unit/test_coordinate_time.cpp`
(somar 10⁸ passos de 10⁻³ s e recuperar 10⁵ s exatamente).

A conversão para o `double` único que o CSPICE exige (`SpiceDouble et`) acontece
só na chamada de efeméride, onde a perda é irrelevante (as efemérides DE têm
precisão muito inferior a 10⁻⁷ s de qualquer forma).

## 6. Tempo coordenado × tempo próprio

Grandezas **distintas**, com tipos distintos, nunca intercambiáveis:

* `CoordinateTime` — parâmetro de evolução, comum a toda a simulação;
* `ProperTime` — leitura do relógio que viaja com a nave, obtida integrando

```
dτ/dt = 1/γ                          (relatividade especial)
dτ/dt = 1/γ · (1 + Φ/c² + …)         (campo fraco, Milestone 4)
```

Em Milestone 0 a nave é newtoniana e `dτ/dt = 1`; a variável já existe no estado
de propagação para que a introdução do termo relativístico seja uma mudança de
modelo, não uma mudança de arquitetura.

## 7. Orçamento de erro de arredondamento (`double`, ε = 2,22·10⁻¹⁶)

| Quantidade | Magnitude típica | 1 ulp |
|---|---|---|
| posição em SSB, órbita terrestre | 1,5·10¹¹ m | 3,3·10⁻⁵ m |
| posição em SSB, Netuno | 4,5·10¹² m | 1,0·10⁻³ m |
| velocidade orbital terrestre | 3,0·10⁴ m/s | 6,7·10⁻¹² m/s |
| raio de LEO (geocêntrico) | 6,8·10⁶ m | 1,5·10⁻⁹ m |

Leitura correta desta tabela: manter a nave em `SSB/J2000` degrada a resolução
da sua posição *relativa à Terra* de ~1,5 nm para ~33 µm. Isso é aceitável e é
uma escolha consciente: a alternativa (integrar em frame geocêntrico) exigiria
tratar a Terra como origem acelerada e introduzir o termo de terceiro corpo com
diferença de cubos, que tem seu próprio cancelamento catastrófico.

Se e quando a acumulação de arredondamento se tornar mensurável nos testes de
conservação, a resposta correta é *compensated summation* na variável de estado
do integrador — não trocar de referencial.

## 8. Regras de uso (invariantes verificáveis)

1. Nenhum estado físico é armazenado em coordenadas de cena do renderizador.
2. Nenhuma função do core aceita "posição" sem que o frame seja conhecido pelo
   contexto ou explicitado no tipo.
3. Nenhuma transformação de frame é feita por multiplicação de matriz escrita à
   mão quando o SPICE já a fornece (`pxform_c`, `sxform_c`).
4. Floating origin e camera-relative rendering (Milestone 2) **leem** o estado;
   jamais o modificam (§23 do enunciado).
