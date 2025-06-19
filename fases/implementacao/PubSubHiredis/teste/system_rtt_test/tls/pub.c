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

#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#define BILLION 1000000000

double time_connection;
#define PUBLISH "pub"
struct timespec start_time_rtt, end_time_rtt;

void fatal(const char *msg, int rv)
{
    fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

typedef struct
{
    const char *value;
    const char *redis_key;
} RedisParams;

typedef struct {
    nng_socket sock;
    nng_dialer dialer;
    const char *url;
} reconnect_info;


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
    if ((rv = nng_dialer_start((info->dialer), NNG_FLAG_NONBLOCK)) != 0) {
        fprintf(stderr, "nng_dialer_start: %s\n", nng_strerror(rv));
    } else {
        printf("Reconnection successful.\n");
    }
}

static void connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    int reason;
    nng_pipe_get_int(p, NNG_OPT_MQTT_CONNECT_REASON, &reason);
    printf("%s: connected! RC [%d] \n", __FUNCTION__, reason);
    (void) ev;
    (void) arg;
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

    // create a CONNECT message
    /* CONNECT */
    nng_msg *connmsg;
    nng_mqtt_msg_alloc(&connmsg, 0);
    nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
    nng_mqtt_msg_set_connect_proto_version(connmsg, 4);
    nng_mqtt_msg_set_connect_keep_alive(connmsg, 60);
    nng_mqtt_msg_set_connect_user_name(connmsg, "nng_mqtt_client");
    nng_mqtt_msg_set_connect_password(connmsg, "secrets");
    nng_mqtt_msg_set_connect_will_msg(connmsg, (uint8_t *)"bye-bye", strlen("bye-bye"));
    nng_mqtt_msg_set_connect_will_topic(connmsg, "will_topic");
    nng_mqtt_msg_set_connect_clean_session(connmsg, true);

    printf("Connecting to server ...\n");
    // Marcar o início do tempo
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);

    nng_dialer_set_ptr(*dialer, NNG_OPT_MQTT_CONNMSG, connmsg);
    nng_dialer_start(*dialer, NNG_FLAG_NONBLOCK);
    clock_gettime(CLOCK_REALTIME, &end_time);

    // Calcular a diferença
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long nanoseconds = end_time.tv_nsec - start_time.tv_nsec;
    double elapsed = seconds + nanoseconds * 1e-9;
    time_connection = elapsed;
    printf("Time taken to connect: %.9f seconds\n", elapsed);

    return (0);
}

int publish_operation(nng_socket sock, const char *topic, uint8_t qos, const char *operation, int val1, int val2, bool verbose)
{
    int rv;

    // Criar uma mensagem PUBLISH
    nng_msg *pubmsg;
    nng_mqtt_msg_alloc(&pubmsg, 0);
    nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
    nng_mqtt_msg_set_publish_dup(pubmsg, 0);
    nng_mqtt_msg_set_publish_qos(pubmsg, qos);
    nng_mqtt_msg_set_publish_retain(pubmsg, 0);

    char payload[64];
    snprintf(payload, sizeof(payload), "%s %d %d", operation, val1, val2);

    // Definir o payload da mensagem
    nng_mqtt_msg_set_publish_payload(pubmsg, (uint8_t *)payload, strlen(payload));

    // Definir o tópico da mensagem
    nng_mqtt_msg_set_publish_topic(pubmsg, topic);

    if (verbose)
    {
        uint8_t print[1024] = {0};
        nng_mqtt_msg_dump(pubmsg, print, 1024, true);
        printf("%s\n", print);
    }

    printf("Publishing to '%s' with payload '%s'...\n", topic, payload);

    // Enviar a mensagem
    if ((rv = nng_sendmsg(sock, pubmsg, NNG_FLAG_NONBLOCK)) != 0)
    {
        fatal("nng_sendmsg", rv);
    }

    return rv;
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
typedef struct
{
    nng_socket sock;
    const char *topic;
    uint8_t qos;
    uint32_t interval_ms;
    uint32_t num_packets;
    const char *redis_key;
    bool verbose;
} publisher_args_t;

void *publisher_thread(void *arg)
{
    publisher_args_t *args = (publisher_args_t *)arg;
    const char *operacoes[] = {"add", "sub", "mul", "div"};
    for (uint32_t i = 0; i < args->num_packets && keepRunning; i++)
    {
        clock_gettime(CLOCK_REALTIME, &start_time_rtt);
        int val1 = (rand() % 100) + 1;
        int val2 = (rand() % 100) + 1;
        const char *op = operacoes[rand() % 4];

        int rv = publish_operation(args->sock, args->topic, args->qos, op, val1, val2, args->verbose);

        if (args->interval_ms > 0)
        {
            nng_msleep(args->interval_ms);
        }
    }
    printf("Time taken to connect: %.9f seconds\n", time_connection);

    return NULL;
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

int tls_client(const char *url, uint8_t proto_ver, const char *ca, const char *cert, const char *key, const char *pass, const char *topic, uint8_t qos, bool verbose, uint32_t interval, uint32_t num_packets, const char *redis_key)
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

    //connect msg
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
        printf("erro");
    }

    // Marcar o início do tempo
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);

    //enviando connect pkt
    nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, msg);
    if ((rv = nng_dialer_start(dialer, NNG_FLAG_ALLOC)) != 0)
    {
        fatal("nng_dialer_start", rv);
    }

    clock_gettime(CLOCK_REALTIME, &end_time);

    // Calcular a diferença
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long nanoseconds = end_time.tv_nsec - start_time.tv_nsec;
    double elapsed = seconds + nanoseconds * 1e-9;
    time_connection = elapsed;

    printf("Time taken to connect: %.9f seconds\n", elapsed);


        signal(SIGINT, intHandler);

    // Tópico para assinatura
    char topic_resultados[256];
    snprintf(topic_resultados, sizeof(topic_resultados), "%s/resultados", topic);

    // Inscrição assíncrona
    nng_mqtt_topic_qos subscriptions[] = {
        {
            .qos = qos,
            .topic = {
                .buf = (uint8_t *)topic_resultados,
                .length = strlen(topic_resultados),
            },
        },
    };

    nng_mqtt_client *client = nng_mqtt_client_alloc(sock, &send_callback, NULL, true);
    nng_mqtt_subscribe_async(client, subscriptions,
                             sizeof(subscriptions) / sizeof(nng_mqtt_topic_qos), NULL);

    // Iniciar thread de publicação
    pthread_t pub_thread;

    //struct pra facilitar a manipulacao de parametros
    publisher_args_t pub_args = {
        .sock = sock,
        .topic = topic,
        .qos = qos,
        .interval_ms = interval,
        .num_packets = num_packets,
        .redis_key = redis_key,
        .verbose = verbose,
    };

    nng_msleep(1000);
    pthread_create(&pub_thread, NULL, publisher_thread, &pub_args);
    //signal(SIGINT, intHandler);
    // Loop de recepção de mensagens(sub)
    while (keepRunning) {
        nng_msg *msg;
        uint8_t *payload;
        uint32_t payload_len;
        int rv = nng_recvmsg(sock, &msg, 0);
        if (rv != 0) {
            fatal("nng_recvmsg", rv);
            continue;
        }
        //pega o tempo final
        clock_gettime(CLOCK_REALTIME, &end_time_rtt);

        assert(nng_mqtt_msg_get_packet_type(msg) == NNG_MQTT_PUBLISH);
        payload = nng_mqtt_msg_get_publish_payload(msg, &payload_len);
        uint32_t topicsz;
        char *recv_topic = (char *)nng_mqtt_msg_get_publish_topic(msg, &topicsz);

        long segundos_pub = end_time_rtt.tv_sec - start_time_rtt.tv_sec;
        long nanosegundos_pub = end_time_rtt.tv_nsec - start_time_rtt.tv_nsec;
        double diff_pub = segundos_pub + nanosegundos_pub * 1e-9;

       long long diff_rtt=diferenca_tempo(end_time_rtt, start_time_rtt);


        // Conversão do tipo(double em varchar)
        char buf[64];
        sprintf(buf, "%.9f", diff_pub);
        printf("valor convertido: %s\n", buf);

        const char *diff = diferenca_para_varchar(diff_rtt);

        printf("topic   => %.*s\npayload => %.*s\n", topicsz, recv_topic, payload_len, payload);
        store_in_redis_async_call(diff, redis_key);
        nng_msg_free(msg);
    }

    pthread_join(pub_thread, NULL);
    if ((rv = nng_close(sock)) != 0) {
        fatal("nng_close", rv);
    }
    printf("Time taken to connect: %.9f seconds\n", time_connection);
    return 0;
}



void usage_definition()
{
    printf("Usage: ./pub_tls -u <URL> [options]\n");

    printf("    example: ./pub_tls -u tls+mqtt-tcp://broker.emqx.io:8883 -t teste_latencia_tls -q 0 -i 100 -n 10\n");
    printf("Options:\n");
    printf("  -u <URL>           Broker URL (required)\n");
    printf("  -a <cafile>        Path to CA file (optional)\n");
    printf("  -c <cert>          Path to client certificate file (optional)\n");
    printf("  -k <key>           Path to client private key file (optional)\n");
    printf("  -p <key_psw>       Password for the private key (optional)\n");
    printf("  -v <proto_ver>     MQTT protocol version (4 for v3.1.1, 5 for v5.0; default: 4)\n");
    printf("  -t <topic>         Topic to publish to (default: teste_davi)\n");
    printf("  -q <qos>           Quality of Service level (0, 1, or 2; default: 0)\n");
    printf("  -i <interval_ms>   Interval between messages in milliseconds (default: 1000)\n");
    printf("  -n <num_packets>   Number of packets to send (default: 1)\n");
    printf("  -r <redis_key>     Redis key name for storing results (default: valores)\n");
    printf("  -h                 Show this help message and exit\n");
}


int main(int argc, char const *argv[])
{
    signal(SIGINT, intHandler);
    signal(SIGTERM, intHandler);

    // TLS & conexão
    char *url = NULL;
    char *cafile = NULL;
    char *cert = NULL;
    char *key = NULL;
    char *key_psw = NULL;
    size_t file_len;
    uint8_t proto_ver = MQTT_PROTOCOL_VERSION_v311; // default 4

    // Publicação
    const char *topic = "teste_davi";
    uint8_t qos = 0;
    uint32_t interval = 1000;
    uint32_t num_packets = 1;
    const char *redis_key = "valores"; // valor padrão

    // Verbosidade
    char *verbose_env = getenv("VERBOSE");
    bool verbose = verbose_env && strlen(verbose_env) > 0;

    // Verifica se modo 'pub' foi especificado como primeiro argumento
    if (argc < 2 || strcmp(argv[1], "pub") != 0)
    {
        fprintf(stderr, "Usage: %s pub -u <URL> [-a <cafile>] [-c <cert>] [-k <key>] [-p <key_psw>] [-v <proto_ver>] [-t <topic>] [-q <qos>] [-i <interval_ms>] [-n <num_packets>] [-r <redis_key>]\n", argv[0]);
        return 1;
    }

    // Avança os argumentos em 1 (pula "pub")
    int opt;
    optind = 2;
    while ((opt = getopt(argc, (char *const *)argv, "a:c:k:u:p:v:t:q:i:n:r:")) != -1)
    {
        switch (opt)
        {
        case '?':
        case 'h':
            usage_definition();
            return 0;
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
        case 'i':
            interval = atoi(optarg);
            break;
        case 'n':
            num_packets = atoi(optarg);
            break;
        case 'r':
            redis_key = optarg;
            break;
        default:
            fprintf(stderr, "Invalid argument: '%c'\n", opt);
            usage_definition();
            return 1;
        }
    }

    if (url == NULL)
    {
        fprintf(stderr, "Error: URL is required.\n");
        usage_definition();
        return 1;
    }

    // Carrega certificados (se fornecidos)
    if (cafile != NULL)
        loadfile(cafile, (void **)&cafile, &file_len);
    if (cert != NULL)
        loadfile(cert, (void **)&cert, &file_len);
    if (key != NULL)
        loadfile(key, (void **)&key, &file_len);

    // Chamada ao cliente TLS com a chave redis
    if (tls_client(url, proto_ver, cafile, cert, key, key_psw, topic, qos, verbose, interval, num_packets, redis_key) != 0)
    {
        fprintf(stderr, "Error: tls_client\n");
        return 1;
    }
    else
    {
        printf("TLS client connected successfully\n");
        printf("Time taken to connect: %.9f seconds\n", time_connection);
    }

    return 0;
}
