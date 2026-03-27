#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

#include "owr_headers.h"

// -------------------------
// Helpers
// -------------------------

// envia um pedido UDP ao servidor e espera resposta com timeout

static int udp_request_response(
    const char *server_ip,
    const char *server_port,
    const char *request,
    char *response,
    size_t response_sz,
    int timeout_sec)
{
    struct addrinfo hints, *res = NULL;
    int fd = -1;

    if (response_sz == 0)
        return -1;

    memset(&hints, 0, sizeof(hints));  // preparar pesquisa do endereço UDP do servidor
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int err = getaddrinfo(server_ip, server_port, &hints, &res);
    if (err != 0)
    {
        fprintf(stderr, "[ERRO] getaddrinfo(%s,%s): %s\n", server_ip, server_port, gai_strerror(err));
        return -1;
    }
    // criar socket UDP
    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1)
    {
        perror("[ERRO] socket(UDP)");
        freeaddrinfo(res);
        return -1;
    }
    // enviar pedido ao servidor
    ssize_t n = sendto(fd, request, strlen(request), 0, res->ai_addr, res->ai_addrlen);
    if (n == -1)
    {
        perror("[ERRO] sendto");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    // esperar resposta com timeout
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (ready == -1)
    {
        perror("[ERRO] select(UDP)");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    if (ready == 0)
    {
        fprintf(stderr, "[ERRO] Timeout UDP (%ds) a aguardar resposta do servidor.\n", timeout_sec);
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    // ler resposta recebida
    struct sockaddr_storage from;
    socklen_t fromlen = sizeof(from);
    n = recvfrom(fd, response, response_sz - 1, 0, (struct sockaddr *)&from, &fromlen);
    if (n == -1)
    {
        perror("[ERRO] recvfrom");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    response[n] = '\0';

    close(fd);
    freeaddrinfo(res);
    return (int)n;
}

// gera um tid pseudoaleatório entre 000 e 999
static int next_tid(void)
{
    static int seeded = 0;
    if (!seeded)
    {
        seeded = 1;
        srand((unsigned int)(time(NULL) ^ (unsigned int)getpid()));
    }
    return rand() % 1000; // 000..999
}

// converte id do nó para índice inteiro
static int id_to_index(const char *id)
{
    if (testa_formato_id((char *)id))
        return -1;
    return atoi(id);
}
// garante escrita completa no socket
static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        ssize_t w = write(fd, buf + off, len - off);
        if (w <= 0)
            return -1;
        off += (size_t)w;
    }
    return 0;
}
// remove \n e \r do fim da string
static void trim_trailing_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
    {
        s[n - 1] = '\0';
        n--;
    }
}

// escreve no monitor mensagens TX/RX quando o modo monitor está ligado
static void monitor_log(const INFO_NO *no, const char *dir, int fd, const char *fmt, ...)
{
    if (!no || !no->monitor_on)
        return;

    char payload[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(payload, sizeof(payload), fmt, ap);
    va_end(ap);
    trim_trailing_newline(payload);

    const char *peer_id = "??";
    int idx = neighbor_find_by_fd(no, fd);
    if (idx != -1 && no->neighbors[idx].id[0] != '\0')
        peer_id = no->neighbors[idx].id;

    printf("[MONITOR] %s fd=%d peer=%s %s\n", dir, fd, peer_id, payload);
}
// envia uma mensagem ROUTE para um fd específico
static int send_route_to_fd(const INFO_NO *no, int fd, const char *dest, int dist)
{
    char msg[64];
    int n = snprintf(msg, sizeof(msg), "ROUTE %s %d\n", dest, dist);
    if (n < 0 || (size_t)n >= sizeof(msg))
        return -1;
    if (send_all(fd, msg, (size_t)n) < 0)
        return -1;
    monitor_log(no, "TX", fd, "%s", msg);
    return 0;
}
// envia uma mensagem COORD para um fd específico
static int send_coord_to_fd(const INFO_NO *no, int fd, const char *dest)
{
    char msg[32];
    int n = snprintf(msg, sizeof(msg), "COORD %s\n", dest);
    if (n < 0 || (size_t)n >= sizeof(msg))
        return -1;
    if (send_all(fd, msg, (size_t)n) < 0)
        return -1;
    monitor_log(no, "TX", fd, "%s", msg);
    return 0;
}
// envia uma mensagem UNCOORD para um fd específico
static int send_uncoord_to_fd(const INFO_NO *no, int fd, const char *dest)
{
    char msg[32];
    int n = snprintf(msg, sizeof(msg), "UNCOORD %s\n", dest);
    if (n < 0 || (size_t)n >= sizeof(msg))
        return -1;
    if (send_all(fd, msg, (size_t)n) < 0)
        return -1;
    monitor_log(no, "TX", fd, "%s", msg);
    return 0;
}

// envia ROUTE para todos os vizinhos, exceto um fd específico
static void flood_route_except(const INFO_NO *no, const char *dest, int dist, int except_fd)
{
    for (int i = 0; i < n_max_internos; i++)
    {
        int fd = no->neighbors[i].fd;
        if (fd == -1 || fd == except_fd)
            continue;

        if (send_route_to_fd(no, fd, dest, dist) < 0)
            perror("[AVISO] write ROUTE");
    }
}

// envia uma mensagem CHAT para um fd
static int send_chat_to_fd(int fd, const char *src, const char *dest, const char *text)
{
    char msg[160];
    size_t text_len;
    int n;

    if (!text)
        return -1;

    text_len = strlen(text);
    if (text_len == 0 || text_len > CHAT_MAX_LEN)
        return -1;

    n = snprintf(msg, sizeof(msg), "CHAT %s %s %s\n", src, dest, text);
    if (n < 0 || (size_t)n >= sizeof(msg))
        return -1;
    if (send_all(fd, msg, (size_t)n) < 0)
        return -1;

    // CHAT não entra no monitor de encaminhamento
    return 0;
}

// limpa completamente uma entrada da tabela de routing
static void route_entry_clear(ROUTE_ENTRY *r)
{
    r->valid = 0;
    r->dest[0] = '\0';
    r->state = ROUTE_STATE_FORWARD;
    r->dist = ROUTE_INF;
    r->next_hop[0] = '\0';
    r->succ_coord[0] = '\0';

    for (int i = 0; i < n_max_internos; i++)
        r->coord_wait[i] = 0;
}

// devolve a rota de um destino ou cria uma nova se ainda não existir
static ROUTE_ENTRY *route_get_or_create(INFO_NO *no, const char *dest)
{
    int idx = id_to_index(dest);
    if (idx < 0 || idx >= n_max_nos)
        return NULL;

    ROUTE_ENTRY *r = &no->routing[idx];
    if (!r->valid)
    {
        route_entry_clear(r);
        r->valid = 1;
        strncpy(r->dest, dest, sizeof(r->dest) - 1);
        r->dest[sizeof(r->dest) - 1] = '\0';
    }

    return r;
}

// verifica se já chegaram todos os UNCOORD/COORD esperados
static int route_all_coord_done(const INFO_NO *no, const ROUTE_ENTRY *r)
{
    for (int i = 0; i < n_max_internos; i++)
    {
        if (no->neighbors[i].fd != -1 && r->coord_wait[i])
            return 0;
    }
    return 1;
}
// sai do estado de coordenação para uma rota
static void route_leave_coord(INFO_NO *no, ROUTE_ENTRY *r)
{
    char dest[3] = "";
    char succ_coord_id[3] = "";

    strncpy(dest, r->dest, sizeof(dest) - 1); // guardar cópias antes de limpar campos
    dest[sizeof(dest) - 1] = '\0';

    strncpy(succ_coord_id, r->succ_coord, sizeof(succ_coord_id) - 1);
    succ_coord_id[sizeof(succ_coord_id) - 1] = '\0';
 
    r->state = ROUTE_STATE_FORWARD; // voltar ao estado normal de forwarding

    for (int i = 0; i < n_max_internos; i++) // voltar ao estado normal de forwarding
        r->coord_wait[i] = 0;

    r->succ_coord[0] = '\0';

    if (r->dist < ROUTE_INF) // se ainda existe rota válida, voltar a anunciá-la
        flood_route_except(no, dest, r->dist, -1);

    if (succ_coord_id[0] != '\0') // avisar o sucessor coordenado que a coordenação terminou
    {
        int sidx = neighbor_find_by_id(no, succ_coord_id);
        if (sidx != -1 && no->neighbors[sidx].fd != -1)
            send_uncoord_to_fd(no, no->neighbors[sidx].fd, dest);
    }
}

void routing_reset(INFO_NO *no)
{ // limpa toda a tabela de routing
    for (int i = 0; i < n_max_nos; i++)
        route_entry_clear(&no->routing[i]);
}

void routing_init_self(INFO_NO *no)
{
    // Esta função só é usada quando o utilizador executa "announce".
    // Estar numa rede e ter vizinhos não significa que o nó já se anunciou como destino alcançável. 
    //Pelo enunciado, essa alcançabilidade nasce com o comando "announce" e 
    //é então difundida por mensagens ROUTE
    ROUTE_ENTRY *r = route_get_or_create(no, no->node_id);
    if (!r) 
        return;

    r->state = ROUTE_STATE_FORWARD;
    r->dist = 0;
    r->next_hop[0] = '\0';     // destino local: não há next-hop 
    r->succ_coord[0] = '\0';

    for (int i = 0; i < n_max_internos; i++)
        r->coord_wait[i] = 0;
}

// quando entra um novo vizinho, enviar-lhe as rotas já conhecidas
void routing_on_new_neighbor(INFO_NO *no, const char *neighbor_id)
{
    int nidx = neighbor_find_by_id(no, neighbor_id);
    if (nidx == -1 || no->neighbors[nidx].fd == -1)
        return;

    int fd = no->neighbors[nidx].fd;

    for (int i = 0; i < n_max_nos; i++)
    {
        ROUTE_ENTRY *r = &no->routing[i];
        if (!r->valid)
            continue;

        if (r->state == ROUTE_STATE_FORWARD)
        {
            if (r->dist < ROUTE_INF) // se a rota está normal e é válida, anunciar ao novo vizinho
                send_route_to_fd(no, fd, r->dest, r->dist);
        }
        else if (r->state == ROUTE_STATE_COORD)
        {
            // se a rota está normal e é válida, anunciar ao novo vizinho
            r->coord_wait[nidx] = 0;
        }
    }
}

// se estiver em coordenação, este vizinho novo não conta como pendente
void routing_invalidate_next_hop(INFO_NO *no, const char *neighbor_id)
{
    if (!neighbor_id || neighbor_id[0] == '\0')
        return;

    // invalida rotas cujo next hop era um vizinho que desapareceu
    int still_present = (neighbor_find_by_id(no, neighbor_id) != -1);

    for (int i = 0; i < n_max_nos; i++)
    {
        ROUTE_ENTRY *r = &no->routing[i];
        if (!r->valid)
            continue;
        // se a rota já está em coordenação, limpar vizinhos que já não existem
        if (r->state == ROUTE_STATE_COORD)
        {
            for (int k = 0; k < n_max_internos; k++)
            {
                if (no->neighbors[k].fd == -1)
                    r->coord_wait[k] = 0;
            }

            if (route_all_coord_done(no, r))
                route_leave_coord(no, r);
        }
        // se o vizinho ainda existe, não é preciso invalidar esta rota
        if (still_present)
            continue;
        // se esta rota usava esse vizinho como next hop, entrar em coordenação
        if (r->state == ROUTE_STATE_FORWARD &&
            r->next_hop[0] != '\0' &&
            strcmp(r->next_hop, neighbor_id) == 0)
        {
            printf("[ROUTING] dest=%s perdeu succ=%s -> entra em coordenação\n",
                   r->dest, neighbor_id);

            r->state = ROUTE_STATE_COORD;
            r->dist = ROUTE_INF;
            r->next_hop[0] = '\0';
            r->succ_coord[0] = '\0';

            for (int k = 0; k < n_max_internos; k++)
            {
                r->coord_wait[k] = 0;

                if (no->neighbors[k].fd != -1)
                { // pedir coordenação a todos os vizinhos ainda ativos
                    r->coord_wait[k] = 1;
                    if (send_coord_to_fd(no, no->neighbors[k].fd, r->dest) < 0)
                        perror("[AVISO] write COORD");
                }
            }
            // se não ficou ninguém pendente, sair logo da coordenação
            if (route_all_coord_done(no, r))
                route_leave_coord(no, r);
        }
    }
}
 // se não ficou ninguém pendente, sair logo da coordenação
int route_cmd(INFO_NO *no)
{
    if (!no->joined)
    {
        printf("[ERRO] Não estás em nenhuma rede.\n");
        return -1;
    }

    routing_init_self(no); // garantir que existe rota para o próprio nó
 
    // Depois do autoanúncio, difunde-se ROUTE <meu_id> 0 a todos os vizinhos.
    flood_route_except(no, no->node_id, 0, -1);

    printf("[OK] announce: nó %s anunciado na rede %s.\n", no->node_id, no->net.net_id);
    return 0;
}
// mostra a entrada de routing para um destino
void show_routing_cmd(const INFO_NO *no, const char *dest)
{
    if (!no->joined)
    {
        printf("[ERRO] Não estás em nenhuma rede.\n");
        return;
    }

    if (testa_formato_id((char *)dest))
    {
        printf("[ERRO] Uso: show routing (sr) dest   (dest=00..99)\n");
        return;
    }

    int idx = id_to_index(dest);
    if (idx < 0 || idx >= n_max_nos || !no->routing[idx].valid)
    {
        printf("Routing para destino %s: sem rota conhecida.\n", dest);
        return;
    }

    const ROUTE_ENTRY *r = &no->routing[idx];

    if (r->state == ROUTE_STATE_COORD)
    {
        printf("Routing para destino %s: estado=coordenação.\n", dest);
        return;
    }

    if (r->state == ROUTE_STATE_FORWARD)
    {
        if (r->dist >= ROUTE_INF || r->next_hop[0] == '\0') // caso especial: rota para si próprio
        {
            if (strcmp(dest, no->node_id) == 0)
                printf("Routing para destino %s: estado=expedição, distância=0, vizinho=local.\n", dest);
            else
                printf("Routing para destino %s: estado=expedição, sem vizinho de expedição.\n", dest);
            return;
        }

        printf("Routing para destino %s: estado=expedição, distância=%d, vizinho=%s.\n",
               dest, r->dist, r->next_hop);
        return;
    }

    printf("Routing para destino %s: estado desconhecido.\n", dest);
}

void start_monitor_cmd(INFO_NO *no)
{ // liga a monitorização
    no->monitor_on = 1;
    printf("[OK] monitorização ativada.\n");
}

void end_monitor_cmd(INFO_NO *no)
{ // desliga a monitorização
    no->monitor_on = 0;
    printf("[OK] monitorização desativada.\n");
}
// envia uma mensagem CHAT para um destino
int message_cmd(INFO_NO *no, const char *dest, const char *message)
{
    if (!no->joined)
    {
        printf("[ERRO] Não estás em nenhuma rede.\n");
        return -1;
    }

    if (testa_formato_id((char *)dest))
    {
        printf("[ERRO] Uso: message (m) dest texto   (dest=00..99)\n");
        return -1;
    }

    if (!message)
        message = "";
    // ignorar espaços no início
    while (*message == ' ' || *message == '\t')
        message++;

    if (*message == '\0')
    {
        printf("[ERRO] A mensagem não pode ser vazia.\n");
        return -1;
    }

    if (strcmp(dest, no->node_id) == 0)
    { // se o destino for o próprio nó, mostrar localmente
        printf("[CHAT] de %s para %s: %s\n", no->node_id, no->node_id, message);
        return 0;
    }
    
    int ridx = id_to_index(dest);
    if (ridx < 0 || ridx >= n_max_nos || !no->routing[ridx].valid ||
        no->routing[ridx].state != ROUTE_STATE_FORWARD)
    {
        printf("[ERRO] Sem rota para o destino %s. Usa 'announce' e confirma com 'sr %s'.\n", dest, dest);
        return -1;
    }
    // descobrir o vizinho next hop
    int nidx = neighbor_find_by_id(no, no->routing[ridx].next_hop);
    if (nidx == -1 || no->neighbors[nidx].fd == -1)
    {
        printf("[ERRO] Next-hop %s indisponível para o destino %s.\n",
               no->routing[ridx].next_hop, dest);
        return -1;
    }

    if (strlen(message) > CHAT_MAX_LEN)
    {
        printf("[ERRO] A mensagem excede o máximo de %d caracteres.\n", CHAT_MAX_LEN);
        return -1;
    }
    // enviar CHAT para o vizinho escolhido
    if (send_chat_to_fd(no->neighbors[nidx].fd, no->node_id, dest, message) < 0)
    {
        perror("[ERRO] write CHAT");
        return -1;
    }

    printf("[OK] mensagem enviada para destino=%s via vizinho=%s.\n",
           dest, no->routing[ridx].next_hop);
    return 0;
}
// trata uma mensagem ROUTE recebida por TCP
void handle_route_message(INFO_NO *no, int fd, const char *line)
{
    char dest[3] = "";
    int dist = -1;

    if (sscanf(line, "ROUTE %2s %d", dest, &dist) != 2)
    {
        printf("[AVISO] ROUTE mal formatada: %s\n", line);
        return;
    }

    if (!no->joined)
        return;

    monitor_log(no, "RX", fd, "%s", line);

    if (testa_formato_id(dest) || dist < 0)
    {
        printf("[AVISO] ROUTE inválida: %s\n", line);
        return;
    }
    // descobrir de que vizinho veio esta ROUTE
    int nidx = neighbor_find_by_fd(no, fd);
    if (nidx == -1 || no->neighbors[nidx].id[0] == '\0')
        return;
    // ignorar anúncios do próprio nó
    if (strcmp(dest, no->node_id) == 0)
        return;

    ROUTE_ENTRY *r = route_get_or_create(no, dest);
    if (!r)
        return;

    int new_dist = dist + 1;

    // só atualizar se encontrou caminho melhor
    if (new_dist < r->dist)
    {
        r->dist = new_dist;
        strncpy(r->next_hop, no->neighbors[nidx].id, sizeof(r->next_hop) - 1);
        r->next_hop[sizeof(r->next_hop) - 1] = '\0';

        if (r->state == ROUTE_STATE_FORWARD)
        {
            printf("[ROUTING] dest=%s via=%s dist=%d\n", r->dest, r->next_hop, r->dist);
            // pelo enunciado, reenviar a todos os vizinhos
            flood_route_except(no, dest, r->dist, -1);
        }
        else
        {
            printf("[ROUTING] dest=%s atualizado em coordenação: via=%s dist=%d\n",
                   r->dest, r->next_hop, r->dist);
        }
    }
}

void handle_coord_message(INFO_NO *no, int fd, const char *line)
{
    char dest[3] = "";
    // tenta ler o destina da msg COORD
    if (sscanf(line, "COORD %2s", dest) != 1)
    {
        printf("[AVISO] COORD mal formatada: %s\n", line);
        return;
    }

    if (!no->joined)
        return;

    monitor_log(no, "RX", fd, "%s", line);

    if (testa_formato_id(dest))
    { // validar id do destino
        printf("[AVISO] COORD inválida: %s\n", line);
        return;
    }
    // Descobrir de que vizinho veio a msg
    int nidx = neighbor_find_by_fd(no, fd);
    if (nidx == -1 || no->neighbors[nidx].id[0] == '\0')
        return;

    ROUTE_ENTRY *r = route_get_or_create(no, dest);
    if (!r)
        return;

    if (r->state == ROUTE_STATE_COORD)
    {
        /*
         * se eu já estava em coordenação e tinha uma rota temporária
         * por este vizinho, essa rota deixa de ser fiável
         */
        if (r->next_hop[0] != '\0' &&
            strcmp(r->next_hop, no->neighbors[nidx].id) == 0)
        {
            r->dist = ROUTE_INF;
            r->next_hop[0] = '\0';
        }
        // responder que pra mim esta coordenação terminou
        send_uncoord_to_fd(no, fd, dest);
        return;
    }
    // se a mensagem não veio do meu next hop, responder com ROUTE e UNCOORD
    if (r->next_hop[0] == '\0' || strcmp(no->neighbors[nidx].id, r->next_hop) != 0)
    {
        if (send_route_to_fd(no, fd, dest, r->dist) < 0)
            perror("[AVISO] write ROUTE");

        if (send_uncoord_to_fd(no, fd, dest) < 0)
            perror("[AVISO] write UNCOORD");

        return;
    }

    r->state = ROUTE_STATE_COORD; // se veio do meu next hop, também entro em coordenação
    strncpy(r->succ_coord, r->next_hop, sizeof(r->succ_coord) - 1);
    r->succ_coord[sizeof(r->succ_coord) - 1] = '\0';

    r->dist = ROUTE_INF; // se veio do meu next hop, também entro em coordenação
    r->next_hop[0] = '\0';
    // pedir coordenação a todos os vizinhos ativos
    for (int k = 0; k < n_max_internos; k++)
    { 
        r->coord_wait[k] = 0;

        if (no->neighbors[k].fd != -1)
        {
            r->coord_wait[k] = 1;
            if (send_coord_to_fd(no, no->neighbors[k].fd, dest) < 0)
                perror("[AVISO] write COORD");
        }
    }
    // se não ficou ninguém pendente, sair logo da coordenação
    if (route_all_coord_done(no, r))
        route_leave_coord(no, r);
}

void handle_uncoord_message(INFO_NO *no, int fd, const char *line)
{
    char dest[3] = "";
    // tentar ler destino da mensagem UNCOORD
    if (sscanf(line, "UNCOORD %2s", dest) != 1)
    {
        printf("[AVISO] UNCOORD mal formatada: %s\n", line);
        return;
    }

    if (!no->joined)
        return;

    monitor_log(no, "RX", fd, "%s", line);
    // validar destino
    if (testa_formato_id(dest))
    {
        printf("[AVISO] UNCOORD inválida: %s\n", line);
        return;
    }
    // descobrir de que vizinho veio
    int nidx = neighbor_find_by_fd(no, fd);
    if (nidx == -1)
        return;

    int ridx = id_to_index(dest);
    if (ridx < 0 || ridx >= n_max_nos || !no->routing[ridx].valid)
        return;

    ROUTE_ENTRY *r = &no->routing[ridx];

    // só interessa se esta rota estiver em coordenação
    if (r->state != ROUTE_STATE_COORD)
        return;
    // marcar que este vizinho já respondeu
    r->coord_wait[nidx] = 0;
    
    // se todos já responderam, sair da coordenação
    if (route_all_coord_done(no, r))
        route_leave_coord(no, r);
}

void handle_chat_message(INFO_NO *no, int fd, const char *line)
{
    char src[3] = "";
    char dest[3] = "";
    char text[CHAT_MAX_LEN + 1] = "";
    char extra = '\0';
    int matched;

    // garantir que começa com CHAT
    if (strncmp(line, "CHAT ", 5) != 0)
    {
        printf("[AVISO] mensagem de chat desconhecida: %s\n", line);
        return;
    }

    // garantir que começa com CHAT
    matched = sscanf(line, "CHAT %2s %2s %128[^\n]%c", src, dest, text, &extra);
    if (matched == 4)
    {
        printf("[AVISO] CHAT excede o máximo de %d caracteres: %s\n", CHAT_MAX_LEN, line);
        return;
    }
    if (matched != 3)
    {
        printf("[AVISO] CHAT mal formatada: %s\n", line);
        return;
    }

    if (!no->joined)
        return;

    // validar ids
    if (testa_formato_id(src) || testa_formato_id(dest))
    {
        printf("[AVISO] CHAT inválida: %s\n", line);
        return;
    }

    // não aceitar msg vazia
    if (text[0] == '\0')
    {
        printf("[AVISO] CHAT vazia descartada.\n");
        return;
    }

    // se a mensagem é para este nó, mostrar no terminal
    if (strcmp(dest, no->node_id) == 0)
    {
        printf("[CHAT] de %s para %s: %s\n", src, dest, text);
        return;
    }

    int ridx = id_to_index(dest);
    if (ridx < 0 || ridx >= n_max_nos || !no->routing[ridx].valid ||
        no->routing[ridx].state != ROUTE_STATE_FORWARD)
    {
        printf("[AVISO] sem rota para encaminhar mensagem para dest=%s.\n", dest);
        return;
    }
    // descobrir o next hop
    int nidx = neighbor_find_by_id(no, no->routing[ridx].next_hop);
    if (nidx == -1 || no->neighbors[nidx].fd == -1)
    {
        printf("[AVISO] next-hop %s indisponível para dest=%s.\n",
               no->routing[ridx].next_hop, dest);
        return;
    }

    if (no->neighbors[nidx].fd == fd)
    { // evitar devolver a mensagem para o mesmo lado de onde veio
        printf("[AVISO] rota para %s aponta de volta para o remetente; mensagem descartada.\n", dest);
        return;
    }
    // reenviar a msg ao prox nó
    if (send_chat_to_fd(no->neighbors[nidx].fd, src, dest, text) < 0)
        perror("[AVISO] write CHAT");
}

// -------------------------
// Neighbor helpers
// -------------------------

// Procurar o vizinho pelo id
int neighbor_find_by_id(const INFO_NO *no, const char *id)
{
    if (!id || strlen(id) != 2)
        return -1;
    for (int i = 0; i < n_max_internos; i++)
        if (no->neighbors[i].fd != -1 && strcmp(no->neighbors[i].id, id) == 0)
            return i;
    return -1;
}

// procurar um vizinho pelo fd
int neighbor_find_by_fd(const INFO_NO *no, int fd)
{
    for (int i = 0; i < n_max_internos; i++)
        if (no->neighbors[i].fd == fd)
            return i;
    return -1;
}

// devolve um slot livre na tabela de vizinhos
int neighbor_alloc_slot(INFO_NO *no)
{
    for (int i = 0; i < n_max_internos; i++)
        if (no->neighbors[i].fd == -1)
            return i;
    return -1;
}

void neighbor_clear_slot(INFO_NO *no, int idx)
{ // limpa uma entrada da tabela de vizinhos
    if (idx < 0 || idx >= n_max_internos)
        return;
    no->neighbors[idx].fd = -1;
    no->neighbors[idx].outgoing = 0;
    no->neighbors[idx].id[0] = '\0';
    no->neighbors[idx].ip[0] = '\0';
    no->neighbors[idx].tcp[0] = '\0';
}

// -------------------------
// Validações
// -------------------------

bool testa_invocacao_programa(int argc, char **argv)
{
    // OWR IP TCP [regIP regUDP]
    if (argc != 3 && argc != 5)
    {
        fprintf(stderr, "Uso: %s IP TCP [regIP regUDP]\n", argv[0]);
        return true;
    }

    if (testa_formato_ip(argv[1]))
    {
        fprintf(stderr, "[ERRO] IP inválido: %s\n", argv[1]);
        return true;
    }

    if (testa_formato_porto(argv[2]))
    {
        fprintf(stderr, "[ERRO] Porto TCP inválido: %s\n", argv[2]);
        return true;
    }

    if (argc == 5)
    {
        if (testa_formato_ip(argv[3]))
        {
            fprintf(stderr, "[ERRO] regIP inválido: %s\n", argv[3]);
            return true;
        }
        if (testa_formato_porto(argv[4]))
        {
            fprintf(stderr, "[ERRO] regUDP inválido: %s\n", argv[4]);
            return true;
        }
    }

    return false;
}

void inicializar_no(INFO_NO *no)
{
    no->id.fd = -1;
    no->no_ext.fd = -1;
    no->no_salv.fd = -1;
    for (int i = 0; i < n_max_internos; i++)
        no->no_int[i].fd = -1;

    no->net.net_id[0] = '\0';
    no->net.regIP[0] = '\0';
    no->net.regUDP[0] = '\0';

    no->node_id[0] = '\0';
    no->joined = 0;
    no->registered = 0;
    no->monitor_on = 0;
    for (int i = 0; i < n_max_internos; i++)
        neighbor_clear_slot(no, i);
    routing_reset(no);
}

int testa_formato_porto(char *porto)
{
    if (porto == NULL || *porto == '\0')
        return 1;
    for (int i = 0; porto[i] != '\0'; i++)
        if (!isdigit((unsigned char)porto[i]))
            return 1;
    int numero = atoi(porto);
    return (numero < 0 || numero > 65535) ? 1 : 0;
}

int testa_formato_rede(char *net)
{
    if (net == NULL || strlen(net) != 3)
        return 1;
    for (int i = 0; i < 3; i++)
        if (!isdigit((unsigned char)net[i]))
            return 1;
    int numero = atoi(net);
    return (numero < 0 || numero > 999) ? 1 : 0;
}

int testa_formato_id(char *id)
{
    if (id == NULL || strlen(id) != 2)
        return 1;
    for (int i = 0; i < 2; i++)
        if (!isdigit((unsigned char)id[i]))
            return 1;
    int numero = atoi(id);
    return (numero < 0 || numero > 99) ? 1 : 0;
}

int testa_formato_ip(char *ip)
{
    if (ip == NULL)
        return 1;

    int octetos = 0;
    int digit_count = 0;
    int num = 0;

    for (int i = 0; ip[i] != '\0'; i++)
    {
        if (isdigit((unsigned char)ip[i]))
        {
            num = num * 10 + (ip[i] - '0');
            digit_count++;
            if (num > 255)
                return 1;
        }
        else if (ip[i] == '.')
        {
            if (digit_count == 0 || digit_count > 3)
                return 1;
            octetos++;
            digit_count = 0;
            num = 0;
        }
        else
        {
            return 1;
        }
    }

    if (digit_count == 0 || digit_count > 3 || octetos != 3)
        return 1;

    return 0;
}

// -------------------------
// Parsing (stdin)
// -------------------------

int parse_buffer(const char *buffer, int tamanho_buffer, char words[][100], int max_words)
{
    if (max_words <= 0)
        return 0;

    for (int i = 0; i < max_words; i++)
        words[i][0] = '\0';

    char tmp[1024];
    size_t n = (size_t)tamanho_buffer;
    if (n >= sizeof(tmp))
        n = sizeof(tmp) - 1;

    strncpy(tmp, buffer, n);
    tmp[n] = '\0';

    int count = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(tmp, " \t\r\n", &saveptr);
    while (tok != NULL && count < max_words)
    {
        strncpy(words[count], tok, 99);
        words[count][99] = '\0';
        count++;
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    return count;
}

// -------------------------
// Comandos do enunciado (join / direct join / leave)
// -------------------------


// entra numa rede sem usar o servidor de registo
int direct_join(INFO_NO *no, const char *net, const char *id)
{
    // não pode entrar se já estiver numa rede
    if (no->joined)
    {
        printf("[ERRO] Já estás numa rede (%s). Faz 'leave' antes.\n", no->net.net_id);
        return -1;
    }

    if (testa_formato_rede((char *)net) || testa_formato_id((char *)id))
    { // validar o formato da rede e do id
        printf("[ERRO] Uso: direct join (dj) net id  (net=000..999, id=00..99)\n");
        return -1;
    }

    strncpy(no->net.net_id, net, sizeof(no->net.net_id) - 1); // guardar net e id localmente
    no->net.net_id[sizeof(no->net.net_id) - 1] = '\0';

    strncpy(no->node_id, id, sizeof(no->node_id) - 1);
    no->node_id[sizeof(no->node_id) - 1] = '\0';

    // como é direct join, não há servidor associado
    no->net.regIP[0] = '\0';
    no->net.regUDP[0] = '\0';

    no->joined = 1;
    no->registered = 0;// entrou, mas não ficou registado no servidor
    routing_reset(no);  // limpar tabela de routing

    printf("[OK] direct join: net=%s id=%s (sem registo no servidor)\n", no->net.net_id, no->node_id);
    return 0;
}

// Para entrar na rede com registo no servidor
int join(INFO_NO *no, const char *net, const char *id, const char *regIP, const char *regUDP)
{
    if (no->joined) // não pode fazer join se já está numa rede
    {
        printf("[ERRO] Já estás numa rede (%s). Faz 'leave' antes.\n", no->net.net_id);
        return -1;
    }

    if (testa_formato_rede((char *)net) || testa_formato_id((char *)id))
    { // valida formato da rede e do id
        printf("[ERRO] Uso: join (j) net id  (net=000..999, id=00..99)\n");
        return -1;
    }

    // A decisão de aceitar ou não o nó vem do server
    {
        int tid = next_tid();
        char req[128];
        snprintf(req, sizeof(req), "REG %03d 0 %s %s %s %s\n",
                 tid, net, id, no->id.ip, no->id.tcp);

        char resp[256];
        // enviar pedido udp e esperar resposta
        if (udp_request_response(regIP, regUDP, req, resp, sizeof(resp), 5) < 0)
            return -1;

        int r_tid = -1, op = -1;
        char r_net[4] = "";
        char r_id[3] = "";

        // parse mínimo
        if (sscanf(resp, "REG %d %d %3s %2s", &r_tid, &op, r_net, r_id) < 2)
        {
            printf("[ERRO] Resposta REG mal formatada: %s\n", resp);
            return -1;
        }

        if (r_tid != tid)
        { // garantir que a resposta corresponde ao pedido 
            printf("[ERRO] TID inconsistente na resposta REG.\n");
            return -1;
        }

        // sucesso 
        if (op == 1)
        {
            if (sscanf(resp, "REG %d %d %3s %2s", &r_tid, &op, r_net, r_id) != 4)
            {
                printf("[ERRO] Resposta REG mal formatada para op=1: %s\n", resp);
                return -1;
            }
            // confirma o id
            if (strcmp(r_net, net) != 0 || strcmp(r_id, id) != 0)
            {
                printf("[ERRO] Resposta REG inconsistente (net/id).\n");
                return -1;
            }
        }
        else
        {
            /* qualquer op != 1 significa que o servidor recusou o registo.
               Isto evita assumir localmente se foi "id duplicado", "bd cheia"
               ou outro motivo específico, caso o protocolo do professor use
               códigos diferentes. */
            printf("[ERRO] Servidor recusou o join para net=%s id=%s (REG op=%d).\n",
                   net, id, op);
            return -1;
        }
    }

    // só após REG aceite atualizar o estado local 
    strncpy(no->net.net_id, net, sizeof(no->net.net_id) - 1);
    no->net.net_id[sizeof(no->net.net_id) - 1] = '\0';

    strncpy(no->node_id, id, sizeof(no->node_id) - 1);
    no->node_id[sizeof(no->node_id) - 1] = '\0';

    strncpy(no->net.regIP, regIP, sizeof(no->net.regIP) - 1);
    no->net.regIP[sizeof(no->net.regIP) - 1] = '\0';

    strncpy(no->net.regUDP, regUDP, sizeof(no->net.regUDP) - 1);
    no->net.regUDP[sizeof(no->net.regUDP) - 1] = '\0';

    no->joined = 1;
    no->registered = 1;
    routing_reset(no);
    //routing_init_self(no); Essa rota só deve nascer quando o utilizador fizer "announce"

    printf("[OK] join: net=%s id=%s (registado em %s:%s)\n",
           no->net.net_id, no->node_id, no->net.regIP, no->net.regUDP);
    return 0;
}

int leave(INFO_NO *no, fd_set *master_set, int listen_fd, int *max_fd)
{
    if (!no->joined)
    {
        printf("[ERRO] Não estás em nenhuma rede.\n");
        return -1;
    }

    // remover arestas e fechar tudo exceto stdin e listen_fd
    if (master_set != NULL)
    {
        for (int fd = 0; fd <= *max_fd; fd++)
        {
            if (!FD_ISSET(fd, master_set))
                continue;

            if (fd == STDIN_FILENO || fd == listen_fd)
                continue;

            close(fd);
            FD_CLR(fd, master_set);
            clear_tcp_fd_state(fd);
        }

        *max_fd = (listen_fd > STDIN_FILENO) ? listen_fd : STDIN_FILENO;
    }
    // limpar completamente a tabela de vizinhos
    for (int i = 0; i < n_max_internos; i++)
        neighbor_clear_slot(no, i);

    // se foi join (registado), então UNREG via REG op=3
    if (no->registered)
    {
        int tid = next_tid();
        char req[128];
        snprintf(req, sizeof(req), "REG %03d 3 %s %s\n", tid, no->net.net_id, no->node_id);

        char resp[256];
        if (udp_request_response(no->net.regIP, no->net.regUDP, req, resp, sizeof(resp), 5) < 0)
        {
            printf("[AVISO] Falhou contacto com servidor no leave. Vou sair localmente na mesma.\n");
        }
        else
        {
            int r_tid = -1, op = -1;
            char r_net[4] = "";
            char r_id[3] = "";
            if (sscanf(resp, "REG %d %d %3s %2s", &r_tid, &op, r_net, r_id) == 4)
            {
                if (op != 4)
                    printf("[AVISO] Servidor respondeu com op=%d no UNREG.\n", op);
            }
            else
            {
                printf("[AVISO] Resposta UNREG mal formatada: %s\n", resp);
            }
        }
    }

    printf("[OK] leave: saíste da rede %s (id=%s).\n", no->net.net_id, no->node_id);

    // limpar estado local
    no->net.net_id[0] = '\0';
    no->net.regIP[0] = '\0';
    no->net.regUDP[0] = '\0';
    no->node_id[0] = '\0';
    no->joined = 0;
    no->registered = 0;
    routing_reset(no);

    return 0;
}

// -------------------------
// Arestas (ae / re / dae)
// -------------------------

static int send_neighbor_hello(int fd, const char *my_id)
{
    char msg[32];
    int n = snprintf(msg, sizeof(msg), "NEIGHBOR %s\n", my_id);
    if (n < 0)
        return -1;
    ssize_t w = write(fd, msg, (size_t)n);
    return (w == n) ? 0 : -1;
}

int direct_add_edge(INFO_NO *no, const char *id, const char *idIP, const char *idTCP,
                    fd_set *master_set, int *max_fd)
{
    if (!no->joined)
    {
        printf("[ERRO] Tens de estar numa rede antes de criar arestas (join/dj).\n");
        return -1;
    }
    if (testa_formato_id((char *)id) || testa_formato_ip((char *)idIP) || testa_formato_porto((char *)idTCP))
    {
        printf("[ERRO] Uso: dae id idIP idTCP\n");
        return -1;
    }
    if (strcmp(id, no->node_id) == 0)
    {
        printf("[ERRO] Não podes ligar-te a ti próprio (id=%s).\n", id);
        return -1;
    }
    if (neighbor_find_by_id(no, id) != -1)
    {
        printf("[ERRO] Já existe uma aresta com o nó %s.\n", id);
        return -1;
    }

    int slot = neighbor_alloc_slot(no);
    if (slot == -1)
    {
        printf("[ERRO] Limite de vizinhos atingido (%d).\n", n_max_internos);
        return -1;
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(idIP, idTCP, &hints, &res);
    if (err != 0)
    {
        printf("[ERRO] getaddrinfo(%s,%s): %s\n", idIP, idTCP, gai_strerror(err));
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1)
    {
        perror("[ERRO] socket(TCP)");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("[ERRO] connect");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    // registar localmente
    strncpy(no->neighbors[slot].id, id, sizeof(no->neighbors[slot].id) - 1);
    no->neighbors[slot].id[sizeof(no->neighbors[slot].id) - 1] = '\0';
    strncpy(no->neighbors[slot].ip, idIP, sizeof(no->neighbors[slot].ip) - 1);
    no->neighbors[slot].ip[sizeof(no->neighbors[slot].ip) - 1] = '\0';
    strncpy(no->neighbors[slot].tcp, idTCP, sizeof(no->neighbors[slot].tcp) - 1);
    no->neighbors[slot].tcp[sizeof(no->neighbors[slot].tcp) - 1] = '\0';
    no->neighbors[slot].fd = fd;
    no->neighbors[slot].outgoing = 1;

    if (master_set)
        FD_SET(fd, master_set);
    if (max_fd && fd > *max_fd)
        *max_fd = fd;

    (void)send_neighbor_hello(fd, no->node_id);
    routing_on_new_neighbor(no, id);

    printf("[OK] dae: ligado a id=%s (%s:%s) fd=%d\n", id, idIP, idTCP, fd);
    return 0;
}

int add_edge(INFO_NO *no, const char *id, fd_set *master_set, int *max_fd)
{
    if (!no->joined) // só pode adicionar aresta se estiver numa rede
    {
        printf("[ERRO] Não estás em nenhuma rede.\n");
        return -1;
    }

    if (!no->registered) // nesse comando, precisa-se estar no server
    {
        printf("[ERRO] add edge requer 'join' normal (com servidor), não 'direct join'.\n");
        return -1;
    }

    if (testa_formato_id((char *)id))
    { //testar formado do id
        printf("[ERRO] Uso: ae id\n");
        return -1;
    }

    if (strcmp(id, no->node_id) == 0) // valida o id, que nao é o proprio
    {
        printf("[ERRO] Não podes criar aresta contigo próprio.\n");
        return -1;
    }

    if (neighbor_find_by_id(no, id) != -1)
    { //ve se não tem aresta com este nó
        printf("[ERRO] Já existe aresta com o nó %s.\n", id);
        return -1;
    }
    // se passou tudo isso, pode-se tentar criar a aresta:

    int tid = next_tid();
    char req[64];
    // Pedir ao server os dados de contacto do nó de destino
    snprintf(req, sizeof(req), "CONTACT %03d 0 %s %s\n", tid, no->net.net_id, id);

    char resp[256];
    if (udp_request_response(no->net.regIP, no->net.regUDP, req, resp, sizeof(resp), 5) < 0)
        return -1;

    int r_tid = -1, op = -1;
    char r_net[4] = "", r_id[3] = "";

    if (sscanf(resp, "CONTACT %d %d %3s %2s", &r_tid, &op, r_net, r_id) != 4)
    { // parse base da resposta CONTACT
        printf("[ERRO] Resposta CONTACT mal formatada: %s\n", resp);
        return -1;
    }

    if (r_tid != tid)
    { // confirmar que a resposta corresponde ao pedido
        printf("[ERRO] TID inconsistente na resposta CONTACT.\n");
        return -1;
    }

    if (strcmp(r_net, no->net.net_id) != 0 || strcmp(r_id, id) != 0)
    {
        printf("[ERRO] Resposta CONTACT inconsistente (net/id).\n");
        return -1;
    }

    if (op == 2)
    { 
        printf("[ERRO] Nó %s não está registado na rede %s.\n", id, no->net.net_id);
        return -1;
    }
    // qualquer outro op diferente de 1 é erro
    if (op != 1)
    {
        printf("[ERRO] CONTACT op=%d.\n", op);
        return -1;
    }

    char ip[tamanho_ip] = "";
    char tcp[tamanho_porto] = "";

    if (sscanf(resp, "CONTACT %d %d %3s %2s %15s %5s",
               &r_tid, &op, r_net, r_id, ip, tcp) != 6)
    {
        printf("[ERRO] Resposta CONTACT mal formatada para op=1: %s\n", resp);
        return -1;
    }

    return direct_add_edge(no, id, ip, tcp, master_set, max_fd);
}

int remove_edge(INFO_NO *no, const char *id, fd_set *master_set, int *max_fd)
{
    if (!no->joined)
    {
        printf("[ERRO] Não estás em nenhuma rede.\n");
        return -1;
    }

    if (testa_formato_id((char *)id))
    {
        printf("[ERRO] Uso: re id\n");
        return -1;
    }

    int idx = neighbor_find_by_id(no, id);
    if (idx == -1)
    {
        printf("[ERRO] Não existe aresta com o nó %s.\n", id);
        return -1;
    }

    int fd = no->neighbors[idx].fd;
    char removed_id[3] = "";

    strncpy(removed_id, no->neighbors[idx].id, sizeof(removed_id) - 1);
    removed_id[sizeof(removed_id) - 1] = '\0';

    // Fechar a ligação TCP 
    close(fd);

    // Remover do conjunto monitorizado 
    if (master_set)
        FD_CLR(fd, master_set);

    // Limpar estado associado ao fd 
    clear_tcp_fd_state(fd);

    // Remover o vizinho da tabela local ANTES da invalidação 
    neighbor_clear_slot(no, idx);

    // Agora a invalidação já vê que o vizinho desapareceu
    if (removed_id[0] != '\0')
        routing_invalidate_next_hop(no, removed_id);

    // Se este fd era o maior, recalcular max_fd 
    if (master_set && max_fd && fd == *max_fd)
    {
        while (*max_fd >= 0 && !FD_ISSET(*max_fd, master_set))
            (*max_fd)--;

        if (*max_fd < 0)
            *max_fd = 0;
    }

    printf("[OK] re: aresta removida com id=%s (fd=%d)\n", id, fd);
    return 0;
}

int show_nodes_cmd(const char *net, const char *regIP, const char *regUDP)
{
    if (testa_formato_rede((char *)net))
    {
        printf("[ERRO] Uso: show nodes (n) net   (net=000..999)\n");
        return -1;
    }

    int tid = next_tid();
    char req[64];

    //  pedido op=0 e omitindo-se o carácter <LF> a seguir a net
    snprintf(req, sizeof(req), "NODES %03d 0 %s", tid, net);

    char resp[4096];
    int r = udp_request_response(regIP, regUDP, req, resp, sizeof(resp), 5);
    if (r < 0)
    {
        // erro já foi impresso pelo helper (timeout/recv/send)
        return -1;
    }

    // Resposta: "NODES tid op net<LF>" seguido de ids, 1 por linha. 
    char *saveptr = NULL;
    char *line = strtok_r(resp, "\n", &saveptr);
    if (!line)
    {
        printf("[ERRO] Resposta NODES vazia.\n");
        return -1;
    }

    int op = -1;
    char rnet[4] = "";
    if (sscanf(line, "NODES %*d %d %3s", &op, rnet) != 2)
    {
        printf("[ERRO] Resposta NODES mal formatada: %s\n", line);
        return -1;
    }

    if (op != 1)
    {
        printf("[ERRO] NODES falhou (op=%d).\n", op);
        return -1;
    }

    printf("Nodes na rede %s:\n", rnet);

    int count = 0;
    for (line = strtok_r(NULL, "\n", &saveptr); line != NULL; line = strtok_r(NULL, "\n", &saveptr))
    {
        // cada id deve ser "00".."99"
        char id[3] = "";
        if (sscanf(line, "%2s", id) == 1 && !testa_formato_id(id))
        {
            printf("  %s\n", id);
            count++;
        }
    }

    if (count == 0)
    {
        printf("  (sem nós)\n");
    }

    return 0;
}

void show_neighbors_cmd(const INFO_NO *no)
{
    if (!no->joined)
    {
        printf("[ERRO] Não estás em nenhuma rede.\n");
        return;
    }

    printf("Vizinhos do nó %s (net=%s):\n", no->node_id, no->net.net_id[0] ? no->net.net_id : "???");

    int count = 0;
    for (int i = 0; i < n_max_internos; i++)
    {
        if (no->neighbors[i].fd == -1)
            continue;

        const char *nid = (no->neighbors[i].id[0] ? no->neighbors[i].id : "??");
        const char *dir = (no->neighbors[i].outgoing ? "out" : "in");

        // ip/tcp podem estar vazios no caso de accept() (ainda não guardamos)
        const char *ip = (no->neighbors[i].ip[0] ? no->neighbors[i].ip : "-");
        const char *tcp = (no->neighbors[i].tcp[0] ? no->neighbors[i].tcp : "-");

        printf("  id=%s  fd=%d  dir=%s  ip=%s  tcp=%s\n", nid, no->neighbors[i].fd, dir, ip, tcp);
        count++;
    }

    if (count == 0)
    {
        printf("  (sem vizinhos)\n");
    }
}