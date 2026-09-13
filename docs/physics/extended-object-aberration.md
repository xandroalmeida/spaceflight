# Aberração de objetos extensos

Status: **auditado; transformação diferencial por vértice já existe**  
Última revisão: 2026-09-13

## Problema

Aberrar somente a direção do centro trata um planeta como disco rígido. Para um
raio angular grande, pontos diferentes têm direções de chegada diferentes e
tempos de emissão diferentes. Escalar a mesh por `1/γ` também não produz uma
fotografia: mistura simultaneidade coordenada com recepção simultânea.

## Método usado

O centro é retardado pela efeméride. Para cada vértice, no shader:

1. transforma-se o offset de repouso para a fatia simultânea do observador
   (`lorentz_contract`);
2. resolve-se exatamente o cone de luz para movimento retilíneo durante o tempo
   de travessia do corpo;
3. subtrai-se o retardo do centro, evitando aplicar duas vezes o tempo de luz;
4. o offset aparente resultante deforma a mesh.

O resultado está em `core/render/terrell.hpp` e
`godot/project/shaders/relativistic_body.gdshader`. Os testes com esfera medem
silhueta e rotação de Terrell; a literatura moderna confirma que a aparência é
determinada pelos eventos de emissão de cada ponto, não por uma contração rígida
[visualização experimental do efeito Terrell–Penrose](https://arxiv.org/abs/2409.04296).

## Validade

- velocidade relativa constante durante o tempo de travessia luminosa;
- mesh representa forma no referencial de repouso;
- luz em espaço plano no interior angular do objeto;
- corpo opaco sem atmosfera refrativa;
- velocidade de todos os vértices subluminal.

Para a Terra, a travessia é 42 ms pelo diâmetro; aceleração orbital e rotação
mudam a velocidade em quantidade desprezível nesse intervalo. Perto de uma
fonte compacta, ray tracing nulo curvo substituiria este método.

## Custo

São, por vértice, aproximadamente dois produtos escalares, uma raiz, duas
divisões e operações vetoriais, além da transformação habitual. O custo é
`O(N_vertices)` na GPU e não exige memória por frame. Uma esfera de 64×32 usa
~2 mil vértices; oito corpos custam ~16 mil avaliações, muito abaixo dos ~8,8
mil pontos do starfield e do raster. Ray tracing por pixel seria
`O(N_pixels × iterações)` e só se justifica para lente gravitacional ou
superfícies muito grandes na tela.

## Casos de falsificação

- `β=0`: identidade bit a bit dentro da aritmética do shader;
- esfera distante: silhueta circular e rotação aparente `asin(β)`;
- reduzir distância: desvio deve crescer continuamente com o raio angular;
- inverter `β`: rotação troca de sentido;
- CPU versus shader em vértices escolhidos;
- ligar/desligar retarded-time sem alterar o estado da simulação.

