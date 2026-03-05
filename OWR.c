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

static void print_prompt(void)
{
    printf("> ");
    fflush(stdout);
}

static int processa_comandos(const char *buffer, INFO_NO *no)
{
    char words[10][100];
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

    printf("Comandos (fase inicial):\n");
    printf("  join (j) net id\n");
    printf("  direct join (dj) net id\n");
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
                    FD_SET(new_fd, &master_fds);
                    if (new_fd > max_fd)
                        max_fd = new_fd;
                }
            }
            else
            {
                char tmp[256];
                int n = (int)read(i, tmp, sizeof(tmp));
                if (n <= 0)
                {
                    close(i);
                    FD_CLR(i, &master_fds);
                }
            }
        }
    }

    if (no.joined)
        (void)leave(&no, &master_fds, listen_fd, &max_fd);
    close(listen_fd);
    return 0;
}