# 1. Título do Projeto

**OWR — Rede Sobreposta com Encaminhamento Sem Ciclos de Expedição**

# 2. Visão Geral

O projeto OWR (*Overlay With Routing*) consiste no desenvolvimento de uma aplicação distribuída em que cada instância executada corresponde a um nó de uma rede sobreposta à Internet. Cada nó possui um identificador único dentro de uma rede lógica, pode estabelecer ligações TCP bidirecionais com outros nós dessa mesma rede e participa num protocolo distribuído de encaminhamento. A aplicação inclui ainda um mecanismo simples de troca de mensagens de chat, usado como tráfego funcional para validar a conectividade e o comportamento do encaminhamento. 

A rede sobreposta não é necessariamente completa: em condições normais, apenas alguns pares de nós estarão ligados por arestas diretas. Por esse motivo, o sistema não pode depender exclusivamente de comunicação ponto-a-ponto direta. Em vez disso, cada nó tem de manter informação de encaminhamento que lhe permita decidir para que vizinho deve expedir uma mensagem quando o destino não é seu vizinho imediato. O objetivo é garantir comunicação eficiente entre quaisquer dois nós alcançáveis da rede, sem recorrer a difusão cega e sem introduzir ciclos transitórios de expedição.

A aplicação interage também com um servidor de nós externo, disponibilizado pelo corpo docente, que mantém uma base de dados dinâmica com o mapeamento entre identificadores de nós e respetivos contactos de rede, compostos por endereço IP e porto TCP. Essa interação é feita por UDP e suporta operações de registo, remoção de registo, listagem de nós de uma rede e obtenção do contacto de um nó específico.

Este documento descreve a base técnica do projeto, clarifica o problema que a aplicação resolve, organiza os seus componentes principais e estabelece um referencial sólido para desenvolvimento, integração, depuração e validação funcional.

# 3. Contexto e Motivação

Uma rede sobreposta é uma rede lógica construída sobre uma infraestrutura de comunicação já existente. Neste projeto, essa infraestrutura subjacente é a Internet, enquanto a rede lógica é formada por instâncias da aplicação OWR. Cada nó é identificado por um identificador da rede sobreposta e as suas ligações são materializadas por sessões TCP entre pares de nós. Assim, a topologia lógica relevante para o projeto não é a topologia IP da Internet, mas sim a topologia das arestas estabelecidas entre processos OWR.

Quando a rede sobreposta é incompleta, um nó pode necessitar de comunicar com outro sem existir entre ambos uma aresta direta. Isso introduz a necessidade de encaminhamento distribuído: cada nó precisa de saber qual o vizinho que mais aproxima uma mensagem do seu destino. Sem esse mecanismo, a utilidade da rede ficaria limitada a pares diretamente ligados. 

Uma solução simples seria recorrer a *flooding*, isto é, difundir cada mensagem por todas as arestas disponíveis. Contudo, o próprio enunciado afasta essa opção por desperdício de recursos de transmissão, redundância de tráfego e ausência de controlo eficiente do caminho seguido pelas mensagens. O projeto privilegia, por isso, o envio de cada mensagem ao longo de um único caminho. 

O critério de seleção escolhido é o caminho mais curto medido em número de saltos, ou seja, em número de arestas da rede sobreposta. Essa escolha simplifica a métrica de encaminhamento, torna o comportamento previsível e permite que cada nó mantenha uma estimativa objetiva da proximidade a cada destino anunciado.

No entanto, procurar caminhos curtos não é suficiente. Em protocolos distribuídos, alterações topológicas podem introduzir inconsistências temporárias entre nós. Se essas inconsistências forem tratadas de forma ingênua, um nó pode mudar o seu sucessor antes de os nós a jusante estarem preparados, originando ciclos de expedição temporários. Nesses ciclos, as mensagens continuam a circular sem chegar ao destino. O projeto impõe explicitamente que isso nunca aconteça: em nenhum instante os vizinhos de expedição podem formar um ciclo. Esse requisito é central e condiciona toda a lógica do protocolo de encaminhamento.

# 4. Objetivos do Projeto

## Objetivo geral

Desenvolver uma aplicação OWR capaz de participar numa rede sobreposta, gerir ligações com outros nós, interagir com o servidor de nós, encaminhar mensagens por caminhos mais curtos em número de saltos e garantir ausência de ciclos de expedição durante todo o funcionamento do protocolo.

## Objetivos específicos

* Permitir que um nó entre e saia de uma rede lógica identificada por `net`.
* Garantir registo e remoção de registo do contacto do nó no servidor de nós quando a operação usa o modo com servidor.
* Permitir a descoberta dos identificadores dos nós pertencentes a uma rede.
* Permitir obter o contacto de um nó remoto para criação de arestas.
* Estabelecer e remover arestas TCP entre pares de nós da mesma rede.
* Implementar a mensagem topológica `NEIGHBOR id` para identificação mútua entre vizinhos.
* Implementar o protocolo distribuído de encaminhamento baseado nas mensagens `ROUTE`, `COORD` e `UNCOORD`.
* Reagir automaticamente a anúncio de destino, adição de aresta e remoção ou falha de aresta.
* Encaminhar mensagens de chat entre nós não diretamente ligados.
* Disponibilizar monitorização das mensagens de encaminhamento para depuração e validação.
* Suportar também um modo de operação sem servidor de nós, através de `direct join` e `direct add edge`.

# 5. Escopo do Sistema

Fazem parte do escopo deste projeto todos os mecanismos necessários para que uma instância da aplicação funcione como nó completo da rede OWR: interface de linha de comandos, sockets UDP e TCP, integração com o servidor de nós, gestão de vizinhos, protocolo topológico, protocolo de encaminhamento sem ciclos de expedição, envio de mensagens de chat e monitorização das mensagens de encaminhamento.

Também está explicitamente dentro do escopo a interoperabilidade entre implementações distintas, desde que respeitem os formatos de mensagem definidos. O enunciado recomenda inclusive testar interoperação entre grupos diferentes, o que implica que a implementação não deve depender de comportamentos ad hoc nem de formatos não especificados. 

Não são prioridade nesta fase aspetos como interface gráfica, persistência em disco, autenticação, cifragem, tolerância a intrusão, otimizações avançadas de desempenho, balanceamento de carga, encaminhamento multipercurso ou mecanismos sofisticados de recuperação de estado após reinício. O foco é funcional, protocolar e laboratorial.

# 6. Arquitetura Geral

A arquitetura lógica do sistema assenta em cinco elementos principais.

O primeiro elemento é a própria aplicação OWR, executada localmente e invocada como `OWR IP TCP regIP regUDP`. Os parâmetros `IP` e `TCP` definem o contacto público do nó no contexto da rede sobreposta; `regIP` e `regUDP` identificam o servidor de nós, que por omissão usa o endereço `193.136.138.142` e porto `59000`, sendo indicado no material de apoio que, no laboratório, o endereço IP do servidor é `192.168.1.1`.

O segundo elemento é o servidor de nós. Este componente externo gere uma base de dados dinâmica com registos do tipo `<net> <id> <IP> <TCP>` e responde a pedidos UDP de registo, remoção de registo, listagem de nós e obtenção de contactos. O servidor não encaminha mensagens entre nós; a sua função é de apoio à descoberta e gestão de participação.

O terceiro elemento é a malha de ligações TCP entre nós. Cada aresta da rede sobreposta corresponde a uma sessão TCP entre dois nós. A comunicação numa aresta é bidirecional e existe, no máximo, uma aresta por par de nós. Sobre essas sessões TCP circulam tanto mensagens topológicas (`NEIGHBOR`) como mensagens de encaminhamento (`ROUTE`, `COORD`, `UNCOORD`) e mensagens de aplicação (`CHAT`).

O quarto elemento é a interface de utilizador em linha de comandos. A CLI permite ao utilizador provocar os eventos principais do sistema: adesão à rede, saída, criação ou remoção de arestas, anúncio de destino, inspeção do estado de encaminhamento e envio de mensagens. A interface não é apenas um detalhe de operação; ela é o principal disparador dos fluxos funcionais do projeto. 

O quinto elemento é a lógica interna do nó, que pode ser vista como a cooperação entre vários subsistemas: interpretação de comandos, gestão de sockets, gestão de vizinhos, manutenção do estado de encaminhamento, encaminhamento de mensagens de chat e monitorização. Embora a implementação concreta possa variar, estes subsistemas devem manter responsabilidades bem separadas para evitar inconsistências de estado e facilitar a depuração.

# 7. Componentes Principais

## 7.1 Interface de linha de comandos

A interface de linha de comandos é o ponto de controlo local da aplicação. Deve aceitar os comandos definidos na especificação, validar argumentos, impedir execuções fora de contexto e acionar a lógica correspondente. Também deve apresentar resultados compreensíveis ao utilizador, como a lista de nós conhecidos, a lista de vizinhos, o estado de encaminhamento para um destino e mensagens de erro quando uma operação não pode ser executada. 

## 7.2 Comunicação com servidor de nós

Este componente implementa o protocolo UDP com o servidor externo. Deve ser responsável por gerar `tid`, formatar mensagens `NODES`, `CONTACT` e `REG`, enviar pedidos, receber respostas, verificar consistência mínima da transação e devolver resultados normalizados aos restantes módulos. É também o componente que concretiza o modo de operação com servidor.

## 7.3 Protocolo de rede sobreposta

Este componente gere o estabelecimento lógico da vizinhança entre nós já ligados por TCP. A ligação física é a sessão TCP; a confirmação lógica do vizinho é feita através da mensagem `NEIGHBOR id`. O componente deve tratar tanto ligações de saída, iniciadas localmente, como ligações de entrada, aceites pelo servidor TCP do nó. Só após identificação válida de ambas as partes uma ligação deve ser promovida a aresta funcional da rede sobreposta.

## 7.4 Protocolo de encaminhamento

Este é o núcleo do projeto. O componente de encaminhamento mantém o estado por destino, processa eventos topológicos e mensagens de controlo, decide quando um nó está em estado de expedição ou de coordenação e define o vizinho de expedição a usar para cada destino alcançável. Deve implementar a métrica de menor número de saltos e, simultaneamente, garantir ausência de ciclos transitórios de expedição.

## 7.5 Protocolo de chat

O protocolo de chat produz o tráfego de dados útil da aplicação. O componente deve construir mensagens `CHAT origin dest chat`, entregá-las localmente quando o nó atual é o destino e, caso contrário, reenviá-las para o sucessor apropriado segundo o estado de encaminhamento disponível. A mensagem textual tem limite máximo de 128 caracteres.

## 7.6 Monitorização e debug

Este componente controla a apresentação opcional das mensagens de encaminhamento ao utilizador. Quando a monitorização está ativa, as mensagens `ROUTE`, `COORD` e `UNCOORD` trocadas com vizinhos devem ser mostradas de forma útil para inspeção. O objetivo não é alterar o protocolo, mas tornar observável a sua execução. O enunciado também recomenda ferramentas auxiliares de depuração como `gdb`, `nc` e Wireshark, o que reforça a importância deste subsistema.

# 8. Modelo de Operação do Nó

O ciclo de vida de um nó pode ser descrito em etapas sucessivas.

**Arranque da aplicação.** O processo OWR é iniciado com o seu contacto local e, opcionalmente, com o contacto do servidor de nós. Nesta fase, o nó ainda não participa em nenhuma rede lógica e não deve assumir que possui identificador válido nem vizinhos ativos. 

**Entrada na rede.** Ao executar `join net id` ou `direct join net id`, o nó passa a associar-se logicamente à rede `net` com o identificador `id`. No modo com servidor, a entrada implica registo do seu contacto na base de dados do servidor. No modo direto, a participação existe apenas localmente e entre nós que se conheçam explicitamente. 

**Registo no servidor.** Quando aplicável, o nó envia `REG tid 0 net id IP TCP` e espera confirmação. O registo só deve ser considerado concluído quando o servidor confirmar com `op=1`. Se houver erro ou base de dados cheia, a entrada não deve ser dada como bem-sucedida.

**Estabelecimento de vizinhanças.** Para criar uma aresta com `add edge id`, o nó obtém primeiro o contacto do nó pretendido através de `CONTACT` e, depois, estabelece uma ligação TCP para esse contacto. Uma vez criada a sessão, comunica o seu identificador por `NEIGHBOR id`. O mesmo raciocínio se aplica ao modo `direct add edge`, mas sem consulta ao servidor.

**Anúncio como destino alcançável.** Quando o utilizador executa `announce`, o nó anuncia-se como destino na rede. Em termos de encaminhamento, isto significa iniciar a difusão de informação `ROUTE` com distância zero para os seus vizinhos, desencadeando a propagação distribuída das melhores distâncias. O material teórico descreve esse arranque como envio de `(route, t, 0)` pelo destino a todos os seus vizinhos.

**Encaminhamento de mensagens.** Depois de existir estado de encaminhamento convergente para um destino, o nó pode expedir mensagens de chat para o sucessor apropriado. Cada nó intermédio reencaminha a mensagem com base no seu `succ[dest]`. Se o nó atual for o destino, a mensagem é entregue localmente ao utilizador. 

**Saída da rede.** Ao executar `leave`, o nó deixa de participar na rede: remove as arestas que sobre ele incidiam e, no modo com servidor, remove também o seu registo do servidor. Esta operação deve limpar adequadamente o estado associado a vizinhos e encaminhamento.

**Fecho da aplicação.** O comando `exit` termina o processo. Se o nó ainda estiver numa rede, o comando implica primeiro o comportamento de `leave` e só depois o encerramento do processo. 

# 9. Interface de Utilizador e Comandos

| Comando                         | Abreviatura | Descrição                                                                                                                            | Efeito esperado                                                                                                                        |
| ------------------------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------- |
| `join net id`                   | `j`         | Participação de um nó com identificador `id` na rede `net`, garantindo unicidade do identificador e registo do contacto no servidor. | O nó entra em modo participante, associa-se à rede, fixa `net` e `id` locais e regista `IP/TCP` no servidor de nós.                    |
| `show nodes net`                | `n`         | Consulta ao servidor para obter os identificadores de todos os nós pertencentes à rede `net`.                                        | A aplicação apresenta a lista de identificadores devolvida pelo servidor ou uma indicação de erro.                                     |
| `leave`                         | `l`         | Saída da rede atual.                                                                                                                 | O nó remove as arestas ativas, deixa de participar na rede e, quando aplicável, remove o seu registo do servidor.                      |
| `exit`                          | `x`         | Fecho da aplicação.                                                                                                                  | O processo termina; se ainda estiver numa rede, executa primeiro o equivalente a `leave`.                                              |
| `add edge id`                   | `ae`        | Estabelecimento de uma aresta com o nó `id` da mesma rede.                                                                           | O nó obtém o contacto remoto, abre sessão TCP, troca identificação por `NEIGHBOR` e passa a ter esse nó como vizinho.                  |
| `remove edge id`                | `re`        | Remoção da aresta com o nó `id`.                                                                                                     | A ligação TCP correspondente é encerrada e o protocolo de encaminhamento reage automaticamente à alteração topológica.                 |
| `show neighbors`                | `sg`        | Apresentação da lista de vizinhos do nó.                                                                                             | A aplicação mostra os nós atualmente ligados por arestas ativas.                                                                       |
| `announce`                      | `a`         | Anúncio do nó como destino alcançável na rede.                                                                                       | O nó inicia a propagação de informação de rota para si próprio, permitindo que outros nós passem a conhecê-lo como destino.            |
| `show routing dest`             | `sr`        | Consulta do estado de encaminhamento relativo ao destino `dest`.                                                                     | A aplicação mostra se o nó está em expedição ou coordenação; se estiver em expedição, mostra também distância e vizinho de expedição.  |
| `start monitor`                 | `sm`        | Ativação da monitorização das mensagens de encaminhamento.                                                                           | Passam a ser mostradas ao utilizador as mensagens de encaminhamento trocadas com o nó.                                                 |
| `end monitor`                   | `em`        | Desativação da monitorização das mensagens de encaminhamento.                                                                        | As mensagens de encaminhamento deixam de ser mostradas automaticamente ao utilizador.                                                  |
| `message dest message`          | `m`         | Envio de uma mensagem de chat para o destino `dest`.                                                                                 | A aplicação encapsula a mensagem em `CHAT origin dest chat` e entrega-a localmente ou reencaminha-a segundo o protocolo.               |
| `direct join net id`            | `dj`        | Entrada do nó na rede `net` sem uso do servidor de nós.                                                                              | O nó passa a participar localmente na rede, mas sem registo central.                                                                   |
| `direct add edge id idIP idTCP` | `dae`       | Criação manual de aresta com o nó `id`, usando diretamente o seu contacto IP/TCP.                                                    | A aplicação abre a sessão TCP para o contacto indicado e estabelece a vizinhança sem consulta ao servidor.                             |

# 10. Protocolos de Comunicação

## 10.1 Comunicação com o servidor de nós

O objetivo do protocolo com o servidor de nós é suportar descoberta e gestão de participação. O servidor mantém a correspondência entre `(net, id)` e o contacto `(IP, TCP)` de cada nó participante. Sem este serviço, a criação de arestas entre nós desconhecidos exigiria configuração manual prévia.

O protocolo com o servidor é suportado em UDP. A escolha é coerente com operações curtas de pedido-resposta, sem necessidade de sessão persistente. Cada transação é identificada por um campo `tid` entre `000` e `999`, escolhido aleatoriamente por quem inicia a transação e preservado na resposta. O campo `op` identifica o tipo lógico da mensagem dentro de cada família protocolar e assume valores entre `0` e `9`.

### Mensagens `NODES`

Formato geral:

```text
NODES tid op net
id1
id2
...
```

* `op = 0`: pedido de listagem de todos os nós da rede `net`.
* `op = 1`: resposta com a lista de identificadores, um por linha.
* Outros valores de `op`: condição de erro. 

### Mensagens `CONTACT`

Formato geral:

```text
CONTACT tid op net id IP TCP
```

* `op = 0`: pedido do contacto do nó `id` da rede `net`; os campos `IP` e `TCP` seguem vazios no pedido.
* `op = 1`: resposta com o contacto do nó pedido.
* `op = 2`: o nó não está registado.
* Outros valores de `op`: condição de erro.

### Mensagens `REG`

Formato geral:

```text
REG tid op net id IP TCP
```

* `op = 0`: pedido de registo do nó `id` na rede `net`, incluindo `IP` e `TCP` do nó.
* `op = 1`: confirmação de que o nó ficou registado; os campos `IP` e `TCP` vêm vazios.
* `op = 2`: base de dados cheia; no resumo da especificação operacional esta resposta surge como `REG tid op`.
* `op = 3`: pedido de remoção do registo do nó `id` na rede `net`.
* `op = 4`: confirmação da ausência de registo do nó `id`; operacionalmente corresponde à confirmação da remoção.
* Outros valores de `op`: condição de erro.

### Significado dos campos

* `tid`: identificador da transação, usado para correlacionar pedido e resposta. 
* `op`: código do tipo lógico de operação ou resposta. 
* `net`: identificador da rede lógica, com três dígitos. 
* `id`: identificador do nó dentro da rede, com dois dígitos. 
* `IP`: endereço IP do nó. 
* `TCP`: porto TCP servidor do nó. 

### Comportamento esperado em sucesso e erro

Em sucesso, a operação deve atualizar o estado local apenas depois de validada a resposta adequada do servidor. Em erro, timeout ou resposta mal formatada, a aplicação não deve avançar como se a operação tivesse sido concluída. Por exemplo, um `join` não deve ser dado como válido sem confirmação de registo, e um `add edge` não deve prosseguir sem contacto remoto consistente.

## 10.2 Protocolo de rede sobreposta

O protocolo de rede sobreposta contém a mensagem:

```text
NEIGHBOR id
```

Esta mensagem é enviada sobre TCP e serve para um nó dar a conhecer a outro o seu identificador na rede. A sessão TCP estabelece a conectividade; a mensagem `NEIGHBOR` estabelece a identidade lógica do par remoto no contexto da rede sobreposta. A aresta só deve ser considerada operacional quando essa identificação tiver sido recebida e validada.

## 10.3 Protocolo de encaminhamento

O protocolo de encaminhamento usa três mensagens TCP.

```text
ROUTE dest n
COORD dest
UNCOORD dest
```

* `ROUTE dest n`: o nó emissor anuncia ao vizinho que consegue alcançar o destino `dest` com distância `n` saltos.
* `COORD dest`: o emissor informa um vizinho de que entrou em estado de coordenação relativamente ao destino `dest`. 
* `UNCOORD dest`: o emissor informa um vizinho, que antes tinha sido envolvido numa coordenação, de que já não depende desse vizinho para alcançar o destino `dest`. 

O documento teórico de apoio descreve a mensagem de fim de coordenação como `(exped, t)`, enquanto a especificação operacional do projeto usa a forma textual `UNCOORD dest`. Para efeitos de implementação do projeto, `UNCOORD` deve ser entendida como a mensagem concreta que representa o fim da dependência de coordenação para um destino.

## 10.4 Protocolo de chat

O protocolo de chat contém a mensagem:

```text
CHAT origin dest chat
```

* `origin`: identificador do nó origem.
* `dest`: identificador do nó destino.
* `chat`: conteúdo textual da mensagem.

A mensagem é transportada sobre TCP e a sequência `chat` tem um máximo de 128 caracteres. A limitação de tamanho deve ser respeitada na validação local e na interpretação de mensagens recebidas.

# 11. Encaminhamento Sem Ciclos de Expedição

A finalidade do protocolo de encaminhamento é permitir que cada nó descubra, para cada destino anunciado, um vizinho de expedição que o aproxime do destino por um caminho mais curto em número de saltos, sem que em nenhum instante se formem ciclos de expedição. Este objetivo é mais exigente do que simplesmente manter menores distâncias: o protocolo também coordena a ordem em que sucessores podem mudar quando a topologia se altera.

O **vizinho de expedição** é o vizinho para o qual o nó reencaminha mensagens destinadas a um certo destino. A **distância** é a estimativa do número de arestas entre o nó atual e esse destino. Em condições estabilizadas, o nó prefere o vizinho que lhe oferece a menor distância conhecida.

Relativamente a cada destino `t`, o nó pode estar em dois estados:

* **Estado de expedição (`state[t] = 0`)**: o nó possui, ou não, uma rota válida, mas está fora de coordenação. Se tiver rota válida, pode anunciar a sua distância aos vizinhos e usar `succ[t]` para encaminhar tráfego.
* **Estado de coordenação (`state[t] = 1`)**: o nó suspende a confiança imediata no sucessor anterior e coordena com os vizinhos a transição segura para um novo estado de expedição, evitando ciclos transitórios. Durante esta fase, o par `dist[t]` e `succ[t]` pode ser temporariamente invalidado.

As estruturas de estado por destino são as seguintes:

* `dist[t]`: estimativa da distância ao destino `t`; vale `∞` se `t` não for alcançável.
* `succ[t]`: identificador do vizinho de expedição para `t`; vale `-1` se não houver sucessor. 
* `state[t]`: estado relativo a `t`, com `0` para expedição e `1` para coordenação. 
* `succ_coord[t]`: vizinho de expedição que causou a entrada em coordenação; se a transição se dever à falha da ligação com o sucessor, vale `-1`. 
* `coord[t,j]`: estado da coordenação em curso com o vizinho `j`; vale `1` se ainda se aguarda conclusão dessa coordenação e `0` quando ela terminou.

## Reação à receção de `ROUTE`

Quando o nó recebe de um vizinho `j` a mensagem `ROUTE t n`, compara `n + 1` com a sua distância atual `dist[t]`. Se `n + 1` for estritamente melhor, atualiza `dist[t] := n + 1` e `succ[t] := j`. Se estiver em estado de expedição, propaga então a nova distância aos seus vizinhos. Esta regra permite que a melhor rota conhecida se difunda pela rede. 

## Reação à receção de `COORD`

Quando o nó recebe `COORD t` de um vizinho `j`, o comportamento depende do contexto:

1. Se já estiver em coordenação para `t`, responde com a mensagem de fim de coordenação equivalente, isto é, `UNCOORD t` no protocolo do projeto.
2. Se estiver em expedição e `j` **não** for o seu sucessor atual para `t`, então pode responder imediatamente com a sua informação de rota atual e com `UNCOORD t`, porque a sua escolha de sucessor não depende de `j`.
3. Se estiver em expedição e `j` **for** o seu sucessor atual, então a situação é crítica: o nó dependia precisamente de quem agora entrou em coordenação. Nesse caso, o nó também entra em coordenação, memoriza `succ_coord[t]`, invalida a sua rota local (`dist[t] := ∞`, `succ[t] := -1`) e envia mensagens de coordenação aos seus vizinhos, marcando `coord[t,k] := 1` para cada vizinho `k`. 

## Reação à receção de `UNCOORD`

Quando o nó recebe `UNCOORD t` de um vizinho `j`, se estiver em coordenação marca `coord[t,j] := 0`. Quando `coord[t,k] = 0` para todos os vizinhos `k`, a coordenação termina: o nó regressa ao estado de expedição. Se, entretanto, já tiver uma distância finita para `t`, anuncia essa distância aos vizinhos; além disso, se `succ_coord[t] != -1`, envia `UNCOORD t` ao vizinho guardado em `succ_coord[t]`, informando-o de que já não depende dele para alcançar `t`. 

## Reação à adição de aresta

Quando um nó ganha um novo vizinho por adição de aresta, o protocolo reage da seguinte forma:

* se o nó estiver em estado de expedição relativamente a um destino `t`, deve enviar ao novo vizinho uma mensagem de rota com a distância conhecida para `t`; 
* se estiver em estado de coordenação, deve registar que o seu regresso ao estado de expedição não depende desse novo vizinho. Em termos práticos, esse vizinho não deve bloquear a conclusão de uma coordenação que já estava em curso. 

## Reação à remoção ou falha de aresta

Quando um nó perde um vizinho por remoção ou falha de ligação:

* se estiver em estado de expedição e o vizinho perdido for `succ[t]`, o nó entra em coordenação e segue o procedimento associado a esse estado; 
* se já estiver em coordenação, deve apenas registar que a conclusão da coordenação já não depende desse vizinho perdido. 

## Porque esta coordenação impede ciclos transitórios

O mecanismo de coordenação impede que um nó passe prematuramente a encaminhar por um vizinho que ainda dependia dele, direta ou indiretamente. Em vez de atualizar sucessores de forma imediata após uma falha, o protocolo força uma propagação ordenada da coordenação ao longo da cadeia de dependências. Os nós mais a jusante deixam primeiro de depender do caminho antigo, confirmam essa independência com `UNCOORD`, e só depois os nós a montante regressam ao estado de expedição e anunciam novas rotas. O exemplo do material de apoio para a falha da aresta `12-30` mostra precisamente esta atualização coordenada: antes de `30` poder passar a usar `15`, os nós dependentes a jusante alteram primeiro o seu comportamento, evitando o ciclo transitório `30 ↔ 15`.

# 12. Fluxos Funcionais do Sistema

## 12.1 Um nó entra na rede

1. O utilizador inicia a aplicação com `OWR IP TCP regIP regUDP`.
2. O utilizador executa `join net id`.
3. A aplicação valida o formato de `net` e `id`.
4. A aplicação interage com o servidor de nós para registar o contacto do nó.
5. Em caso de confirmação, o nó passa a considerar-se participante da rede `net` com identificador `id`.
6. O estado topológico e de encaminhamento inicia-se sem vizinhos até que arestas sejam criadas.

## 12.2 Um nó descobre outros nós

1. O utilizador executa `show nodes net`.
2. A aplicação envia `NODES tid 0 net` ao servidor.
3. O servidor responde com `NODES tid 1 net` seguido da lista de identificadores.
4. A aplicação apresenta esses identificadores ao utilizador.
5. A listagem pode servir de base para posterior `add edge`.

## 12.3 Um nó cria uma aresta

1. O utilizador executa `add edge id`.
2. A aplicação envia `CONTACT tid 0 net id` ao servidor para obter `IP` e `TCP` do nó alvo.
3. Recebido o contacto, a aplicação abre uma sessão TCP para esse nó.
4. Após a ligação, envia `NEIGHBOR <meu_id>`.
5. A ligação passa a representar uma aresta bidirecional da rede sobreposta.
6. O protocolo de encaminhamento deve ser notificado da adição do vizinho.

## 12.4 Um nó se anuncia como destino

1. O utilizador executa `announce`.
2. O nó considera-se destino alcançável para o seu próprio identificador.
3. O nó envia aos vizinhos a informação equivalente a `ROUTE meu_id 0`.
4. Os vizinhos que ainda não tinham melhor rota atualizam a sua distância para `1` e propagam `ROUTE` com a sua nova distância.
5. A informação propaga-se pela rede até que os nós alcancem estabilidade para esse destino.

## 12.5 Uma mensagem de chat é enviada entre dois nós não diretamente ligados

1. O utilizador no nó origem executa `message dest texto`.
2. A aplicação constrói `CHAT origin dest texto`.
3. Se `dest` for o próprio nó, a mensagem é entregue localmente.
4. Caso contrário, o nó consulta `succ[dest]`.
5. A mensagem é enviada pela sessão TCP correspondente ao vizinho de expedição.
6. Cada nó intermédio repete o processo até a mensagem chegar ao destino.
7. O destino apresenta o conteúdo ao utilizador.

## 12.6 Uma aresta é removida e o encaminhamento se reorganiza

1. O utilizador executa `remove edge id`, ou a sessão TCP falha abruptamente.
2. O nó remove o vizinho da sua estrutura topológica.
3. Para cada destino cujo sucessor era esse vizinho, o nó entra em coordenação.
4. O nó envia `COORD dest` aos vizinhos relevantes.
5. Os nós dependentes do caminho antigo entram, por sua vez, em coordenação de forma encadeada.
6. Nós que não dependem desse caminho respondem com informação de rota e `UNCOORD`.
7. À medida que as coordenações se fecham, os nós regressam ao estado de expedição e anunciam as suas novas rotas.
8. O sistema estabiliza novamente sem ter criado ciclos transitórios de expedição.

# 13. Funcionalidades Necessárias do Projeto

## 13.1 Gestão de rede

**Entrada numa rede.**
Deve permitir ao nó associar-se a uma rede lógica com `join` ou `direct join`. É necessária porque toda a semântica de identificadores, arestas e mensagens depende de o nó conhecer a sua rede e o seu identificador. O comportamento mínimo é fixar `net` e `id` locais e, no modo com servidor, registar com sucesso o contacto do nó. 

**Saída de rede.**
Deve permitir ao nó abandonar a rede de forma controlada. É necessária para libertar recursos, desfazer arestas e manter coerência do estado global observado pelos restantes nós. O comportamento mínimo é remover todas as arestas e, quando aplicável, remover o registo do servidor.

**Descoberta de nós.**
Deve permitir consultar os identificadores atualmente registados numa rede. É necessária para apoiar a criação de arestas e a observabilidade do sistema. O comportamento mínimo é enviar `NODES` e apresentar a lista devolvida. 

## 13.2 Gestão de vizinhos

**Criação de arestas.**
Deve permitir estabelecer uma sessão TCP com outro nó e convertê-la numa aresta lógica. É necessária para formar a topologia da rede sobreposta. O comportamento mínimo é obter o contacto remoto, ligar por TCP, trocar `NEIGHBOR` e atualizar a estrutura de vizinhos.

**Remoção de arestas.**
Deve permitir terminar uma ligação com um vizinho. É necessária para controlo topológico, teste do protocolo e reação a alterações. O comportamento mínimo é encerrar a sessão, remover o vizinho das estruturas locais e notificar o encaminhamento.

**Visualização de vizinhos.**
Deve listar os vizinhos atualmente ativos. É necessária para inspeção e depuração. O comportamento mínimo é mostrar cada vizinho com o identificador conhecido e, idealmente, o contexto da ligação correspondente. 

## 13.3 Encaminhamento

**Anúncio de destino.**
Deve permitir que um nó se anuncie como alcançável. É necessária para iniciar a construção de rotas na rede. O comportamento mínimo é gerar `ROUTE` com distância zero para os vizinhos.

**Cálculo e atualização de rotas.**
Deve manter `dist`, `succ` e `state` por destino. É necessária porque o encaminhamento depende de informação local coerente. O comportamento mínimo é reagir a `ROUTE`, `COORD`, `UNCOORD`, adição de aresta e remoção ou falha de aresta.

**Garantia de ausência de ciclos transitórios.**
Deve coordenar mudanças de sucessor quando ocorrem falhas ou remoções. É necessária porque a simples troca imediata de sucessor pode criar loops. O comportamento mínimo é entrar em coordenação quando se perde o sucessor, propagar `COORD` e só regressar a expedição depois de receber as confirmações adequadas.

**Inspeção do estado de encaminhamento.**
Deve permitir consultar o estado relativo a um destino. É necessária para validação manual e oral do projeto. O comportamento mínimo é mostrar se o nó está em expedição ou coordenação e, no primeiro caso, indicar distância e sucessor. 

## 13.4 Monitorização

**Ativação e desativação de monitorização.**
Deve permitir tornar visíveis as mensagens de encaminhamento. É necessária para depurar o protocolo distribuído, que é difícil de observar apenas pelo estado final. O comportamento mínimo é imprimir `ROUTE`, `COORD` e `UNCOORD` recebidas e enviadas quando a monitorização estiver ativa. 

## 13.5 Troca de mensagens

**Envio de mensagens de chat.**
Deve permitir enviar texto entre nós com base no encaminhamento disponível. É necessária para demonstrar utilidade funcional da rede criada. O comportamento mínimo é construir e interpretar `CHAT origin dest chat`, respeitando o limite de 128 caracteres e encaminhando a mensagem até ao destino.

## 13.6 Operação sem servidor

**Participação direta.**
Deve suportar `direct join` e `direct add edge`. É necessária para permitir testes ou funcionamento em cenários sem o servidor de nós. O comportamento mínimo é permitir associar o nó a uma rede e criar vizinhanças manualmente com base em contactos fornecidos pelo utilizador. 

# 14. Requisitos Funcionais

1. A aplicação deve ser invocável com `OWR IP TCP regIP regUDP`. 
2. A aplicação deve suportar os comandos `join`, `show nodes`, `leave`, `exit`, `add edge`, `remove edge`, `show neighbors`, `announce`, `show routing`, `start monitor`, `end monitor`, `message`, `direct join` e `direct add edge`. 
3. O nó deve conseguir registar e remover o seu contacto no servidor de nós através de mensagens `REG`. 
4. O nó deve conseguir pedir a lista de identificadores de uma rede através de `NODES`. 
5. O nó deve conseguir obter o contacto de outro nó através de `CONTACT`. 
6. O nó deve manter um servidor TCP para aceitar novas ligações de vizinhos. 
7. O nó deve conseguir iniciar ligações TCP para criação de arestas. 
8. Cada aresta deve suportar comunicação bidirecional e existir no máximo uma aresta por par de nós. 
9. O nó deve enviar e receber a mensagem `NEIGHBOR id` para identificar logicamente vizinhos. 
10. O nó deve manter estado de encaminhamento por destino, incluindo distância, sucessor e estado de coordenação. 
11. O protocolo deve reagir a anúncio de novo destino, adição de aresta e remoção ou falha de aresta.
12. Na ausência desses acontecimentos, o encaminhamento deve privilegiar caminhos mais curtos em número de saltos.
13. Em nenhum momento podem existir ciclos de expedição. 
14. O nó deve suportar mensagens `ROUTE`, `COORD` e `UNCOORD`. 
15. O nó deve suportar mensagens `CHAT origin dest chat` com texto até 128 caracteres.
16. O nó deve encaminhar mensagens de chat para destinos não diretamente ligados quando existir rota válida. 
17. O nó deve permitir ativar e desativar a monitorização das mensagens de encaminhamento. 
18. O nó deve suportar operação sem servidor através de `direct join` e `direct add edge`. 

# 15. Requisitos Não Funcionais

**Robustez.** A aplicação deve continuar a operar corretamente perante falhas de vizinhança, remoções de arestas, mensagens inválidas e erros pontuais de comunicação, evitando corrupção de estado interno. O material de desenvolvimento explicita que clientes e servidores devem terminar graciosamente perante mensagens mal formatadas, sessão TCP fechada abruptamente e erros de chamadas de sistema. 

**Clareza da interface.** A CLI deve fornecer respostas compreensíveis, coerentes e úteis para operação e depuração. Como a interface é a principal forma de validação manual do projeto, mensagens ambíguas dificultam tanto o desenvolvimento como a demonstração funcional.

**Tratamento de erro.** Todas as operações devem validar pré-condições, argumentos e resultados de chamadas de sistema. O sistema não deve assumir sucesso implícito nem prosseguir com estado parcialmente inicializado. 

**Interoperabilidade entre implementações.** A implementação deve respeitar rigorosamente os formatos das mensagens, permitindo comunicação com nós implementados por outros grupos. O enunciado recomenda explicitamente esta forma de teste. 

**Terminação graciosa.** A aplicação deve libertar sockets, limpar estruturas e sair de forma controlada, sobretudo em `leave` e `exit`.

**Legibilidade e organização do código.** O código deve ser modular, coerente e fácil de manter, dado que o projeto envolve protocolos concorrentes e estado distribuído. O material de apoio recomenda também comentários, uso de depuradores e makefile sem erros nem avisos. 

**Compatibilidade com o ambiente de laboratório.** O projeto será testado no ambiente de desenvolvimento do laboratório. A solução deve, portanto, compilar com `make`, usar primitivas padrão de sockets e multiplexagem previstas no enunciado e evitar dependências não controladas. 

# 16. Situações de Erro e Casos Limite

**Mensagens mal formatadas.** O nó deve detetar e tratar mensagens que não respeitem o formato esperado, sem entrar em comportamento indefinido. Isto aplica-se tanto a UDP como a TCP. 

**Tentativa de criar aresta duplicada.** Como existe no máximo uma aresta entre cada par de nós, a aplicação deve recusar ou ignorar tentativas de duplicação. Manter duas ligações simultâneas para o mesmo vizinho introduziria ambiguidade topológica. 

**Tentativa de contactar nó inexistente.** Se `CONTACT` devolver `op=2`, ou se a ligação TCP falhar, `add edge` não deve criar qualquer estado parcial de vizinho. 

**Sessão TCP encerrada abruptamente.** A perda inesperada de um vizinho deve ser tratada como remoção ou falha de aresta. Se o vizinho perdido for o sucessor atual para algum destino, o nó deve entrar em coordenação para esse destino.

**Remoção de vizinho que é sucessor atual.** Este é o caso crítico clássico do protocolo. A aplicação deve invalidar a rota dependente, entrar em coordenação e evitar mudança imediata de sucessor que possa introduzir ciclo.

**Inconsistências temporárias durante atualizações.** O sistema deve aceitar que, durante coordenação, alguns nós já tenham novas distâncias enquanto outros ainda aguardam `UNCOORD`. O estado intermédio é normal; o que não pode acontecer é usar esse estado intermédio para formar ciclos de expedição.

**Uso de comandos fora de contexto.** Operações como `add edge`, `announce`, `show routing` ou `message` não devem correr como se fossem válidas quando o nó ainda não entrou em nenhuma rede. A aplicação deve responder com erro contextual claro.

**Receção de `COORD` de um vizinho que não é sucessor.** O nó deve responder adequadamente com a sua rota atual e `UNCOORD`, sem entrar desnecessariamente em coordenação.

**Adição de novo vizinho durante coordenação.** O nó não deve bloquear uma coordenação já em curso por causa de uma aresta recém-criada; deve registar que o regresso a expedição não depende desse novo vizinho. 

# 17. Considerações de Implementação

A implementação deve ser orientada a eventos e suportar múltiplas fontes de entrada em simultâneo. O enunciado indica explicitamente o uso de `select()` para multiplexagem síncrona, o que sugere um ciclo principal capaz de observar stdin, socket UDP, socket TCP de escuta e sockets TCP já associados a vizinhos. 

A gestão de sockets TCP e UDP deve ser separada conceptualmente. UDP é transacional e adequado à interação com o servidor de nós; TCP é orientado a sessão e adequado às arestas da rede sobreposta. Misturar a lógica dos dois canais tende a complicar a depuração e a introduzir erros de interpretação de mensagens.

A gestão de buffers é crítica, sobretudo em TCP. As mensagens são delimitadas por `<LF>`, pelo que uma leitura pode conter parte de uma mensagem, exatamente uma mensagem ou várias mensagens concatenadas. A implementação deve acumular dados por socket, extrair linhas completas e preservar resíduos para leituras futuras. A correção do protocolo depende diretamente desta disciplina.

A separação entre *parsing*, rede e lógica de encaminhamento deve ser mantida. Uma boa organização é: módulo de CLI para interpretar comandos do utilizador; módulo UDP para pedidos ao servidor; módulo TCP/topologia para vizinhos; módulo de encaminhamento para estado e regras; módulo de chat para entrega e reencaminhamento. Esta separação reduz acoplamento e facilita testes unitários de comportamento lógico.

As estruturas de estado do encaminhamento devem permanecer consistentes em todos os eventos. Sempre que se altera `dist[t]`, `succ[t]`, `state[t]`, `succ_coord[t]` ou `coord[t,j]`, essa alteração deve respeitar as invariantes do protocolo. Em particular, entrar em coordenação implica invalidar a rota dependente; sair de coordenação implica verificar conclusão com todos os vizinhos relevantes.

A limpeza correta de estado é obrigatória. Encerrar uma ligação deve remover o descritor do conjunto observado por `select()`, limpar buffers associados ao socket, atualizar a lista de vizinhos e desencadear a lógica de remoção de aresta. Deixar descritores órfãos, buffers antigos ou vizinhos “fantasma” é uma fonte clássica de erros difíceis de reproduzir.

A implementação deve ainda privilegiar verificações defensivas: validação de limites de tamanho, verificação de retorno das chamadas de sistema, distinção entre EOF e erro em leituras TCP, e tratamento explícito de respostas inesperadas do servidor. O projeto não exige sofisticação excessiva, mas exige comportamento correto e previsível.

# 18. Estrutura Sugerida do Projeto

Uma organização lógica possível do código pode dividir responsabilidades pelos seguintes módulos, sem impor nomes rígidos:

* **`main`**: ponto de entrada, inicialização global, ciclo principal com `select()`, encerramento e coordenação geral dos subsistemas.
* **`cli`**: leitura de comandos, validação sintática, parsing de argumentos e despacho para ações internas.
* **`udp_registry`**: construção e interpretação de mensagens `NODES`, `CONTACT` e `REG`, gestão de `tid` e operações com o servidor de nós.
* **`tcp_overlay`**: criação do servidor TCP, aceitação de ligações, ligações de saída, envio e receção de `NEIGHBOR`, manutenção da tabela de vizinhos e buffers por ligação.
* **`routing`**: estado `dist/succ/state/succ_coord/coord`, regras de reação a `ROUTE`, `COORD`, `UNCOORD`, anúncio de destino e eventos topológicos.
* **`chat`**: construção, validação, entrega local e reencaminhamento de mensagens `CHAT`.
* **`monitor`**: controlo da visibilidade das mensagens de encaminhamento e normalização dos registos de debug.
* **`utils`**: funções auxiliares comuns, formatação, validação de identificadores, gestão de erros e pequenas abstrações reutilizáveis.

Esta estrutura é apenas sugestiva. O importante é preservar a separação de responsabilidades e impedir que a lógica de protocolo fique espalhada de forma inconsistente pelo projeto.

# 19. Critérios de Sucesso do Projeto

O projeto pode ser considerado conceitualmente completo, do ponto de vista funcional, quando se verificarem cumulativamente os seguintes critérios:

* um nó consegue entrar e sair corretamente de uma rede, com e sem servidor; 
* a aplicação consegue descobrir nós e estabelecer ou remover arestas válidas;
* a lista de vizinhos reflete o estado real das sessões TCP ativas; 
* o anúncio de um destino propaga distâncias corretas em número de saltos; 
* `show routing` permite observar estados coerentes com a execução do protocolo; 
* após adição de aresta, o encaminhamento consegue beneficiar de caminhos mais curtos quando apropriado; 
* após remoção ou falha de aresta, o sistema reorganiza o encaminhamento sem criar ciclos transitórios;
* mensagens de chat chegam ao destino correto mesmo quando origem e destino não são vizinhos diretos;
* a monitorização torna observáveis as mensagens de encaminhamento relevantes; 
* a aplicação termina graciosamente e mantém bom comportamento perante erros comuns de execução. 

# 20. Glossário

**Nó** — Instância da aplicação OWR que participa numa rede sobreposta com um identificador próprio. 

**Rede sobreposta** — Rede lógica construída sobre a Internet, formada por nós OWR e por arestas correspondentes a sessões TCP entre esses nós. 

**Aresta** — Ligação lógica e bidirecional entre dois nós, implementada por uma sessão TCP. 

**Vizinho** — Nó ligado diretamente por uma aresta ao nó atual. 

**Vizinho de expedição** — Vizinho escolhido para reencaminhar mensagens destinadas a um dado destino.

**Distância** — Número estimado de saltos até um destino na rede sobreposta. 

**Coordenação** — Estado transitório do protocolo de encaminhamento usado para atualizar sucessores sem formar ciclos de expedição.

**Anúncio** — Ato de um nó se declarar alcançável como destino, iniciando a propagação de `ROUTE` para si próprio.

**Servidor de nós** — Componente externo que mantém o mapeamento entre identificadores e contactos dos nós e responde a pedidos UDP de registo e descoberta.

**Chat** — Mensagem de aplicação transportada pela rede sobreposta através do formato `CHAT origin dest chat`. 

**Encaminhamento** — Processo distribuído pelo qual cada nó escolhe o vizinho apropriado para aproximar mensagens do destino por caminhos mais curtos, sem criar ciclos transitórios.
