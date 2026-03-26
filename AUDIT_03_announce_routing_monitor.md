# AUDIT_03_announce_routing_monitor.md

## 1. Título do ficheiro

**AUDIT_03_announce_routing_monitor — Auditoria funcional de anúncio, propagação de rotas, show routing, monitorização e caminhos mínimos em regime estável**

## 2. Âmbito desta bateria de testes

Esta bateria audita exclusivamente o comportamento observado da implementação nas funções de:

* `announce (a)`;
* `show routing (sr) dest`;
* `start monitor (sm)`;
* `end monitor (em)`;
* mensagens `ROUTE dest n<LF>`;
* aprendizagem e propagação de rotas por número de saltos;
* apresentação ao utilizador do estado relativo a um destino;
* propriedade de caminhos mais curtos na ausência de novos acontecimentos.

Para esta bateria, usa-se a seguinte distinção operacional:

* **destino não conhecido**: ainda não existe anúncio observado pelo nó auditado, pelo que não deve aparecer uma rota válida finita;
* **destino conhecido e alcançável**: o nó já aprendeu uma rota válida e `show routing` deve refletir estado de expedição com distância e vizinho de expedição;
* **destino conhecido na rede mas não alcançável**: o nó sabe, por consulta ao servidor, que esse identificador existe na rede, mas não existe caminho topológico até ele; nesse caso, não deve surgir uma rota válida finita. Esta distinção decorre do facto de `show nodes` revelar participantes da rede, enquanto o protocolo de encaminhamento usa apenas as arestas existentes e define `dist[t] = ∞` se `t` não for alcançável.

## 3. Preparação mínima / topologia recomendada para executar os testes

### 3.1 Regras de preparação

* Recomenda-se executar os testes por grupos de topologia em arranques limpos, porque o enunciado não define um comando padrão para apagar apenas o estado de encaminhamento sem reiniciar a experiência.
* Sempre que possível, usar 4 terminais independentes para os nós `10`, `20`, `30` e `40`. Para o teste de “destino conhecido mas não alcançável”, usar um quinto terminal para o nó `50`.
* Sempre que relevante, a verificação pode ser feita por:

  * saída do próprio programa;
  * `show routing`;
  * `show neighbors`;
  * `start monitor` / `end monitor`;
  * Wireshark, já que o material de apoio recomenda a sua utilização para visualizar mensagens e conteúdos.

### 3.2 Topologia linear recomendada

Usar quatro nós numa linha:

```text
10 —— 20 —— 30 —— 40
```

Preparação recomendada em modo direto:

**Terminal N10**

```text
./OWR 127.0.0.1 5010
dj 073 10
dae 20 127.0.0.1 5020
```

**Terminal N20**

```text
./OWR 127.0.0.1 5020
dj 073 20
dae 10 127.0.0.1 5010
dae 30 127.0.0.1 5030
```

**Terminal N30**

```text
./OWR 127.0.0.1 5030
dj 073 30
dae 20 127.0.0.1 5020
dae 40 127.0.0.1 5040
```

**Terminal N40**

```text
./OWR 127.0.0.1 5040
dj 073 40
dae 30 127.0.0.1 5030
```

### 3.3 Topologia em anel recomendada

Usar quatro nós em anel:

```text
10 —— 20
|       |
40 —— 30
```

Preparação recomendada em modo direto:

**Terminal N10**

```text
./OWR 127.0.0.1 5010
dj 073 10
dae 20 127.0.0.1 5020
dae 40 127.0.0.1 5040
```

**Terminal N20**

```text
./OWR 127.0.0.1 5020
dj 073 20
dae 10 127.0.0.1 5010
dae 30 127.0.0.1 5030
```

**Terminal N30**

```text
./OWR 127.0.0.1 5030
dj 073 30
dae 20 127.0.0.1 5020
dae 40 127.0.0.1 5040
```

**Terminal N40**

```text
./OWR 127.0.0.1 5040
dj 073 40
dae 10 127.0.0.1 5010
dae 30 127.0.0.1 5030
```

### 3.4 Topologia para “destino conhecido na rede mas não alcançável”

Este teste requer **modo com servidor**, porque a noção de “conhecido na rede” será confirmada via `show nodes`. Usar duas componentes desligadas dentro da mesma rede:

```text
10 —— 20      40 —— 50
```

Preparação recomendada:

**Terminal N10**

```text
./OWR 127.0.0.1 5010
j 073 10
ae 20
```

**Terminal N20**

```text
./OWR 127.0.0.1 5020
j 073 20
ae 10
```

**Terminal N40**

```text
./OWR 127.0.0.1 5040
j 073 40
ae 50
```

**Terminal N50**

```text
./OWR 127.0.0.1 5050
j 073 50
ae 40
```

Se o servidor de nós não estiver disponível, este teste deve ser marcado como **não executado** e não como conforme.

## 4. Lista de testes

---

### ID do teste

**T01**

### Nome do teste

**Ausência de rota válida antes de qualquer anúncio**

### Objetivo

Verificar que, numa topologia já formada mas sem qualquer `announce`, um nó não apresenta uma rota válida finita para um destino que ainda não se anunciou.

### Cobertura do enunciado

`announce` é o comando que torna um nó alcançável pelos restantes; `show routing` deve apresentar o estado do encaminhamento relativo a um destino e, quando está em expedição, mostrar distância e vizinho de expedição. O protocolo de encaminhamento é ativado por receção de mensagens de encaminhamento, adição de aresta e remoção de aresta; sem anúncio, não deve haver aprendizagem legítima de rota para esse destino.

### Pré-condições

* Topologia linear preparada.
* Nenhum dos nós executou `a`.

### Topologia necessária

```text
10 —— 20 —— 30 —— 40
```

### Comandos a executar

**Terminal N20**

```text
sr 10
```

**Terminal N30**

```text
sr 10
```

**Terminal N40**

```text
sr 10
```

### Resultado esperado segundo o enunciado

Antes de qualquer anúncio do nó `10`, os nós `20`, `30` e `40` não devem apresentar uma rota válida finita para `10`. O formato exato da mensagem ao utilizador pode variar, mas o programa **não deve** afirmar estado de expedição com distância finita e vizinho de expedição para `10`.

### Como verificar manualmente

* Confirmar que `sr 10` **não** mostra uma distância `1`, `2`, `3`, etc.
* Confirmar que `sr 10` **não** indica um vizinho de expedição válido para `10`.
* Se a implementação mostrar “sem rota”, “destino desconhecido”, “inacessível” ou equivalente, esse comportamento é compatível com o enunciado, desde que não exista falsa rota finita.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T02**

### Nome do teste

**Autoanúncio: distância 0 no nó que se anuncia**

### Objetivo

Verificar que o nó que executa `announce` passa a ser tratado como destino alcançável para si próprio e que `show routing` reflete esse estado com distância `0`.

### Cobertura do enunciado

O comando `announce` faz com que um nó se anuncie para ser alcançável por todos os outros nós da rede. O material de apoio diz explicitamente que o destino `t` se anuncia na rede enviando a mensagem de encaminhamento `(route,t,n)` a todos os seus vizinhos, e `show routing` deve mostrar distância e vizinho de expedição quando o estado é de expedição.

### Pré-condições

* Topologia linear preparada.
* Nenhum anúncio anterior nesta execução.

### Topologia necessária

```text
10 —— 20 —— 30 —— 40
```

### Comandos a executar

**Terminal N10**

```text
a
sr 10
```

### Resultado esperado segundo o enunciado

Depois de `a` em `N10`, o próprio nó `10` deve apresentar estado coerente de destino alcançável para `10`, com distância `0`. Como o próprio nó é o destino, não pode surgir uma distância positiva para `10`.

### Como verificar manualmente

* Confirmar em `N10` que `sr 10` mostra distância `0`.
* Confirmar que a implementação não mostra distância `1` ou superior para o próprio destino.
* Se a implementação mostrar vizinho de expedição para o próprio destino, isso deve ser analisado como divergência, porque o próprio nó não aprende a sua distância através de um vizinho.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T03**

### Nome do teste

**Primeiro salto aprende rota com distância 1**

### Objetivo

Verificar que o vizinho direto do nó anunciado aprende a rota com distância `1`.

### Cobertura do enunciado

A mensagem `ROUTE dest n<LF>` anuncia que o emissor consegue alcançar `dest` com `n` saltos. Quando um nó recebe de um vizinho `j` a mensagem `(route,t,n)` e `n + 1 < dist[t]`, atualiza `dist[t] := n + 1` e `succ[t] := j`. Logo, se `10` anuncia `ROUTE 10 0`, o seu vizinho direto deve aprender distância `1`.

### Pré-condições

* Continuação de T02.
* `N10` já executou `a`.

### Topologia necessária

```text
10 —— 20 —— 30 —— 40
```

### Comandos a executar

**Terminal N20**

```text
sr 10
```

### Resultado esperado segundo o enunciado

O nó `20` deve apresentar estado de expedição relativamente ao destino `10`, com distância `1` e vizinho de expedição `10`.

### Como verificar manualmente

* Confirmar que `sr 10` em `N20` mostra estado de expedição.
* Confirmar que a distância mostrada é `1`.
* Confirmar que o vizinho de expedição é `10`.
* Confirmar que **não** aparece distância `0`, `2` ou superior.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T04**

### Nome do teste

**Propagação multi-salto na topologia linear**

### Objetivo

Verificar que a rota anunciada por `10` se propaga ao longo da linha e que as distâncias aprendidas aumentam de acordo com o número de saltos.

### Cobertura do enunciado

O protocolo usa `ROUTE dest n<LF>` para anúncio de distâncias e, em estado estável, os vizinhos de expedição devem guiar as mensagens ao longo de caminhos mais curtos. Numa linha `10-20-30-40`, a distância a `10` tem de ser `2` em `30` e `3` em `40`.

### Pré-condições

* Continuação de T02/T03.
* `N10` já executou `a`.

### Topologia necessária

```text
10 —— 20 —— 30 —— 40
```

### Comandos a executar

**Terminal N30**

```text
sr 10
```

**Terminal N40**

```text
sr 10
```

### Resultado esperado segundo o enunciado

* Em `N30`, `sr 10` deve mostrar distância `2` e vizinho de expedição `20`.
* Em `N40`, `sr 10` deve mostrar distância `3` e vizinho de expedição `30`.

### Como verificar manualmente

* Confirmar em `N30` que a distância a `10` é `2`.
* Confirmar em `N40` que a distância a `10` é `3`.
* Confirmar que o vizinho de expedição em cada nó coincide com o próximo salto natural da linha.
* Confirmar que não existe “atalho” impossível, como `N40` aprender `10` com distância `2` nesta topologia.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T05**

### Nome do teste

**start monitor mostra mensagens ROUTE trocadas com o nó**

### Objetivo

Verificar que `start monitor` ativa a visualização das mensagens de encaminhamento e que, durante um anúncio, as mensagens `ROUTE dest n<LF>` ficam observáveis no nó monitorizado.

### Cobertura do enunciado

O enunciado define `start monitor (sm)` como ativação da monitorização de mensagens e diz que, por omissão, as mensagens de encaminhamento trocadas com o nó não são mostradas ao utilizador; com `start monitor` e `end monitor`, ativa-se e desativa-se essa visualização. O protocolo de encaminhamento inclui a mensagem `ROUTE dest n<LF>`.

### Pré-condições

* Reiniciar a topologia linear de raiz.
* Nenhum anúncio anterior nesta nova execução.

### Topologia necessária

```text
10 —— 20 —— 30 —— 40
```

### Comandos a executar

**Terminal N20**

```text
sm
```

**Terminal N10**

```text
a
```

### Resultado esperado segundo o enunciado

Com a monitorização ativa em `N20`, o utilizador deve conseguir observar mensagens de encaminhamento `ROUTE` trocadas com `N20` durante a propagação do anúncio de `10`. O formato exato do texto impresso pode variar, mas a visualização tem de corresponder a mensagens `ROUTE` efetivamente trocadas com o nó monitorizado.

### Como verificar manualmente

* Observar a consola de `N20` logo após `a` em `N10`.
* Confirmar que aparecem eventos compatíveis com `ROUTE 10 0` recebido de `10` e/ou `ROUTE 10 1` reenviado a partir de `20`, conforme a forma de apresentação escolhida pela implementação.
* Confirmar em Wireshark, se necessário, que há tráfego TCP com mensagens `ROUTE 10 n<LF>` na sessão de `N20`.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T06**

### Nome do teste

**end monitor desativa só a visualização, não o protocolo**

### Objetivo

Verificar que `end monitor` desativa a monitorização visível ao utilizador, mas não interrompe nem altera a aprendizagem de novas rotas.

### Cobertura do enunciado

`end monitor (em)` desativa a monitorização de mensagens. O enunciado não lhe atribui qualquer efeito protocolar; apenas efeito sobre a visibilidade das mensagens trocadas com o nó. O encaminhamento deve continuar a reagir aos anúncios.

### Pré-condições

* Continuação de T05.
* `N20` tem monitorização ativa.
* `N10` já se anunciou.

### Topologia necessária

```text
10 —— 20 —— 30 —— 40
```

### Comandos a executar

**Terminal N20**

```text
em
```

**Terminal N40**

```text
a
```

**Terminal N20**

```text
sr 40
```

### Resultado esperado segundo o enunciado

Depois de `em`, `N20` deve deixar de mostrar automaticamente as mensagens de encaminhamento. No entanto, o novo anúncio de `40` continua a propagar-se pela rede, e `N20` deve aprender uma rota válida para `40`. Nesta topologia linear, a distância correta de `20` a `40` é `2`, com vizinho de expedição `30`.

### Como verificar manualmente

* Confirmar que, após `em`, a consola de `N20` deixa de imprimir automaticamente as mensagens `ROUTE`.
* Confirmar depois que `sr 40` em `N20` mostra estado de expedição, distância `2` e vizinho de expedição `30`.
* O teste passa se o protocolo continua funcional apesar de a visualização ter sido desligada.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T07**

### Nome do teste

**Destino conhecido na rede mas não alcançável**

### Objetivo

Verificar a diferença entre “nó conhecido na rede” e “nó alcançável por encaminhamento”, usando o servidor de nós para confirmar que um identificador existe na rede, embora não haja caminho topológico até ele.

### Cobertura do enunciado

`show nodes` permite consultar os identificadores dos nós pertencentes à rede. O protocolo de encaminhamento usa apenas as arestas existentes e define `dist[t] = ∞` quando não é possível alcançar `t`. Logo, conhecer um identificador na rede não implica ter uma rota válida até ele.

### Pré-condições

* Servidor de nós disponível.
* Topologia em duas componentes desligadas preparada em modo com servidor.
* O nó `50` já executou `a`.

### Topologia necessária

```text
10 —— 20      40 —— 50
```

### Comandos a executar

**Terminal N10**

```text
n 073
sr 50
```

### Resultado esperado segundo o enunciado

* `n 073` deve mostrar que `50` pertence à rede `073`.
* `sr 50` em `N10` **não** deve apresentar uma rota válida finita para `50`, porque não existe caminho de `10` até `50`. O texto exato pode variar, mas não pode indicar distância finita e vizinho de expedição para `50`.

### Como verificar manualmente

* Confirmar que `50` aparece em `show nodes`.
* Confirmar que `sr 50` em `N10` não mostra uma rota finita.
* Confirmar que não existem arestas a ligar as duas componentes.
* Se a implementação mostrar explicitamente “inacessível” ou `∞`, isso é compatível com o protocolo.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T08**

### Nome do teste

**Anel: nós adjacentes ao destino aprendem distância 1**

### Objetivo

Verificar que, numa topologia em anel, os dois vizinhos imediatos do nó anunciado aprendem a rota com distância `1`.

### Cobertura do enunciado

`announce` torna o nó alcançável; `ROUTE dest n<LF>` anuncia distâncias em número de saltos; em estado estável, os vizinhos de expedição devem guiar por caminhos mais curtos. Num anel `10-20-30-40-10`, se `10` se anuncia, `20` e `40` devem ficar a `1` salto de `10`.

### Pré-condições

* Reiniciar em topologia de anel limpa.
* Nenhum anúncio anterior nesta execução.

### Topologia necessária

```text
10 —— 20
|       |
40 —— 30
```

### Comandos a executar

**Terminal N10**

```text
a
```

**Terminal N20**

```text
sr 10
```

**Terminal N40**

```text
sr 10
```

### Resultado esperado segundo o enunciado

* `N20` deve mostrar distância `1` para `10`, com vizinho de expedição `10`.
* `N40` deve mostrar distância `1` para `10`, com vizinho de expedição `10`.

### Como verificar manualmente

* Confirmar que ambos os nós adjacentes aprendem o destino `10`.
* Confirmar que a distância mostrada é exatamente `1`.
* Confirmar que não aparece distância `2` ou superior para `10` nesses nós.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T09**

### Nome do teste

**Anel: nó oposto aprende distância 2**

### Objetivo

Verificar que, numa topologia em anel, o nó oposto ao destino anunciado aprende uma rota com distância `2`.

### Cobertura do enunciado

O protocolo usa número de saltos como métrica e deve, em situação estável, conduzir por caminhos mais curtos. Num anel de quatro nós, o nó oposto ao destino fica a dois saltos desse destino.

### Pré-condições

* Continuação de T08.
* `N10` já executou `a`.

### Topologia necessária

```text
10 —— 20
|       |
40 —— 30
```

### Comandos a executar

**Terminal N30**

```text
sr 10
```

### Resultado esperado segundo o enunciado

`N30` deve mostrar estado de expedição relativamente a `10`, com distância `2`. Como existem dois caminhos mínimos de dois saltos (`30-20-10` e `30-40-10`), o vizinho de expedição pode ser `20` **ou** `40`; o que não pode acontecer é surgir distância maior que `2` ou ausência de rota válida.

### Como verificar manualmente

* Confirmar que a distância mostrada em `N30` é `2`.
* Confirmar que o vizinho de expedição é `20` ou `40`.
* Confirmar que **não** aparece distância `3` ou superior.
* Confirmar que a implementação não escolhe um caminho mais longo quando já existe um caminho de dois saltos.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T10**

### Nome do teste

**Dois caminhos possíveis: escolha do caminho mais curto em regime estável**

### Objetivo

Verificar explicitamente a propriedade de caminhos mínimos quando existem duas possibilidades, uma curta e outra mais longa.

### Cobertura do enunciado

Na ausência de acontecimentos, os vizinhos de expedição devem guiar as mensagens ao longo de caminhos mais curtos. No anel `10-20-30-40-10`, relativamente ao destino `10`, o nó `20` tem um caminho direto de `1` salto e um caminho alternativo de `3` saltos (`20-30-40-10`). O protocolo está correto apenas se escolher o caminho de `1` salto. 

### Pré-condições

* Continuação de T08/T09.
* Rede em estado estável, sem novos anúncios nem alterações de arestas.

### Topologia necessária

```text
10 —— 20
|       |
40 —— 30
```

### Comandos a executar

**Terminal N20**

```text
sr 10
```

### Resultado esperado segundo o enunciado

`N20` deve mostrar distância `1` para `10` e vizinho de expedição `10`. O nó **não deve** apresentar como vizinho de expedição o nó `30`, porque isso corresponderia a um caminho mais longo em situação estável.

### Como verificar manualmente

* Confirmar que `sr 10` em `N20` indica distância `1`.
* Confirmar que o vizinho de expedição é `10`.
* Confirmar que não existe desvio para o caminho alternativo `20-30-40-10`.
* Este teste passa apenas se o nó escolher o caminho de menor número de saltos.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

---

### ID do teste

**T11**

### Nome do teste

**Estabilidade das rotas na ausência de novos acontecimentos**

### Objetivo

Verificar que, depois de convergir, o estado de encaminhamento se mantém coerente e estável enquanto não houver novos anúncios nem alterações topológicas.

### Cobertura do enunciado

O enunciado afirma explicitamente que, na ausência dos acontecimentos que ativam o protocolo, os vizinhos de expedição devem guiar as mensagens ao longo de caminhos mais curtos. Esta propriedade deve manter-se ao longo do tempo e não apenas logo após o anúncio.

### Pré-condições

* Continuação de T08/T09/T10.
* Nenhum novo `announce`.
* Nenhuma nova aresta e nenhuma remoção de aresta.

### Topologia necessária

```text
10 —— 20
|       |
40 —— 30
```

### Comandos a executar

Aguardar alguns segundos sem executar mais comandos de encaminhamento. Depois:

**Terminal N20**

```text
sr 10
```

**Terminal N30**

```text
sr 10
```

**Terminal N40**

```text
sr 10
```

### Resultado esperado segundo o enunciado

Sem novos acontecimentos, os resultados devem manter-se estáveis e coerentes com os caminhos mínimos já aprendidos:

* `N20`: distância `1`, vizinho de expedição `10`;
* `N30`: distância `2`, vizinho de expedição `20` ou `40`;
* `N40`: distância `1`, vizinho de expedição `10`.

### Como verificar manualmente

* Confirmar que os valores não se degradam espontaneamente sem novos eventos.
* Confirmar que o nó `20` continua a preferir o caminho direto.
* Confirmar que `N30` continua com distância `2`.
* Se a implementação alterar sozinha para um caminho mais longo sem qualquer evento, o teste falha.

### Campos de registo

* [ ] Conforme
* [ ] Não conforme
* [ ] Parcial
* Observado: ________
* Divergência face ao esperado: ________
* Evidência recolhida: ________

## 5. Checklist final da bateria

* [ ] announce validado
* [ ] ROUTE validado
* [ ] show routing validado
* [ ] start monitor validado
* [ ] end monitor validado
* [ ] caminhos mínimos em regime estável validados
