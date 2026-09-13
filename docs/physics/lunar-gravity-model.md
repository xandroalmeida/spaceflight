# Qualificação progressiva do campo gravitacional lunar

Status: **investigação; monopolo em produção, J2 disponível no core**  
Última revisão: 2026-09-13

A gravidade não esférica domina a evolução de órbitas lunares baixas. Modelos
GRAIL chegam a centenas de graus; isso não significa que toda missão precise
pagá-los. O [relatório GRAIL de grau e ordem
420](https://ntrs.nasa.gov/citations/20120016299) e a revisão de [modelos de
gravidade lunar](https://ntrs.nasa.gov/api/citations/20070035736/downloads/20070035736.pdf)
são as referências externas.

## Sequência de modelos

```text
M0  monopolo
M2a monopolo + J2
M2b monopolo + J2 + C22/S22 em referencial lunar rotativo
MN  harmônicos normalizados até grau/ordem N, coeficientes GRAIL
```

`C22` não pode ser adicionado como força fixa em J2000: o bojo tesseral gira com
a Lua. A transformação IAU_MOON e sua velocidade angular fazem parte do modelo.

## Critério do projeto

“Grau necessário” depende de duração e tolerância. Para fixar a pergunta, a
qualificação será: erro de posição 3D menor que 100 m no arco de aproximação e
menor que 10 m após uma órbita lunar, contra um GRAIL de grau 420 integrado com
tolerância duas décadas mais apertada. A tabela abaixo é uma **matriz inicial a
falsificar**, não resultado já certificado:

| altitude | modelo candidato mínimo | comparação obrigatória |
|---:|---|---|
| 1000 km | `J2+C22`, depois grau 4 | M0, M2a, M2b, N=4, referência 420 |
| 500 km | grau 8 | N=4,8,16, referência 420 |
| 100 km | grau 50 | N=20,50,100, referência 420 |
| 50 km | grau 100 | N=50,100,200, referência 420 |
| 10 km | grau 300 ou modelo local | N=100,200,300,420 |

A atenuação radial de um termo de grau `l` escala aproximadamente como
`(R/r)^(l+2)`, mas os coeficientes lunares são irregulares; essa fórmula só
orienta a varredura e não pode escolher o grau sozinha. O menor modelo aprovado
será o primeiro que satisfizer o critério em todas as longitudes, latitudes,
épocas e inclinações da campanha. Até carregar coeficientes GRAIL e executar
essa comparação, a inserção lunar deve declarar “monopolo/J2”, nunca “campo
lunar de alta fidelidade”.

