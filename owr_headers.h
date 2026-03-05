#ifndef OWR_HEADERS_H
#define OWR_HEADERS_H

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#define tamanho_ip 16
#define tamanho_porto 6
#define n_max_internos 50
#define n_max_interfaces (n_max_internos + 1)
#define n_max_nos 100
#define n_max_interests 100
#define tamanho_max_obj 100

/******************* Estrutura para identificação de rede *******************/
typedef struct info_net {
    char net_id[4];
    char regIP[20];
    char regUDP[7];
} INFO_NET;

/******************* Estrutura para identificação de um nó *******************/
typedef struct no {
    char ip[tamanho_ip];
    char tcp[tamanho_porto];
    int fd;
} ID_NO;

/****************** Estrutura para armazenar informações de um nó ******************/
typedef struct info_no {
    ID_NO id;                    // contacto local (IP/TCP)
    ID_NO no_ext;                // (placeholder p/ próxima fase)
    ID_NO no_salv;               // (placeholder p/ próxima fase)
    ID_NO no_int[n_max_internos];// (placeholder p/ próxima fase)
    INFO_NET net;

    char node_id[3];             // "00".."99"
    int joined;                  // 1 se em rede
    int registered;              // 1 se registado no servidor de nós
} INFO_NO;

// Validações
int testa_formato_ip(char *ip);
int testa_formato_porto(char *porto);
int testa_formato_rede(char *net);
int testa_formato_id(char *id);

// Utilitários
bool testa_invocacao_programa(int argc, char **argv);
void inicializar_no(INFO_NO *no);
int parse_buffer(const char *buffer, int tamanho_buffer, char words[][100], int max_words);

// Comandos (fase inicial)
int join(INFO_NO *no, const char *net, const char *id, const char *regIP, const char *regUDP);
int direct_join(INFO_NO *no, const char *net, const char *id);
int leave(INFO_NO *no, fd_set *master_set, int listen_fd, int *max_fd);

#endif