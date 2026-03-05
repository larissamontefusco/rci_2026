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

#include "owr_headers.h"

// -------------------------
// Helpers
// -------------------------

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

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int err = getaddrinfo(server_ip, server_port, &hints, &res);
    if (err != 0)
    {
        fprintf(stderr, "[ERRO] getaddrinfo(%s,%s): %s\n", server_ip, server_port, gai_strerror(err));
        return -1;
    }

    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1)
    {
        perror("[ERRO] socket(UDP)");
        freeaddrinfo(res);
        return -1;
    }

    ssize_t n = sendto(fd, request, strlen(request), 0, res->ai_addr, res->ai_addrlen);
    if (n == -1)
    {
        perror("[ERRO] sendto");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    // wait for reply with timeout
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
}

int testa_formato_porto(char *porto)
{
    if (porto == NULL || *porto == '\0')
        return 1;

    for (int i = 0; porto[i] != '\0'; i++)
        if (!isdigit((unsigned char)porto[i]))
            return 1;

    int numero = atoi(porto);
    if (numero < 0 || numero > 65535)
        return 1;

    return 0;
}

int testa_formato_rede(char *net)
{
    if (net == NULL || strlen(net) != 3)
        return 1;

    for (int i = 0; i < 3; i++)
        if (!isdigit((unsigned char)net[i]))
            return 1;

    int numero = atoi(net);
    if (numero < 0 || numero > 999)
        return 1;

    return 0;
}

int testa_formato_id(char *id)
{
    if (id == NULL || strlen(id) != 2)
        return 1;

    for (int i = 0; i < 2; i++)
        if (!isdigit((unsigned char)id[i]))
            return 1;

    int numero = atoi(id);
    if (numero < 0 || numero > 99)
        return 1;

    return 0;
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

int direct_join(INFO_NO *no, const char *net, const char *id)
{
    if (no->joined)
    {
        printf("[ERRO] Já estás numa rede (%s). Faz 'leave' antes.\n", no->net.net_id);
        return -1;
    }

    if (testa_formato_rede((char *)net) || testa_formato_id((char *)id))
    {
        printf("[ERRO] Uso: direct join (dj) net id  (net=000..999, id=00..99)\n");
        return -1;
    }

    strncpy(no->net.net_id, net, sizeof(no->net.net_id) - 1);
    no->net.net_id[sizeof(no->net.net_id) - 1] = '\0';

    strncpy(no->node_id, id, sizeof(no->node_id) - 1);
    no->node_id[sizeof(no->node_id) - 1] = '\0';

    no->net.regIP[0] = '\0';
    no->net.regUDP[0] = '\0';

    no->joined = 1;
    no->registered = 0;

    printf("[OK] direct join: net=%s id=%s (sem registo no servidor)\n", no->net.net_id, no->node_id);
    return 0;
}

int join(INFO_NO *no, const char *net, const char *id, const char *regIP, const char *regUDP)
{
    if (no->joined)
    {
        printf("[ERRO] Já estás numa rede (%s). Faz 'leave' antes.\n", no->net.net_id);
        return -1;
    }

    if (testa_formato_rede((char *)net) || testa_formato_id((char *)id))
    {
        printf("[ERRO] Uso: join (j) net id  (net=000..999, id=00..99)\n");
        return -1;
    }

    // 1) NODES -> confirmar unicidade do id
    {
        int tid = next_tid();
        char req[64];
        snprintf(req, sizeof(req), "NODES %03d 0 %s", tid, net);

        char resp[2048];
        if (udp_request_response(regIP, regUDP, req, resp, sizeof(resp), 5) < 0)
            return -1;

        char *saveptr = NULL;
        char *line = strtok_r(resp, "\n", &saveptr);
        if (!line)
        {
            printf("[ERRO] Resposta NODES mal formatada (vazia).\n");
            return -1;
        }

        int r_tid = -1, op = -1;
        char r_net[4] = "";
        if (sscanf(line, "NODES %d %d %3s", &r_tid, &op, r_net) != 3)
        {
            printf("[ERRO] Resposta NODES mal formatada: %s\n", line);
            return -1;
        }

        if (op != 1)
        {
            printf("[ERRO] Servidor respondeu NODES com op=%d (erro).\n", op);
            return -1;
        }

        if (strcmp(r_net, net) != 0)
        {
            printf("[ERRO] Servidor respondeu para net=%s mas pedimos net=%s.\n", r_net, net);
            return -1;
        }

        for (line = strtok_r(NULL, "\n", &saveptr); line != NULL; line = strtok_r(NULL, "\n", &saveptr))
        {
            if (strlen(line) == 2 && strcmp(line, id) == 0)
            {
                printf("[ERRO] O id %s já existe na rede %s. Escolhe outro.\n", id, net);
                return -1;
            }
        }
    }

    // 2) REG tid 0 net id IP TCP\n
    {
        int tid = next_tid();
        char req[128];
        snprintf(req, sizeof(req), "REG %03d 0 %s %s %s %s\n", tid, net, id, no->id.ip, no->id.tcp);

        char resp[256];
        if (udp_request_response(regIP, regUDP, req, resp, sizeof(resp), 5) < 0)
            return -1;

        int r_tid = -1, op = -1;
        char r_net[4] = "";
        char r_id[3] = "";
        if (sscanf(resp, "REG %d %d %3s %2s", &r_tid, &op, r_net, r_id) != 4)
        {
            printf("[ERRO] Resposta REG mal formatada: %s\n", resp);
            return -1;
        }

        if (op != 1)
        {
            printf("[ERRO] Servidor respondeu REG com op=%d (erro).\n", op);
            return -1;
        }

        if (strcmp(r_net, net) != 0 || strcmp(r_id, id) != 0)
        {
            printf("[ERRO] Resposta REG inconsistente (net/id).\n");
            return -1;
        }
    }

    // sucesso: atualizar estado do nó
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

    printf("[OK] join: net=%s id=%s (registado em %s:%s)\n", no->net.net_id, no->node_id, no->net.regIP, no->net.regUDP);
    return 0;
}

int leave(INFO_NO *no, fd_set *master_set, int listen_fd, int *max_fd)
{
    if (!no->joined)
    {
        printf("[ERRO] Não estás em nenhuma rede.\n");
        return -1;
    }

    // 1) remover arestas: fechar tudo exceto stdin e listen_fd
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
        }

        *max_fd = (listen_fd > STDIN_FILENO) ? listen_fd : STDIN_FILENO;
    }

    // 2) se foi join (registado), então UNREG via REG op=3
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

    // 3) limpar estado local
    no->net.net_id[0] = '\0';
    no->net.regIP[0] = '\0';
    no->net.regUDP[0] = '\0';
    no->node_id[0] = '\0';
    no->joined = 0;
    no->registered = 0;

    return 0;
}