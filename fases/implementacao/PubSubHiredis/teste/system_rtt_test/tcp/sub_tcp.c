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
#include <hiredis/hiredis.h>
#include <time.h>

#include "nng/mqtt/mqtt_client.h"
#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"

#define SUBSCRIBE "sub"

#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#define BILLION 1000000000


void fatal(const char *msg, int rv)
{
    fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

typedef struct {
    const char *value;
    const char *redis_key;
} RedisParams;

void store_in_redis(const char *value, const char *redis_key) {
    // Conectar ao servidor Redis na porta 6379 (padrão)
    redisContext *context = redisConnect("127.0.0.1", 6379);
    if (context == NULL || context->err) {
        if (context) {
            printf("Erro na conexão com o Redis: %s\n", context->errstr);
            redisFree(context);
        } else {
            printf("Erro na alocação do contexto do Redis\n");
        }
        return;
    }

    printf("Conectado ao servidor Redis\n");

    // Salvar o valor no Redis com a chave fornecida ou "valores" como padrão
    const char *key = redis_key && *redis_key ? redis_key : "valores";
    redisReply *reply = redisCommand(context, "RPUSH %s \"%s\"", key, value);
    if (reply == NULL) {
        printf("Erro ao salvar os dados no Redis\n");
        redisFree(context);
        return;
    }
    printf("Valor salvo com sucesso no Redis: %s ,na chave: %s\n", value, redis_key);
    freeReplyObject(reply);

    // Encerrar a conexão com o servidor Redis
    redisFree(context);
}

void *store_in_redis_async(void *params) {
    RedisParams *redis_params = (RedisParams *)params;
    store_in_redis(redis_params->value, redis_params->redis_key);
    free(params); // Libera a memória alocada para os parametros
    return NULL;
}

void store_in_redis_async_call(const char *value, const char *redis_key) {
    pthread_t thread;
    RedisParams *params = malloc(sizeof(RedisParams));
    if (params == NULL) {
        perror("Erro ao alocar memória para os parâmetros da thread");
        exit(EXIT_FAILURE);
    }
    params->value = value;
    params->redis_key = redis_key;

    if (pthread_create(&thread, NULL, store_in_redis_async, params) != 0) {
        perror("Erro ao criar a thread");
        free(params); // Libera a memória alocada em caso de falha na criação da thread
        exit(EXIT_FAILURE);
    }

    // opcional: Se não precisar esperar a thread terminar, você pode desanexá-la, por enquanto preferi deixar a thread rodando
    pthread_detach(thread);
}

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

long long diferenca_tempo(struct timespec tempo1, struct timespec tempo2) {
    long long diff_sec = (long long)(tempo1.tv_sec - tempo2.tv_sec);
    long long diff_nsec = (long long)(tempo1.tv_nsec - tempo2.tv_nsec);
    return diff_sec * 1000000000LL + diff_nsec;
}
char *diferenca_para_varchar(long long diferenca) {
    char *tempo_varchar = (char *)malloc(MAX_STR_LEN * sizeof(char));
    if (tempo_varchar == NULL) {
        perror("Erro ao alocar memória");
        exit(EXIT_FAILURE);
    }
    snprintf(tempo_varchar, MAX_STR_LEN, "%lld.%09lld", diferenca / 1000000000LL, diferenca % 1000000000LL);
    return tempo_varchar;
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
    if (len <= max_len) {
        // case the output fit in a line
        printf("%s%s%.*s%s\n", prefix, q, (int)len, str, q);
    } else {
        // case we truncate the payload with ellipses
        printf("%s%s%.*s%s...\n", prefix, q, (int)(max_len - 3), str, q);
    }
}

static void disconnect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    printf("Disconnected!\n");
}

static void connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    printf("Connected!\n");
}

int client_connect(nng_socket *sock, nng_dialer *dialer, const char *url, bool verbose)
{
    int rv;

    if ((rv = nng_mqtt_client_open(sock)) != 0) {
        fatal("nng_socket", rv);
    }

    if ((rv = nng_dialer_create(dialer, *sock, url)) != 0) {
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

    if (verbose) {
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
    
    switch (nng_mqtt_msg_get_packet_type(msg)) {
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

int resolve_operation(const char *operation, int val1, int val2){

    int resultado = 0;
    char payload[64];

    if (strcmp(operation, "add") == 0) {
        resultado = val1 + val2;
    } else if (strcmp(operation, "sub") == 0) {
        resultado = val1 - val2;
    } else if (strcmp(operation, "mul") == 0) {
        resultado = val1 * val2;
    } else if (strcmp(operation, "div") == 0) {
        if (val2 == 0) {
            snprintf(payload, sizeof(payload), "Erro: divisao por zero");
        } else {
            resultado = val1 / val2;
        }
    } else {
        snprintf(payload, sizeof(payload), "Operacao invalida");
    }

    // Se não houve erro na divisao, monta o payload com o resultado
    if (strlen(payload) == 0) {
        snprintf(payload, sizeof(payload), "%d", resultado);
    }

    return resultado;
}

int main(const int argc, const char **argv)
{
    nng_socket sock;
    nng_dialer dailer;

    if (argc < 5 || argc > 6 || strcmp(argv[1], SUBSCRIBE) != 0) {
        fprintf(stderr, "Usage: %s sub <URL> <QOS> <TOPIC> [REDIS_KEY]\n", argv[0]);
        return 1;
    }
    const char *url = argv[2];
    uint8_t qos = atoi(argv[3]);
    const char *topic = argv[4];
    const char *redis_key = (argc == 6) ? argv[5] : "valores"; // Chave Redis opcional
    char *verbose_env = getenv("VERBOSE");
    bool verbose = verbose_env && strlen(verbose_env) > 0;

    client_connect(&sock, &dailer, url, verbose);

    signal(SIGINT, intHandler);

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
    while (keepRunning) {
    nng_msg *msg;
    uint8_t *payload;
    uint32_t payload_len;
    int rv;

    if ((rv = nng_recvmsg(sock, &msg, 0)) != 0) {
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
    if (sscanf((char *)payload, "%15s %d %d", oper, &v1, &v2) != 3) {
        fprintf(stderr, "Erro: payload inválido. Esperado: <op> <val1> <val2>\n");
        nng_msg_free(msg);
        continue;  // para continuar o loop, não sair do programa
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
if (nng_mqtt_msg_alloc(&pubmsg, 0) != 0) {
    fprintf(stderr, "Erro ao alocar mensagem MQTT para publicação.\n");
    nng_msg_free(msg);
    continue;
}

nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
nng_mqtt_msg_set_publish_topic(pubmsg, result_topic);
nng_mqtt_msg_set_publish_qos(pubmsg, qos);
nng_mqtt_msg_set_publish_payload(pubmsg, result_payload, strlen(result_payload));

rv = nng_sendmsg(sock, pubmsg, 0);
if (rv != 0) {
    fprintf(stderr, "Falha ao publicar no tópico '%s': %s\n", result_topic, nng_strerror(rv));
    nng_msg_free(pubmsg);
} else {
    printf("Publicado com sucesso no tópico: %s\n", result_topic);
}

    if (verbose) {
        memset(buff, 0, sizeof(buff));
        nng_mqtt_msg_dump(msg, buff, sizeof(buff), true);
        printf("%s\n", buff);
    }

    nng_msg_free(msg);
    }

}