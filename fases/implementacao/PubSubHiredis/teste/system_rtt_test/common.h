#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <hiredis/hiredis.h>
#include <pthread.h>
#include <nng/nng.h>
#include <nng/mqtt/mqtt_client.h>
#include <nng/supplemental/util/platform.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>


//caso for compilar com TLS, o header do TLS vai estar incluso
#ifndef NNG_SUPP_TLS
#include <nng/supplemental/tls/tls.h>

// Definições TLS
#ifndef NNG_TLS_MODE_CLIENT
#define NNG_TLS_MODE_CLIENT 0
#endif

#ifndef NNG_TLS_AUTH_MODE_NONE
#define NNG_TLS_AUTH_MODE_NONE 0
#endif

#ifndef NNG_TLS_AUTH_MODE_REQUIRED
#define NNG_TLS_AUTH_MODE_REQUIRED 2
#endif

// Estrutura para configuração TLS
typedef struct {
    const char *cert;
    const char *key;
    const char *ca;
    const char *pass;
} tls_config;

// Funções TLS
extern int init_dialer_tls(nng_dialer d, const char *cacert, const char *cert, const char *key, const char *pass);
extern void loadfile(const char *path, void **datap, size_t *lenp);

#endif



int configurar_dialer(nng_socket *sock, nng_dialer *dialer, const char *url, 
                     tls_config *tls, bool verbose);

#define MAX_STR_LEN 30
#define CONN 1
#define SUB 2
#define PUB 3
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#define BILLION 1000000000

// Estrutura para parâmetros do Redis
typedef struct {
    const char *value;
    const char *redis_key;
} RedisParams;

typedef struct {
    nng_socket *sock;
    const char *topic;
    uint8_t qos;
    uint32_t interval_ms;
    uint32_t num_packets;
    const char *redis_key;
    int msg_sent; // Flag to indicate message sent
} publisher_args_t;


int configurar_dialer(nng_socket *sock, nng_dialer *dialer, const char *url, 
                     tls_config *tls, bool verbose);

// Funções para manipulação de tempo
extern struct timespec tempo_atual_timespec(void);
extern char *tempo_para_varchar(void);
extern long long diferenca_tempo(struct timespec tempo1, struct timespec tempo2);
extern char *diferenca_para_varchar(long long diferenca);
extern struct timespec string_para_timespec(char *tempo_varchar);



// Funções para Redis
extern void store_in_redis(const char *value, const char *redis_key);
extern void *store_in_redis_async(void *params);
extern void store_in_redis_async_call(const char *value, const char *redis_key);

// Função de erro fatal
extern void fatal(const char *msg, int rv);

// Operacao de publicacao
extern int publish_operation(nng_socket *sock,const char *topic, int qos, const char *operation, int val1, int val2, int *msg_sent);
// Funções MQTT
extern nng_msg *mqtt_msg_compose(int type, int qos, char *topic, char *payload);
extern void subscription(nng_socket *sock, const char *topic, const char *qos);


void print80(const char *prefix, const char *str, size_t len, bool quote);
extern int publish_operation_tcp(nng_socket sock, const char *topic, uint8_t qos, const char *operation, int val1, int val2, bool verbose);

// Função para resolver a operação
extern int resolve_operation(const char *operation, int val1, int val2);
// Redundante, mas mantida para compatibilidade
extern int publish_operation_resolved(nng_socket *sock,const char *topic, int qos, const char *operation, int val1, int val2, int *msg_sent);
// Callbacks MQTT
int msg_recv_cb(void *rmsg, void *arg);
/*
int connect_cb(void *rmsg, void *arg);
int disconnect_cb(void *rmsg, void *arg);
int msg_send_cb(void *rmsg, void *arg);
*/
// Variáveis globais externas
extern struct timespec start_time_rtt, end_time_rtt;
extern double time_connection;
extern const char *g_redis_key;


extern int keepRunning;
// Manipulação de sinais. Lembrando que ele manipula a variável keepRunning
extern void intHandler(int dummy);

#endif // COMMON_H
