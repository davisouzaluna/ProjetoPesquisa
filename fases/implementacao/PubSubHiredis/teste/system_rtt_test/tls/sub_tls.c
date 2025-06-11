// Author: eeff <eeff at eeff dot dev>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

//
// This is a simple MQTT subscriber demonstration application.
//
// The application subscribes to the given topic filter and blocks
// waiting for incoming messages.
//
// # Example:
//
// Subscribe to `topic` with QoS `0` and wait for messages:
// ```
// $ ./mqtt_client_sub mqtt-tcp://127.0.0.1:1883 0 topic
// ```
//

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <getopt.h>
#include <hiredis/hiredis.h>
#include <time.h>
#include <errno.h>
#include <nng/nng.h>
#include <nng/supplemental/tls/tls.h>

#include "nng/mqtt/mqtt_client.h"
#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"

#define SUBSCRIBE "sub"

struct timespec start_time_rtt, end_time_rtt;
#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#define BILLION 1000000000

void fatal(const char *msg, int rv)
{
    fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}
typedef struct
{
    nng_socket sock;
    nng_dialer dialer;
    const char *url;
} reconnect_info;
typedef struct
{
    const char *value;
    const char *redis_key;
} RedisParams;

void store_in_redis(const char *value, const char *redis_key)
{
    // Conectar ao servidor Redis na porta 6379 (padrão)
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

    // Salvar o valor no Redis com a chave fornecida ou "valores" como padrão
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

    // Encerrar a conexão com o servidor Redis
    redisFree(context);
}

void *store_in_redis_async(void *params)
{
    RedisParams *redis_params = (RedisParams *)params;
    store_in_redis(redis_params->value, redis_params->redis_key);
    free(params); // Libera a memória alocada para os parametros
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
    params->value = value;
    params->redis_key = redis_key;

    if (pthread_create(&thread, NULL, store_in_redis_async, params) != 0)
    {
        perror("Erro ao criar a thread");
        free(params); // Libera a memória alocada em caso de falha na criação da thread
        exit(EXIT_FAILURE);
    }

    // opcional: Se não precisar esperar a thread terminar, você pode desanexá-la, por enquanto preferi deixar a thread rodando
    pthread_detach(thread);
}

long long tempo_atual_nanossegundos()
{
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);

    // Converter segundos para nanossegundos e adicionar nanossegundos
    return tempo_atual.tv_sec * BILLION + tempo_atual.tv_nsec;
}

// retorna o tempo atual em timespec
struct timespec tempo_atual_timespec()
{
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);

    return tempo_atual;
}

char *tempo_para_varchar()
{
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);

    // Convertendo o tempo para uma string legível
    char *tempo_varchar = (char *)malloc(MAX_STR_LEN * sizeof(char));
    if (tempo_varchar == NULL)
    {
        perror("Erro ao alocar memória");
        exit(EXIT_FAILURE);
    }

    snprintf(tempo_varchar, MAX_STR_LEN, "%ld.%09ld", tempo_atual.tv_sec, tempo_atual.tv_nsec);

    // Retornando a string de tempo
    return tempo_varchar;
}

long long diferenca_tempo(struct timespec tempo1, struct timespec tempo2)
{
    long long diff_sec = (long long)(tempo1.tv_sec - tempo2.tv_sec);
    long long diff_nsec = (long long)(tempo1.tv_nsec - tempo2.tv_nsec);
    return diff_sec * 1000000000LL + diff_nsec;
}
char *diferenca_para_varchar(long long diferenca)
{
    char *tempo_varchar = (char *)malloc(MAX_STR_LEN * sizeof(char));
    if (tempo_varchar == NULL)
    {
        perror("Erro ao alocar memória");
        exit(EXIT_FAILURE);
    }
    snprintf(tempo_varchar, MAX_STR_LEN, "%lld.%09lld", diferenca / 1000000000LL, diferenca % 1000000000LL);
    return tempo_varchar;
}

struct timespec string_para_timespec(char *tempo_varchar)
{
    struct timespec tempo;
    char *ponto = strchr(tempo_varchar, '.');
    if (ponto != NULL)
    {
        *ponto = '\0'; // separa os segundos dos nanossegundos
        tempo.tv_sec = atol(tempo_varchar);
        tempo.tv_nsec = atol(ponto + 1);
    }
    else
    {
        tempo.tv_sec = atol(tempo_varchar);
        tempo.tv_nsec = 0;
    }
    return tempo;
}

int keepRunning = 1;
void intHandler(int dummy)
{
    keepRunning = 0;
    fprintf(stderr, "\nclient exit(0).\n");
    exit(0);
}

// Print the given string limited to 80 columns.
void print80(const char *prefix, const char *str, size_t len, bool quote)
{
    size_t max_len = 80 - strlen(prefix) - (quote ? 2 : 0);
    char *q = quote ? "'" : "";
    if (len <= max_len)
    {
        // case the output fit in a line
        printf("%s%s%.*s%s\n", prefix, q, (int)len, str, q);
    }
    else
    {
        // case we truncate the payload with ellipses
        printf("%s%s%.*s%s...\n", prefix, q, (int)(max_len - 3), str, q);
    }
}

static void disconnect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    int reason = 0;
    nng_pipe_get_int(p, NNG_OPT_MQTT_DISCONNECT_REASON, &reason);
    printf("%s: disconnected! RC [%d] \n", __FUNCTION__, reason);

    reconnect_info *info = (reconnect_info *)arg;

    int rv;
    printf("Attempting to reconnect...\n");

    // Attempt to restart the dialer
    if ((rv = nng_dialer_start((info->dialer), NNG_FLAG_NONBLOCK)) != 0)
    {
        fprintf(stderr, "nng_dialer_start: %s\n", nng_strerror(rv));
    }
    else
    {
        printf("Reconnection successful.\n");
    }
}

static void connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    int reason;
    nng_pipe_get_int(p, NNG_OPT_MQTT_CONNECT_REASON, &reason);
    printf("%s: connected! RC [%d] \n", __FUNCTION__, reason);
    (void)ev;
    (void)arg;
}

int client_connect(nng_socket *sock, nng_dialer *dialer, const char *url, bool verbose)
{
    int rv;

    if ((rv = nng_mqtt_client_open(sock)) != 0)
    {
        fatal("nng_socket", rv);
    }

    if ((rv = nng_dialer_create(dialer, *sock, url)) != 0)
    {
        fatal("nng_dialer_create", rv);
    }

    nng_msg *connmsg;
    nng_mqtt_msg_alloc(&connmsg, 0);
    nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
    nng_mqtt_msg_set_connect_proto_version(connmsg, 4);
    nng_mqtt_msg_set_connect_keep_alive(connmsg, 60);
    nng_mqtt_msg_set_connect_user_name(connmsg, "nng_mqtt_client");
    nng_mqtt_msg_set_connect_password(connmsg, "secrets");
    nng_mqtt_msg_set_connect_clean_session(connmsg, true);

    nng_mqtt_set_connect_cb(*sock, connect_cb, sock);
    nng_mqtt_set_disconnect_cb(*sock, disconnect_cb, connmsg);

    if (verbose)
    {
        uint8_t buff[1024] = {0};
        nng_mqtt_msg_dump(connmsg, buff, sizeof(buff), true);
        printf("%s\n", buff);
    }

    printf("Connecting to server ...\n");
    nng_dialer_set_ptr(*dialer, NNG_OPT_MQTT_CONNMSG, connmsg);
    nng_dialer_start(*dialer, NNG_FLAG_NONBLOCK);

    return 0;
}

static void send_callback(nng_mqtt_client *client, nng_msg *msg, void *arg)
{
    if (msg == NULL)
        return;

    uint32_t count;
    uint8_t *code;

    switch (nng_mqtt_msg_get_packet_type(msg))
    {
    case NNG_MQTT_SUBACK:
        code = nng_mqtt_msg_get_suback_return_codes(msg, &count);
        printf("SUBACK reason codes are ");
        for (int i = 0; i < count; ++i)
            printf("%d \n", code[i]);
        printf("\n");
        break;
    default:
        printf("Sending in async way is done.\n");
        break;
    }
    nng_msg_free(msg);
}

// metodos TLS

void loadfile(const char *path, void **datap, size_t *lenp)
{
    FILE *f;
    size_t total_read = 0;
    size_t allocation_size = BUFSIZ;
    char *fdata;
    char *realloc_result;

    if ((f = fopen(path, "rb")) == NULL)
    {
        fprintf(stderr, "Cannot open file %s: %s", path, strerror(errno));
        exit(1);
    }

    if ((fdata = malloc(allocation_size + 1)) == NULL)
    {
        fprintf(stderr, "Out of memory.");
    }

    while (1)
    {
        total_read += fread(fdata + total_read, 1, allocation_size - total_read, f);
        if (ferror(f))
        {
            if (errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "Read from %s failed: %s", path, strerror(errno));
            exit(1);
        }
        if (feof(f))
        {
            break;
        }
        if (total_read == allocation_size)
        {
            if (allocation_size > SIZE_MAX / 2)
            {
                fprintf(stderr, "Out of memory.");
            }
            allocation_size *= 2;
            if ((realloc_result = realloc(fdata, allocation_size + 1)) == NULL)
            {
                free(fdata);
                fprintf(stderr, "Out of memory.");
                exit(1);
            }
            fdata = realloc_result;
        }
    }
    if (f != stdin)
    {
        fclose(f);
    }
    fdata[total_read] = '\0';
    *datap = fdata;
    *lenp = total_read;
}

int init_dialer_tls(nng_dialer d, const char *cacert, const char *cert, const char *key, const char *pass)
{
    nng_tls_config *cfg;
    int rv;

    if ((rv = nng_tls_config_alloc(&cfg, NNG_TLS_MODE_CLIENT)) != 0)
    {
        return (rv);
    }

    if (cert != NULL && key != NULL)
    {
        nng_tls_config_auth_mode(cfg, NNG_TLS_AUTH_MODE_REQUIRED);
        if ((rv = nng_tls_config_own_cert(cfg, cert, key, pass)) != 0)
        {
            goto out;
        }
    }
    else
    {
        nng_tls_config_auth_mode(cfg, NNG_TLS_AUTH_MODE_NONE);
    }

    if (cacert != NULL)
    {
        if ((rv = nng_tls_config_ca_chain(cfg, cacert, NULL)) != 0)
        {
            goto out;
        }
    }

    rv = nng_dialer_set_ptr(d, NNG_OPT_TLS_CONFIG, cfg);

out:
    nng_tls_config_free(cfg);
    return (rv);
}

int resolve_operation(const char *operation, int val1, int val2)
{

    int resultado = 0;
    char payload[64];

    if (strcmp(operation, "add") == 0)
    {
        resultado = val1 + val2;
    }
    else if (strcmp(operation, "sub") == 0)
    {
        resultado = val1 - val2;
    }
    else if (strcmp(operation, "mul") == 0)
    {
        resultado = val1 * val2;
    }
    else if (strcmp(operation, "div") == 0)
    {
        if (val2 == 0)
        {
            snprintf(payload, sizeof(payload), "Erro: divisao por zero");
        }
        else
        {
            resultado = val1 / val2;
        }
    }
    else
    {
        snprintf(payload, sizeof(payload), "Operacao invalida");
    }

    // Se não houve erro na divisao, monta o payload com o resultado
    if (strlen(payload) == 0)
    {
        snprintf(payload, sizeof(payload), "%d", resultado);
    }

    return resultado;
}

int tls_client(const char *url, uint8_t proto_ver, const char *ca, const char *cert, const char *key, const char *pass, const char *topic, uint8_t qos, bool verbose)
{
    nng_socket sock;
    nng_dialer dialer;
    int rv;

    if (proto_ver == MQTT_PROTOCOL_VERSION_v5)
    {
        if ((rv = nng_mqttv5_client_open(&sock)) != 0)
        {
            fatal("nng_socket", rv);
        }
    }
    else
    {
        if ((rv = nng_mqtt_client_open(&sock)) != 0)
        {
            fatal("nng_socket", rv);
        }
    }

    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);
    nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
    nng_mqtt_msg_set_connect_keep_alive(msg, 60);
    nng_mqtt_msg_set_connect_clean_session(msg, true);
    nng_mqtt_msg_set_connect_proto_version(msg, proto_ver);
    nng_mqtt_msg_set_connect_user_name(msg, "emqx");
    nng_mqtt_msg_set_connect_password(msg, "emqx123");

    nng_mqtt_set_connect_cb(sock, connect_cb, &sock);
    reconnect_info info = {sock, dialer, url};
    nng_mqtt_set_disconnect_cb(sock, disconnect_cb, NULL);

    if ((rv = nng_dialer_create(&dialer, sock, url)) != 0)
    {
        fatal("nng_dialer_create", rv);
    }

    if ((rv = init_dialer_tls(dialer, ca, cert, key, pass)) != 0)
    {
        fatal("init_dialer_tls", rv);
    }

    nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, msg);
    if ((rv = nng_dialer_start(dialer, NNG_FLAG_ALLOC)) != 0)
    {
        fatal("nng_dialer_start", rv);
    }

    nng_mqtt_topic_qos subscriptions[] = {
        {
            .qos = qos,
            .topic = {
                .buf = (uint8_t *)topic,
                .length = strlen(topic),
            },
        },
    };

    // Asynchronous subscription
    nng_mqtt_client *client = nng_mqtt_client_alloc(sock, &send_callback, NULL, true);
    nng_mqtt_subscribe_async(client, subscriptions,
                             sizeof(subscriptions) / sizeof(nng_mqtt_topic_qos), NULL);

    uint8_t buff[1024] = {0};
    printf("Start receiving loop:\n");
    while (keepRunning)
    {
        nng_msg *msg;
        uint8_t *payload;
        uint32_t payload_len;
        int rv;

        if ((rv = nng_recvmsg(sock, &msg, 0)) != 0)
        {
            fatal("nng_recvmsg", rv);
            continue;
        }

        assert(nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_PUBLISH);

        payload = nng_mqtt_msg_get_publish_payload(msg, &payload_len);

        uint32_t topicsz;
        char *recv_topic = (char *)nng_mqtt_msg_get_publish_topic(msg, &topicsz);

        printf("topic   => %.*s\npayload => %.*s\n", topicsz, recv_topic, payload_len, payload);

        char oper[16];
        int v1, v2;
        if (sscanf((char *)payload, "%15s %d %d", oper, &v1, &v2) != 3)
        {
            fprintf(stderr, "Erro: payload inválido. Esperado: <op> <val1> <val2>\n");
            nng_msg_free(msg);
            continue; // para continuar o loop, não sair do programa
        }

        int result = resolve_operation(oper, v1, v2);
        printf("Operação: %s, v1: %d, v2: %d, resultado: %d\n", oper, v1, v2, result);

        // Monta tópico "<topic>/resultados"
        char result_topic[256];
        snprintf(result_topic, sizeof(result_topic), "%.*s/resultados", topicsz, recv_topic);

        // Monta payload do resultado como string
        char result_payload[64];
        snprintf(result_payload, sizeof(result_payload), "%d", result);

        nng_msg *pubmsg;
        if (nng_mqtt_msg_alloc(&pubmsg, 0) != 0)
        {
            fprintf(stderr, "Erro ao alocar mensagem MQTT para publicação.\n");
            nng_msg_free(msg);
            continue;
        }

        nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
        nng_mqtt_msg_set_publish_topic(pubmsg, result_topic);
        nng_mqtt_msg_set_publish_qos(pubmsg, qos);
        nng_mqtt_msg_set_publish_payload(pubmsg, result_payload, strlen(result_payload));

        rv = nng_sendmsg(sock, pubmsg, 0);
        if (rv != 0)
        {
            fprintf(stderr, "Falha ao publicar no tópico '%s': %s\n", result_topic, nng_strerror(rv));
            nng_msg_free(pubmsg);
        }
        else
        {
            printf("Publicado com sucesso no tópico: %s\n", result_topic);
        }

        if (verbose)
        {
            memset(buff, 0, sizeof(buff));
            nng_mqtt_msg_dump(msg, buff, sizeof(buff), true);
            printf("%s\n", buff);
        }

        nng_msg_free(msg);
    }

    // nng_close(sock);
}

void usage()
{
    fprintf(stderr, "Usage: ./sub_tls [-u URL] [-a CAFILE] [-c CERT] [-k KEY] [-p PASS] [-v VERSION] [-t TOPIC] [-q  QOS] [-r REDIS_KEY]\n");
    exit(1);
}

void usage_definition() {
    fprintf(stderr, "Usage: sub_tls [-u URL] [-a CAFILE] [-c CERT] [-k KEY] [-p PASS] [-v VERSION] [-t TOPIC] [-q QOS]\n");
    fprintf(stderr, "  Example: ./sub_tls -u tls+mqtt-tcp://broker.emqx.io:8883\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -u URL      URL of the MQTT broker (use port 8883 and tls+mqtt-tcp protocol)\n");
    fprintf(stderr, "  -a CAFILE   CA certificate file\n");
    fprintf(stderr, "  -c CERT     Client certificate file\n");
    fprintf(stderr, "  -k KEY      Client private key file\n");
    fprintf(stderr, "  -p PASS     Password for the client private key\n");
    fprintf(stderr, "  -v VERSION  MQTT protocol version (default: 4)\n");
    fprintf(stderr, "  -t TOPIC    Topic to publish\n");
    fprintf(stderr, "  -q QOS      QoS level (default: 0)\n");
    fprintf(stderr, "  -h          Show this help message\n");
    exit(1);
}

int main(int argc, char const *argv[])
{
    char *path = NULL;
    size_t file_len;
    char *url = NULL;
    char *cafile = NULL;
    char *cert = NULL;
    char *key = NULL;
    char *key_psw = NULL;
    uint8_t proto_ver = MQTT_PROTOCOL_VERSION_v311; // default version is 4
    int opt;

    // Elements for latency test
    const char *topic = "teste_davi"; // tópico padrão
    uint8_t qos = 0; // default qos is 0
    bool verbose = false;

    signal(SIGINT, intHandler);
    signal(SIGTERM, intHandler);

    while ((opt = getopt(argc, (char *const *)argv, "a:c:k:u:p:v:t:q:")) != -1) {
        switch (opt) {
        case '?':
        case 'h':
            usage_definition();
            exit(0);
        case 'a':
            cafile = optarg;
            break;
        case 'c':
            cert = optarg;
            break;
        case 'k':
            key = optarg;
            break;
        case 'u':
            url = optarg;
            break;
        case 'p':
            key_psw = optarg;
            break;
        case 'v':
            proto_ver = atoi(optarg);
            break;
        case 't':
            topic = optarg;
            break;
        case 'q':
            qos = atoi(optarg);
            break;
        default:
            fprintf(stderr, "invalid argument: '%c'\n", opt);
            usage_definition();
            exit(1);
        }
    }

    if (url == NULL) {
        usage_definition();
        return 1;
    }

    if (cafile != NULL) {
        loadfile(cafile, (void **)&cafile, &file_len);
    }
    if (cert != NULL) {
        loadfile(cert, (void **)&cert, &file_len);
    }
    if (key != NULL) {
        loadfile(key, (void **)&key, &file_len);
    }

    if (tls_client(url, proto_ver, cafile, cert, key, key_psw, topic, qos, verbose) != 0) {
        fprintf(stderr, "Error: tls_client\n");
        return 1;
    }

    return 0;
}
