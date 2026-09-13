# ADR-0006 — Dense output: extensão contínua de 4ª ordem do Dormand–Prince

Status: **aceito** · Data: 2026-09-13
Estende: ADR-0005 (propagação)

## Contexto

O propagador escolhe seus próprios passos por controle de erro — 5 s perto do
periastro, 3600 s no apoastro. Os consumidores precisam do estado em instantes
que **eles** escolhem:

* o renderizador (Milestone 2) precisa de um estado por frame, a 60 Hz;
* a CLI precisa de amostras em épocas redondas para tabelas e gráficos;
* a detecção de eventos (periastro, entrada em raio de corpo, fim de queima)
  precisa avaliar uma função em pontos arbitrários dentro de um passo;
* o planejador de manobras (Milestone 1) precisa do estado no instante da ignição.

Há três maneiras de atender a isso, e duas são armadilhas:

1. **Forçar o passo do integrador a coincidir com o instante pedido.** É
   exatamente o que §12 do enunciado proíbe ("não vincule o timestep ao
   framerate"): a 60 Hz isso impõe passos de 16,7 ms a um integrador que queria
   dar passos de 600 s, multiplicando o custo por 4 ordens de grandeza e —
   pior — mudando o resultado numérico conforme a taxa de quadros.
2. **Interpolar linearmente entre estados guardados.** Sobre um passo de 600 s de
   uma órbita LEO, o erro de uma reta contra o arco real é da ordem de
   `(1/8)|a|h² ≈ 4·10⁵ m`. Isso destruiria, na apresentação, uma trajetória que
   o núcleo calculou com erro de 10⁻⁴ m.
3. **Extensão contínua do próprio método** — a escolha desta ADR.

## Decisão

Implementar a **extensão contínua de 4ª ordem** do Dormand–Prince 5(4)
(Shampine 1986; Hairer, Nørsett & Wanner, *Solving ODEs I*, §II.6, rotina
`contd5`).

Para cada passo aceito de `t₀` a `t₀ + h`, guardamos cinco coeficientes vetoriais
construídos a partir dos estágios `k₁…k₇` que o passo **já calculou**:

```
c₁ = y₀
c₂ = y₁ − y₀
c₃ = h k₁ − c₂
c₄ = c₂ − h k₇ − c₃
c₅ = h Σ dᵢ kᵢ          (d: coeficientes tabelados do interpolante)

y(θ) = c₁ + θ ( c₂ + (1−θ) ( c₃ + θ ( c₄ + (1−θ) c₅ ) ) ) ,   θ = (t − t₀)/h
```

Propriedades que motivam a escolha:

* **custo zero em avaliações de força.** Usa os sete estágios do passo, incluindo
  o `k₇` que o FSAL já produz. Nenhuma avaliação extra do modelo de forças.
* **não altera a integração.** A trajetória calculada é bit a bit idêntica com e
  sem dense output ligado — isto é um teste, não uma promessa.
* **exata nos extremos.** `y(0) = y₀` e `y(1) = y₁` por construção, então
  amostrar exatamente em uma fronteira de passo devolve o valor do passo.
* **ordem 4**, isto é, erro de interpolação `O(h⁵)` — uma ordem abaixo da solução
  de 5ª ordem nos extremos. Consequência honesta: **um estado interpolado é
  ligeiramente menos preciso que um estado de fronteira de passo**, e a diferença
  cresce no meio do passo. Medido em órbita LEO com `h ≈ 20 s`: ~10⁻⁷ m.

## Alternativas consideradas

| Alternativa | Por que não |
|---|---|
| Interpolação de Hermite cúbica (usando só `y`, `y'` nos extremos) | ordem 3, erro `O(h⁴)`; mais simples, porém sem vantagem: o interpolante de 4ª ordem também é gratuito |
| Interpolante de ordem 5 com estágio extra | exigiria uma avaliação de força adicional por passo (~15 % do custo) para ganhar uma ordem que ninguém precisa na saída |
| Reintegrar do início até cada instante pedido | O(n²) e absurdo: é o que a CLI fazia por segmentos antes desta ADR |
| Guardar todos os passos e interpolar linearmente | erro de 10⁵ m, ver Contexto |
| Passo fixo pequeno o bastante para amostrar direto | perde o controle de erro; proibido pelo §12 |

## Consequências

* Nova estrutura `Trajectory` em `core/propagation/`: sequência de segmentos
  densos com busca binária por época, `state_at(t)`, e amostragem uniforme.
  Cada segmento custa 5 vetores de 7 doubles = 280 bytes; uma órbita LEO com
  ~340 passos ocupa ~95 kB, e um dia de voo interplanetário com passos de 1 h,
  ~7 kB. A memória não é um problema nesta escala, mas a gravação é **opcional**
  (`set_trajectory_recorder(nullptr)` por padrão) para propagações longas de
  planejamento, onde só o estado final interessa.
* Pedir um instante fora da trajetória gravada é **erro**, não extrapolação —
  mesma regra do `EphemerisProvider` (ADR-0003).
* `orbit-cli propagate` passa a integrar **uma vez** e amostrar do resultado, em
  vez de propagar segmento a segmento. Além de mais rápido, elimina uma
  diferença sutil: uma propagação reiniciada a cada amostra recomeça o
  controlador de passo, e portanto não produzia exatamente a mesma trajetória que
  uma propagação contínua.
* O renderizador (Milestone 2) consome `Trajectory`, nunca o integrador. Se o
  jogo estiver a 144 Hz, isso muda quantas vezes o interpolante é avaliado, e
  **nada mais**.
