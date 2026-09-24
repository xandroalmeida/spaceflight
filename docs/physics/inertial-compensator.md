# Compensador inercial

⚠️ **Ficção declarada.** Não existe física conhecida que faça isto. Este
documento diz o que a ficção é, onde ela entra e — mais importante — onde ela
**não** entra.

## 1. O problema

O modo RELATIVÍSTICO ([`propulsion-model.md`](propulsion-model.md) §4.7) empurra a
nave a 100 g com o tanque cheio e a 2 000 g com ele vazio. Um corpo humano
aguenta 1 g indefinidamente, algo como 1,5 g por meses, poucos g por horas e
nove por segundos. A 100 g a tripulação morre em segundos.

## 2. A ficção

Um campo que age sobre a **tripulação e a cabine**, e só sobre elas, e cancela a
parte da aceleração própria da nave que passa de um teto:

```
a_real   = (F_motor + F_RCS) / m          aceleração própria do casco
a_cabine = a_real                          se |a_real| ≤ 1 g
         = a_real · (1 g / |a_real|)       se |a_real| > 1 g
```

A cabine continua sentindo **para que lado** a nave empurra — a direção é a
mesma —, mas nunca mais que um g. Abaixo do teto o compensador não faz nada: a
1,02 g do IMPULSO com o tanque cheio ele apara 0,02 g; em órbita, em queda
livre, a cabine sente zero, como sempre.

`FlightSession::cabin_acceleration_body()`, com o teto em
`FlightSession::CABIN_LIMIT_MS2 = g₀`.

## 3. O que a ficção NÃO faz

**Não muda a trajetória.** O campo é uma força **interna**: empurra a tripulação
contra o resto da nave, e a reação fica no casco. O centro de massa do conjunto
— que é o que o integrador propaga — não sabe que ele existe. Por construção:
o compensador não é um `ForceModel`, não é somado a nada no integrador, e é
calculado **a partir** da aceleração própria que o modelo de forças produziu.

**Não muda a cinemática.** A nave acelera a 100 g de verdade, e a relatividade
cobra por isso exatamente como cobraria sem ficção nenhuma: o `β` segue
`tanh((η w/c) ln(m₀/m))`, o relógio de bordo atrasa, e `c` nunca é atingido.

**Não cria energia nem momento.** O que ele cancela na cabine é empurrado para o
casco, que já estava acelerando com essa aceleração.

## 4. O que se vê

No painel, a célula `ACCEL  REAL` mostra em cima a aceleração do casco, que é a
do acelerômetro e a que o integrador usa, e embaixo `CABIN`, o que a
tripulação sente. Com o compensador atuando a leitura real fica âmbar, e `IC`
acende ao lado da cabine. No HUD mínimo a real fica na linha do acelerador, e
`IC  cabin 1.00 g` aparece no canto superior esquerdo enquanto ele atua. No
`F3`, as três acelerações lado a lado: a coordenada (com a gravidade), a própria
e a da cabine.

## 5. Verificação

`tests/presentation/test_presentation_flight.cpp`,
`the_relativistic_mode_pushes_at_a_hundred_g_and_the_cabin_feels_one`: a 100 g,
a aceleração própria é exatamente empuxo/massa, a da cabine é **exatamente**
1 g (1·10⁻¹² relativo) e na mesma direção, e as duas leituras cabem no painel.
