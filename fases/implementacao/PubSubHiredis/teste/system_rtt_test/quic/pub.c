#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>
#include <nng/mqtt/mqtt_client.h>
#include <nng/mqtt/mqtt_quic.h>
#include "msquic.h"
#include <pthread.h>
#include <hiredis/hiredis.h>
#include <time.h>

#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#define BILLION 1000000000
static nng_socket g_sock;

#define CONN 1
#define SUB 2
#define PUB 3


//variavel pra controlar a espera da mensagem do subscriber(mutex)
volatile int msg_received = 0;

struct timespec start_time, end_time;
struct timespec start_time_pub, end_time_pub;

struct timespec start_time_rtt, end_time_rtt;
long long start_publish_time;
long long end_publish_time;
double time_connection;

conf_quic config_user = {
    .tls = {
        .enable = false,
        .cafile = "",
        .certfile = "",
        .keyfile = "",
        .key_password = "",
        .verify_peer = true,
        .set_fail = true,
    },
    .multi_stream = false,
    .qos_first = false,
    .qkeepalive = 30,
    .qconnect_timeout = 60, // pode ser alterado
    .qdiscon_timeout = 30,
    .qidle_timeout = 30,
};

typedef struct
{
    const char *value;
    const char *redis_key;
} RedisParams;

void fatal(const char *msg, int rv)
{
    fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

void store_in_redis(const char *value, const char *redis_key)
{
    redisContext *context = redisConnect("127.0.0.1", 6379);
    if (context == NULL || context->err)
    {
        if (context)
        {
            printf("Erro na conexão com o Redis: %s\n", context->errstr);
            redisFree(context);
        }
        else
        {
            printf("Erro na alocação do contexto do Redis\n");
        }
        return;
    }

    printf("Conectado ao servidor Redis\n");

    const char *key = redis_key && *redis_key ? redis_key : "valores";
    redisReply *reply = redisCommand(context, "RPUSH %s \"%s\"", key, value);
    if (reply == NULL)
    {
        printf("Erro ao salvar os dados no Redis\n");
        redisFree(context);
        return;
    }
    printf("Valor salvo com sucesso no Redis: %s ,na chave: %s\n", value, redis_key);
    freeReplyObject(reply);

    redisFree(context);
}

void *store_in_redis_async(void *params)
{
    RedisParams *redis_params = (RedisParams *)params;
    store_in_redis(redis_params->value, redis_params->redis_key);
    free((char *)redis_params->value);
    free((char *)redis_params->redis_key);
    free(params); // Libera a memória alocada para os parâmetros
    return NULL;
}

void store_in_redis_async_call(const char *value, const char *redis_key)
{
    pthread_t thread;
    RedisParams *params = malloc(sizeof(RedisParams));
    if (params == NULL)
    {
        perror("Erro ao alocar memória para os parâmetros da thread");
        exit(EXIT_FAILURE);
    }
    params->value = strdup(value);
    params->redis_key = strdup(redis_key);

    if (pthread_create(&thread, NULL, store_in_redis_async, params) != 0)
    {
        perror("Erro ao criar a thread");
        free((char *)params->value);
        free((char *)params->redis_key);
        free(params);
        exit(EXIT_FAILURE);
    }

    pthread_detach(thread);
}

static int connect_cb(void *rmsg, void *arg)
{
    printf("[Connected][%s]...\n", (char *)arg);
    return 0;
}

static int disconnect_cb(void *rmsg, void *arg)
{
    printf("[Disconnected][%s]...\n", (char *)arg);
    return 0;
}

static int msg_send_cb(void *rmsg, void *arg)
{
    clock_gettime(CLOCK_REALTIME, &end_time_pub);
    printf("[Msg Sent][%s]...\n", (char *)arg);
    *((int *)arg) = 1;
    return 0;
}

struct timespec string_para_timespec(char *tempo_varchar) {
    struct timespec tempo;
    char *ponto = strchr(tempo_varchar, '.');
    if (ponto != NULL) {
        *ponto = '\0'; // separa os segundos dos nanossegundos
        tempo.tv_sec = atol(tempo_varchar);
        tempo.tv_nsec = atol(ponto + 1);
    } else {
        tempo.tv_sec = atol(tempo_varchar);
        tempo.tv_nsec = 0;
    }
    return tempo;
}

//retorna o temppo de nanosegundos em long long
long long tempo_atual_nanossegundos() {
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);

    // Converter segundos para nanossegundos e adicionar nanossegundos


return tempo_atual.tv_sec * BILLION + tempo_atual.tv_nsec;
}

//retorna o tempo atual em timespec
struct timespec tempo_atual_timespec() {
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);

    return tempo_atual;
}


char *tempo_para_varchar() {
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);
    
    // Convertendo o tempo para uma string legível
    char *tempo_varchar = (char *)malloc(MAX_STR_LEN * sizeof(char));
    if (tempo_varchar == NULL) {
        perror("Erro ao alocar memória");
        exit(EXIT_FAILURE);
    }

    snprintf(tempo_varchar, MAX_STR_LEN, "%ld.%09ld", tempo_atual.tv_sec, tempo_atual.tv_nsec);

    // Retornando a string de tempo
    return tempo_varchar;
}

// Função para calcular a diferença entre dois tempos em nanossegundos
long long diferenca_tempo(struct timespec tempo1, struct timespec tempo2) {
    long long diff_sec = (long long)(tempo1.tv_sec - tempo2.tv_sec);
    long long diff_nsec = (long long)(tempo1.tv_nsec - tempo2.tv_nsec);
    return diff_sec * 1000000000LL + diff_nsec;
}

// Função para converter diferença de tempo em string
char *diferenca_para_varchar(long long diferenca) {
    char *tempo_varchar = (char *)malloc(MAX_STR_LEN * sizeof(char));
    if (tempo_varchar == NULL) {
        perror("Erro ao alocar memória");
        exit(EXIT_FAILURE);
    }
    snprintf(tempo_varchar, MAX_STR_LEN, "%lld.%09lld", diferenca / 1000000000LL, diferenca % 1000000000LL);
    return tempo_varchar;
}

// manter a conexao ativa
void enviar_pingreq(nng_socket sock)
{
    nng_msg *msg;
    int rv;

    if ((rv = nng_mqtt_msg_alloc(&msg, 0)) != 0)
    {
        fatal("nng_msg_alloc", rv);
    }

    nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PINGREQ);
    //nng_msg_set_cmd_type(msg, NNG_MQTT_PINGREQ);

    // Enviar pacote
    if ((rv = nng_sendmsg(sock, msg, 0)) != 0)
    {
        fatal("nng_sendmsg", rv);
    }

    printf(" PINGREQ enviado manualmente para manter a sessão ativa.\n");
}

static int msg_recv_cb(void *rmsg, void *arg)
{
    clock_gettime(CLOCK_REALTIME, &end_time_rtt);
    nng_msg *msg = rmsg;
    uint32_t payloadsz;
    char *payload = (char *)nng_mqtt_msg_get_publish_payload(msg, &payloadsz);

    char payload_str[256] = {0};
    memcpy(payload_str, payload, (payloadsz < 255) ? payloadsz : 255);
    payload_str[payloadsz] = '\0';

    printf("%s\n", payload_str);

    msg_received = 1;
    enviar_pingreq(g_sock);
    return 0;
}

static nng_msg *mqtt_msg_compose(int type, int qos, char *topic, char *payload) {
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);

    if (type == CONN) {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
        nng_mqtt_msg_set_connect_proto_version(msg, 4);
        nng_mqtt_msg_set_connect_keep_alive(msg, 30);
        nng_mqtt_msg_set_connect_clean_session(msg, true);
    } else if (type == PUB) {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
        nng_mqtt_msg_set_publish_dup(msg, 0);
        nng_mqtt_msg_set_publish_qos(msg, qos);
        nng_mqtt_msg_set_publish_retain(msg, 0);
        nng_mqtt_msg_set_publish_topic(msg, topic);
        nng_mqtt_msg_set_publish_payload(msg, (uint8_t *)payload, strlen(payload));
    }

    return msg;
}
//msm funcao, mas com um nome diferente
static nng_msg *
mqtt_msg_compose_subscribe(int type, int qos, char *topic)
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);
    if (type == SUB)
    {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_SUBSCRIBE);

        nng_mqtt_topic_qos subscriptions[] = {
            {.qos = qos,
             .topic = {
                 .buf = (uint8_t *)topic,
                 .length = strlen(topic)}},
        };
        int count = sizeof(subscriptions) / sizeof(nng_mqtt_topic_qos);

        nng_mqtt_msg_set_subscribe_topics(msg, subscriptions, count);
    }

    return msg;
}

void subscription(nng_socket *sock, const char *topic, const char *qos) {
	int q;
	q =atoi(qos);
    nng_msg *msg = mqtt_msg_compose_subscribe(SUB, q, (char *)topic);
    if (msg == NULL) {
        printf("Failed to compose subscribe message.\n");
        return;
    }
    int rv = nng_sendmsg(*sock, msg, NNG_FLAG_ALLOC);
    if (rv != 0) {
        printf("Failed to send subscribe message: %d\n", rv);
		nng_msleep(1000);//esperar umm segundo para tentar novamente
    } else {
        //printf("Successfully subscribed to topic: %s\n", topic);
    }
	//return rv,msg;
}

int publish(const char *topic, int qos, const char *operation, int val1, int val2, int *msg_sent)
{
    int rv;
    *msg_sent = 0;

    char payload[64];
    snprintf(payload, sizeof(payload), "%s %d %d", operation, val1, val2);  // <<< aqui está a mudança

    nng_msg *msg = mqtt_msg_compose(PUB, qos, (char *)topic, payload);
    if (msg == NULL) {
        printf("Erro ao compor mensagem publish.\n");
        return -1;
    }

    //tempo inicial
    clock_gettime(CLOCK_REALTIME, &start_time_rtt);

    if ((rv = nng_sendmsg(g_sock, msg, NNG_FLAG_NONBLOCK)) != 0) {
        fatal("nng_sendmsg", rv);
        return rv;
    }

    while (!(*msg_sent)) {
        nng_msleep(1);
    }

    *msg_sent = 0;
    return 0;
}


int client(int type, const char *url, const char *qos, const char *topic, const char *numero_pub, const char *interval, const char *redis_key)
{
    nng_socket sock;
    int rv, sz, q, qpub, i;
    nng_msg *msg;
    const char *arg = "CLIENT FOR QUIC";
    uint32_t intervalo = atoi(interval);
    int msg_sent = 0; // Flag to indicate message sent

    if (redis_key == NULL)
    {
        redis_key = "valores";
    }

    qpub = atoi(numero_pub);
    if (qpub <= 0)
    {
        printf("Número de publicações inválido.\n");
        printf("Número de publicações deve ser maior que 0.\n");
        return 0;
    }

    if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &config_user)) != 0)
    {
        printf("error in quic client open.\n");
    }

    if (0 != nng_mqtt_quic_set_connect_cb(&sock, connect_cb, (void *)arg) ||
        0 != nng_mqtt_quic_set_disconnect_cb(&sock, disconnect_cb, (void *)arg) ||
        0 != nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb, (void *)arg) ||
        0 != nng_mqtt_quic_set_msg_send_cb(&sock, msg_send_cb, &msg_sent))
    {
        printf("error in quic client cb set.\n");
    }
    g_sock = sock;

    // MQTT Connect...
    msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    clock_gettime(CLOCK_REALTIME, &start_time);
    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
    clock_gettime(CLOCK_REALTIME, &end_time);
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long nanoseconds = end_time.tv_nsec - start_time.tv_nsec;
    double elapsed = seconds + nanoseconds * 1e-9;
    time_connection = elapsed;
    printf("Time taken to connect: %.9f seconds\n", elapsed);

    if (qos)
    {
        q = atoi(qos);
        if (q < 0 || q > 2)
        {
            printf("Qos should be in range(0~2).\n");
            q = 0;
        }
    }


    //se subscrevendo primeiro
    char response_topic[256]; // buffer para armazenar o novo tópico
    snprintf(response_topic, sizeof(response_topic), "%s/resultado", topic);

    subscription(&sock,response_topic,qos);


    const char *operacoes[] = {"add", "sub", "mul", "div"};

    for (i = 0; i < qpub; i++)
    {
        int val1_rand = (rand() % 100) + 1;
        int val2_rand = (rand() % 100) + 1;
        const char *operacao_aleatoria = operacoes[rand() % 4];

        // Evita divisão por zero
        if (strcmp(operacao_aleatoria, "div") == 0 && val2_rand == 0)
        {
            val2_rand = 1;
        }

        //clock_gettime(CLOCK_REALTIME, &start_time_pub);
        publish(topic, q, operacao_aleatoria, val1_rand, val2_rand, &msg_sent);

        // Espera até o callback sinalizar que a mensagem chegou
        while (!msg_received) {
            nng_msleep(10);  // pausa 10ms para não travar a CPU
        }


        long segundos_pub = end_time_rtt.tv_sec - start_time_rtt.tv_sec;
        long nanosegundos_pub = end_time_rtt.tv_nsec - start_time_rtt.tv_nsec;
        double diff_pub = segundos_pub + nanosegundos_pub * 1e-9;

       long long diff_rtt=diferenca_tempo(end_time_rtt, start_time_rtt);


        // Conversão do tipo(double em varchar)
        char buf[64];
        sprintf(buf, "%.9f", diff_pub);
        printf("valor convertido: %s\n", buf);

        const char *diff = diferenca_para_varchar(diff_rtt);

        // Salvar no Redis
        store_in_redis_async_call(diff, redis_key);

        printf("latencia do publisher: %.9s segundos\n", diff);
        msg_received = 0;  // reseta a flag para próxima iteração

        
        nng_msleep(intervalo);
    }

    nng_close(sock);
    printf("terminou o envio\n");
    printf("Time taken to connect: %.9f seconds\n", time_connection);
    fprintf(stderr, "Done.\n");

    return 0;
}

static void printf_helper(char *exec)
{
    fprintf(stderr, "Usage: %s <url> <qos> <topic> <num_packets> <interval> <[redis-key]>\n", exec);
    fprintf(stderr, "url: the broker url\n");
    fprintf(stderr, "qos: qos level(0~2)\n");
    fprintf(stderr, "topic: the topic name\n");
    fprintf(stderr, "num_packets: the number of packet to publish\n");
    fprintf(stderr, "interval: interval between sending two messages (ms)\n");
    fprintf(stderr, "redis-key: Optional, Redis key to store the values\n");
}

int main(int argc, char **argv)
{
    srand(time(NULL));
    if (argc < 6)
    {
        printf_helper(argv[0]);
        return 0;
    }
    if (argc == 6)
    {
        client(2, argv[1], argv[2], argv[3], argv[4], argv[5], NULL);
    }
    else
    {
        client(2, argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
    }

    return 0;
}
