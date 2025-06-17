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

#endif // COMMON_H
