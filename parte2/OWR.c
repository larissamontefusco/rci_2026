#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "owr_headers.h"

#define max(A, B) ((A) >= (B) ? (A) : (B))
#define TAMANHO_BUFFER 256

static fd_set *master_set = NULL;
static int max_fd = 0;
static char regIP[tamanho_ip] = "193.136.138.142";
static char regUDP[tamanho_porto] = "59000";

// framing TCP por fd
static char rxbuf[FD_SETSIZE][2048];
static int rxlen[FD_SETSIZE];

static void print_prompt(void)
{
    printf("> ");
    fflush(stdout);
}

void clear_tcp_fd_state(int fd)
{
    if (fd >= 0 && fd < FD_SETSIZE)
    {
        rxlen[fd] = 0;
        rxbuf[fd][0] = '\0';
    }
}

static void drop_fd(INFO_NO *no, int fd)
{
    int idx = neighbor_find_by_fd(no, fd);
    char removed_id[3] = "";

    if (idx != -1)
    {
        strncpy(removed_id, no->neighbors[idx].id, sizeof(removed_id) - 1);
        removed_id[sizeof(removed_id) - 1] = '\0';

        /* Remover primeiro da tabela local */
        neighbor_clear_slot(no, idx);
    }

    /* Remover do conjunto monitorizado */
    if (master_set)
        FD_CLR(fd, master_set);

    /* Fechar e limpar estado local do fd */
    close(fd);
    clear_tcp_fd_state(fd);

    /* Recalcular max_fd se este era o maior descritor ativo */
    if (master_set && fd == max_fd)
    {
        while (max_fd >= 0 && !FD_ISSET(max_fd, master_set))
            max_fd--;

        if (max_fd < 0)
            max_fd = 0;
    }

    /* Só depois invalidar rotas dependentes deste vizinho */
    if (removed_id[0] != '\0')
        routing_invalidate_next_hop(no, removed_id);
}

static int prefer_outgoing(const char *my_id, const char *other_id)
{
    // id menor mantém outgoing; id maior mantém incoming
    return (strcmp(my_id, other_id) < 0) ? 1 : 0;
}

static void handle_neighbor_message(INFO_NO *no, int fd, const char *line)
{
    char id[3] = "";
    int newly_identified = 0;

    if (sscanf(line, "NEIGHBOR %2s", id) != 1)
    {
        printf("[AVISO] NEIGHBOR mal formatado: %s\n", line);
        return;
    }
    if (testa_formato_id(id))
    {
        printf("[AVISO] NEIGHBOR com id inválido: %s\n", id);
        return;
    }

    int idx = neighbor_find_by_fd(no, fd);
    if (idx == -1)
        return;

    if (no->neighbors[idx].id[0] == '\0')
    {
        strncpy(no->neighbors[idx].id, id, sizeof(no->neighbors[idx].id) - 1);
        no->neighbors[idx].id[sizeof(no->neighbors[idx].id) - 1] = '\0';
        newly_identified = 1;
        printf("[OK] vizinho identificado: fd=%d id=%s\n", fd, id);
    }
    else if (strcmp(no->neighbors[idx].id, id) != 0)
    {
        printf("[ERRO] Mismatch de vizinho no fd=%d: esperado=%s, recebido=%s. A fechar ligação.\n",
               fd, no->neighbors[idx].id, id);
        drop_fd(no, fd);
        return;
    }

    int other_idx = neighbor_find_by_id(no, id);
    if (other_idx != -1 && other_idx != idx)
    {
        int keep_out = prefer_outgoing(no->node_id, id);
        int idx_out = (no->neighbors[idx].outgoing) ? idx : other_idx;
        int idx_in = (no->neighbors[idx].outgoing) ? other_idx : idx;

        int keep_idx = keep_out ? idx_out : idx_in;
        int drop_idx = keep_out ? idx_in : idx_out;

        int drop_fd_val = no->neighbors[drop_idx].fd;

        printf("[AVISO] aresta duplicada com %s; mantendo %s (fd=%d), a fechar fd=%d\n",
               id,
               (no->neighbors[keep_idx].outgoing ? "outgoing" : "incoming"),
               no->neighbors[keep_idx].fd,
               drop_fd_val);

        drop_fd(no, drop_fd_val);
    }

    if (newly_identified && neighbor_find_by_fd(no, fd) != -1)
        routing_on_new_neighbor(no, id);
}

static void handle_tcp_lines(INFO_NO *no, int fd)
{
    char tmp[512];
    int n = (int)read(fd, tmp, sizeof(tmp));
    if (n <= 0)
    {
        if (n == 0)
            printf("[TCP] fd=%d fechou\n", fd);
        else
            perror("[TCP] read");
        drop_fd(no, fd);
        return;
    }

    if (fd < 0 || fd >= FD_SETSIZE)
        return;

    int cap = (int)sizeof(rxbuf[fd]);
    if (rxlen[fd] + n >= cap)
        rxlen[fd] = 0; // defensivo

    memcpy(rxbuf[fd] + rxlen[fd], tmp, (size_t)n);
    rxlen[fd] += n;

    int start = 0;
    for (int i = 0; i < rxlen[fd]; i++)
    {
        if (rxbuf[fd][i] == '\n')
        {
            int len = i - start;
            if (len < 0)
                len = 0;

            char line[1024];
            if (len >= (int)sizeof(line))
                len = (int)sizeof(line) - 1;

            memcpy(line, rxbuf[fd] + start, (size_t)len);
            line[len] = '\0';
            if (len > 0 && line[len - 1] == '\r')
                line[len - 1] = '\0';

            if (strncmp(line, "NEIGHBOR ", 9) == 0)
                handle_neighbor_message(no, fd, line);
            else if (strncmp(line, "ROUTE ", 6) == 0)
                handle_route_message(no, fd, line);
            else if (strncmp(line, "COORD ", 6) == 0)
                handle_coord_message(no, fd, line);
            else if (strncmp(line, "UNCOORD ", 8) == 0)
                handle_uncoord_message(no, fd, line);
            else if (strncmp(line, "CHAT ", 5) == 0)
                handle_chat_message(no, fd, line);
            else
                printf("[TCP] fd=%d line: %s\n", fd, line);

            start = i + 1;
        }
    }

    if (start > 0)
    {
        int remaining = rxlen[fd] - start;
        if (remaining > 0)
            memmove(rxbuf[fd], rxbuf[fd] + start, (size_t)remaining);
        rxlen[fd] = remaining;
    }
}

static int extract_message_args(const char *buffer, char *dest, size_t dest_sz, char *msg, size_t msg_sz)
{
    const char *p = buffer;
    int msg_too_long = 0;

    while (*p == ' ' || *p == '\t')
        p++;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        p++;
    while (*p == ' ' || *p == '\t')
        p++;

    size_t di = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
    {
        if (di + 1 < dest_sz)
            dest[di++] = *p;
        p++;
    }
    dest[di] = '\0';

    while (*p == ' ' || *p == '\t')
        p++;

    size_t mi = 0;
    while (*p && *p != '\r' && *p != '\n')
    {
        if (mi + 1 < msg_sz)
            msg[mi++] = *p;
        else
            msg_too_long = 1;
        p++;
    }
    msg[mi] = '\0';

    if (dest[0] == '\0' || msg[0] == '\0' || msg_too_long)
        return -1;

    return 0;
}

static int processa_comandos(const char *buffer, INFO_NO *no)
{
    char words[12][100];
    int argc_cmd = parse_buffer(buffer, (int)strlen(buffer), words, 10);

    if (argc_cmd == 0)
        return 0;

    if (strcmp(words[0], "join") == 0 || strcmp(words[0], "j") == 0)
    {
        if (argc_cmd < 3)
        {
            printf("Uso: join (j) net id\n");
            return 0;
        }
        (void)join(no, words[1], words[2], regIP, regUDP);
        return 0;
    }

    if (strcmp(words[0], "dj") == 0)
    {
        if (argc_cmd < 3)
        {
            printf("Uso: dj net id\n");
            return 0;
        }
        (void)direct_join(no, words[1], words[2]);
        return 0;
    }

    if (strcmp(words[0], "direct") == 0 && argc_cmd >= 2 && strcmp(words[1], "join") == 0)
    {
        if (argc_cmd < 4)
        {
            printf("Uso: direct join net id\n");
            return 0;
        }
        (void)direct_join(no, words[2], words[3]);
        return 0;
    }

    // add edge (ae)
    if (strcmp(words[0], "ae") == 0 || (strcmp(words[0], "add") == 0 && argc_cmd >= 2 && strcmp(words[1], "edge") == 0))
    {
        const char *id = (strcmp(words[0], "ae") == 0) ? (argc_cmd >= 2 ? words[1] : NULL) : (argc_cmd >= 3 ? words[2] : NULL);
        if (!id)
        {
            printf("Uso: ae id\n");
            return 0;
        }
        (void)add_edge(no, id, master_set, &max_fd);
        return 0;
    }

    // direct add edge (dae)
    if (strcmp(words[0], "dae") == 0 || (strcmp(words[0], "direct") == 0 && argc_cmd >= 3 && strcmp(words[1], "add") == 0 && strcmp(words[2], "edge") == 0))
    {
        const char *id = NULL, *ip = NULL, *tcp = NULL;

        if (strcmp(words[0], "dae") == 0)
        {
            if (argc_cmd < 4)
            {
                printf("Uso: dae id idIP idTCP\n");
                return 0;
            }
            id = words[1];
            ip = words[2];
            tcp = words[3];
        }
        else
        {
            if (argc_cmd < 6)
            {
                printf("Uso: direct add edge id idIP idTCP\n");
                return 0;
            }
            id = words[3];
            ip = words[4];
            tcp = words[5];
        }

        (void)direct_add_edge(no, id, ip, tcp, master_set, &max_fd);
        return 0;
    }

    // remove edge (re)
    if (strcmp(words[0], "re") == 0 || (strcmp(words[0], "remove") == 0 && argc_cmd >= 2 && strcmp(words[1], "edge") == 0))
    {
        const char *id = (strcmp(words[0], "re") == 0) ? (argc_cmd >= 2 ? words[1] : NULL) : (argc_cmd >= 3 ? words[2] : NULL);
        if (!id)
        {
            printf("Uso: re id\n");
            return 0;
        }
        (void)remove_edge(no, id, master_set, &max_fd);
        return 0;
    }

    if (strcmp(words[0], "leave") == 0 || strcmp(words[0], "l") == 0)
    {
        if (master_set == NULL)
        {
            printf("[ERRO] master_set não inicializado.\n");
            return 0;
        }
        (void)leave(no, master_set, no->id.fd, &max_fd);
        return 0;
    }

    if (strcmp(words[0], "sm") == 0 ||
        (strcmp(words[0], "start") == 0 && argc_cmd >= 2 && strcmp(words[1], "monitor") == 0))
    {
        start_monitor_cmd(no);
        return 0;
    }

    if (strcmp(words[0], "em") == 0 ||
        (strcmp(words[0], "end") == 0 && argc_cmd >= 2 && strcmp(words[1], "monitor") == 0))
    {
        end_monitor_cmd(no);
        return 0;
    }

    if (strcmp(words[0], "m") == 0 || strcmp(words[0], "message") == 0)
    {
        char dest[3] = "";
        char msg[CHAT_MAX_LEN + 1] = "";
        if (extract_message_args(buffer, dest, sizeof(dest), msg, sizeof(msg)) < 0)
        {
            printf("Uso: message (m) dest texto   (texto com no máximo %d caracteres)\n", CHAT_MAX_LEN);
            return 0;
        }
        (void)message_cmd(no, dest, msg);
        return 0;
    }

    if (strcmp(words[0], "announce") == 0 || strcmp(words[0], "a") == 0)
    {
        (void)route_cmd(no);
        return 0;
    }

    if (strcmp(words[0], "sr") == 0 ||
        (strcmp(words[0], "show") == 0 && argc_cmd >= 2 && strcmp(words[1], "routing") == 0))
    {
        const char *dest = NULL;

        if (strcmp(words[0], "sr") == 0)
        {
            if (argc_cmd < 2)
            {
                printf("Uso: sr dest\n");
                return 0;
            }
            dest = words[1];
        }
        else
        {
            if (argc_cmd < 3)
            {
                printf("Uso: show routing dest\n");
                return 0;
            }
            dest = words[2];
        }

        show_routing_cmd(no, dest);
        return 0;
    }

    // show neighbors (sg)
    if (strcmp(words[0], "sg") == 0 || (strcmp(words[0], "show") == 0 && argc_cmd >= 2 && strcmp(words[1], "neighbors") == 0))
    {
        show_neighbors_cmd(no);
        return 0;
    }

    // show nodes (n) net
    if (strcmp(words[0], "n") == 0 || (strcmp(words[0], "show") == 0 && argc_cmd >= 2 && strcmp(words[1], "nodes") == 0))
    {
        const char *net = NULL;

        if (strcmp(words[0], "n") == 0)
        {
            if (argc_cmd < 2)
            {
                printf("Uso: n net\n");
                return 0;
            }
            net = words[1];
        }
        else
        {
            if (argc_cmd < 3)
            {
                printf("Uso: show nodes net\n");
                return 0;
            }
            net = words[2];
        }

        (void)show_nodes_cmd(net, regIP, regUDP);
        return 0;
    }
    if (strcmp(words[0], "exit") == 0 || strcmp(words[0], "x") == 0)
        return 1;

    printf("Comando desconhecido: %s\n", words[0]);
    return 0;
}

int main(int argc, char **argv)
{
    if (testa_invocacao_programa(argc, argv))
        return 1;

    INFO_NO no;
    inicializar_no(&no);

    strncpy(no.id.ip, argv[1], sizeof(no.id.ip) - 1);
    no.id.ip[sizeof(no.id.ip) - 1] = '\0';

    strncpy(no.id.tcp, argv[2], sizeof(no.id.tcp) - 1);
    no.id.tcp[sizeof(no.id.tcp) - 1] = '\0';

    if (argc == 5)
    {
        strncpy(regIP, argv[3], sizeof(regIP) - 1);
        regIP[sizeof(regIP) - 1] = '\0';

        strncpy(regUDP, argv[4], sizeof(regUDP) - 1);
        regUDP[sizeof(regUDP) - 1] = '\0';
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        perror("socket");
        return 1;
    }

    int yes = 1;
    (void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (getaddrinfo(NULL, no.id.tcp, &hints, &res) != 0)
    {
        perror("getaddrinfo");
        close(listen_fd);
        return 1;
    }

    if (bind(listen_fd, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("bind");
        freeaddrinfo(res);
        close(listen_fd);
        return 1;
    }

    freeaddrinfo(res);

    if (listen(listen_fd, 5) == -1)
    {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    no.id.fd = listen_fd;

    fd_set master_fds, read_fds;
    FD_ZERO(&master_fds);
    FD_SET(STDIN_FILENO, &master_fds);
    FD_SET(listen_fd, &master_fds);

    master_set = &master_fds;
    max_fd = max(STDIN_FILENO, listen_fd);

    printf("========================================\n");
    printf(" OWR iniciado (listen TCP=%s)\n", no.id.tcp);
    printf(" regIP/regUDP = %s:%s\n", regIP, regUDP);
    printf("========================================\n\n");

    printf("Comandos:\n");
    printf("  join (j) net id\n");
    printf("  direct join (dj) net id\n");
    printf("  show nodes (n) net\n");
    printf("  add edge (ae) id\n");
    printf("  remove edge (re) id\n");
    printf("  direct add edge (dae) id idIP idTCP\n");
    printf("  annouce (a)\n");
    printf("  show routing (sr) dest\n");
    printf("  start monitor (sm)\n");
    printf("  end monitor (em)\n");
    printf("  message (m) dest texto\n");
    printf("  show neighbors (sg)\n");
    printf("  leave (l)\n");
    printf("  exit (x)\n\n");

    print_prompt();

    while (1)
    {
        read_fds = master_fds;

        int counter = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (counter == -1)
        {
            perror("select");
            break;
        }

        for (int i = 0; i <= max_fd; i++)
        {
            if (!FD_ISSET(i, &read_fds))
                continue;

            if (i == STDIN_FILENO)
            {
                char buffer[TAMANHO_BUFFER];
                if (fgets(buffer, sizeof(buffer), stdin) == NULL)
                {
                    printf("\n");
                    if (no.joined)
                        (void)leave(&no, &master_fds, listen_fd, &max_fd);
                    close(listen_fd);
                    return 0;
                }

                int want_exit = processa_comandos(buffer, &no);
                if (want_exit)
                {
                    if (no.joined)
                        (void)leave(&no, &master_fds, listen_fd, &max_fd);
                    close(listen_fd);
                    return 0;
                }

                print_prompt();
            }
            else if (i == listen_fd)
            {
                struct sockaddr_storage addr;
                socklen_t addrlen = sizeof(addr);
                int new_fd = accept(listen_fd, (struct sockaddr *)&addr, &addrlen);
                if (new_fd == -1)
                {
                    perror("accept");
                }
                else
                {
                    /* CORREÇÃO: se o nó não está numa rede, não pode aceitar arestas */
                    if (!no.joined)
                    {
                        printf("[AVISO] Ligação recebida fora de rede; a rejeitar fd=%d.\n", new_fd);
                        close(new_fd);
                        continue;
                    }
                    int slot = neighbor_alloc_slot(&no);
                    if (slot == -1)
                    {
                        printf("[ERRO] Sem slots para vizinhos — a fechar ligação (fd=%d).\n", new_fd);
                        close(new_fd);
                    }
                    else
                    {
                        no.neighbors[slot].fd = new_fd;
                        no.neighbors[slot].outgoing = 0;
                        no.neighbors[slot].id[0] = '\0';
                        no.neighbors[slot].ip[0] = '\0';
                        no.neighbors[slot].tcp[0] = '\0';

                        // preencher ip/porto do peer (atenção: porto é o porto efémero da conexão)
                        struct sockaddr_storage peer;
                        socklen_t peerlen = sizeof(peer);
                        if (getpeername(new_fd, (struct sockaddr *)&peer, &peerlen) == 0)
                        {
                            if (peer.ss_family == AF_INET)
                            {
                                struct sockaddr_in *sin = (struct sockaddr_in *)&peer;
                                inet_ntop(AF_INET, &sin->sin_addr,
                                          no.neighbors[slot].ip, sizeof(no.neighbors[slot].ip));
                            }
                        }
                        FD_SET(new_fd, &master_fds);
                        if (new_fd > max_fd)
                            max_fd = new_fd;

                        // envia o nosso id

                        char msg[32];
                        int n = snprintf(msg, sizeof(msg), "NEIGHBOR %s\n", no.node_id);
                        if (n > 0)
                            (void)write(new_fd, msg, (size_t)n);

                        printf("[TCP] accepted fd=%d\n", new_fd);
                    }
                }
            }
            else
            {
                handle_tcp_lines(&no, i);
            }
        }
    }

    if (no.joined)
        (void)leave(&no, &master_fds, listen_fd, &max_fd);
    close(listen_fd);
    return 0;
}