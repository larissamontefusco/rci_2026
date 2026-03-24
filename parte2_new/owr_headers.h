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

#define ROUTE_INF 1000000000

/* Pelo enunciado:
   state=0 -> expedição
   state=1 -> coordenação
*/
#define ROUTE_STATE_FORWARD 0
#define ROUTE_STATE_COORD   1
#define ROUTE_STATE_NONE    2

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

// Vizinho = aresta TCP
typedef struct neighbor {
    char id[3];
    char ip[tamanho_ip];
    char tcp[tamanho_porto];
    int fd;
    int outgoing; // 1 se connect() local, 0 se accept()
} NEIGHBOR;

typedef struct route_entry {
    int valid;
    char dest[3];

    int state;                  // ROUTE_STATE_FORWARD / ROUTE_STATE_COORD
    int dist;                   // dist[t], ROUTE_INF se inalcançável
    char next_hop[3];           // succ[t], "" se não existir

    char succ_coord[3];         // succ_coord[t], "" equivale a -1
    int coord_wait[n_max_internos]; // coord[t,j] por slot de vizinho
} ROUTE_ENTRY;

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
    NEIGHBOR neighbors[n_max_internos];
    ROUTE_ENTRY routing[n_max_nos];
    int monitor_on;              // 1 se monitorização de mensagens de encaminhamento ativa
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

// Arestas (overlay)
int add_edge(INFO_NO *no, const char *id, fd_set *master_set, int *max_fd);
int remove_edge(INFO_NO *no, const char *id, fd_set *master_set, int *max_fd);
int direct_add_edge(INFO_NO *no, const char *id, const char *idIP, const char *idTCP,
                    fd_set *master_set, int *max_fd);

int show_nodes_cmd(const char *net, const char *regIP, const char *regUDP);
void show_neighbors_cmd(const INFO_NO *no);

// Routing / controlo overlay
void routing_reset(INFO_NO *no);
void routing_init_self(INFO_NO *no);
void routing_invalidate_next_hop(INFO_NO *no, const char *neighbor_id);
void routing_on_new_neighbor(INFO_NO *no, const char *neighbor_id);
int route_cmd(INFO_NO *no);
void show_routing_cmd(const INFO_NO *no, const char *dest);
void start_monitor_cmd(INFO_NO *no);
void end_monitor_cmd(INFO_NO *no);
int message_cmd(INFO_NO *no, const char *dest, const char *message);
void handle_route_message(INFO_NO *no, int fd, const char *line);
void handle_coord_message(INFO_NO *no, int fd, const char *line);
void handle_uncoord_message(INFO_NO *no, int fd, const char *line);
void handle_msg_message(INFO_NO *no, int fd, const char *line);
// Helpers vizinhos
int neighbor_find_by_id(const INFO_NO *no, const char *id);
int neighbor_find_by_fd(const INFO_NO *no, int fd);
int neighbor_alloc_slot(INFO_NO *no);
void neighbor_clear_slot(INFO_NO *no, int idx);
void clear_tcp_fd_state(int fd);
#endif