#define _POSIX_C_SOURCE 199309L
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

#include <time.h>

#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#include <hiredis/hiredis.h>
#include <pthread.h>

#define CONN 1
#define SUB 2
#define PUB 3

const char * g_redis_key; //chave padrao para salvar no redis
static nng_socket g_sock;
const char * g_topic; //topico padrao para se inscrever
const char * g_qos;
int g_count = 0; //contador para saber se é uma possível reconexão(quando o callback de conectado for chamado novamente ele incrementa o valor em 1 e entra dentro de u if que se subscreve novamente no tópico)	
int g_type;
struct timespec start_time_rtt, end_time_rtt;
char g_alternative_topic[200];

double time_connection;

// Variáveis para rastrear o estado de reconexão
static int is_reconnecting = 0;
static uint32_t reconnection_delay = 100; // Atraso em milissegundos(100 ms padrao)
//o delay de reconexao é definido como o dobro do intervalo de publicação

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
    .qconnect_timeout = 60,
    .qdiscon_timeout = 30,
    .qidle_timeout = 30,
};


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
    printf("Valor salvo com sucesso no Redis: %s ,na chave: %s\n", value, redis_key );
    freeReplyObject(reply);

    // Encerrar a conexão com o servidor Redis
    redisFree(context);
}


void* store_in_redis_async(void *params) {
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

static void
fatal(const char *msg, int rv)
{
	fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

static int disconnect_cb(void *rmsg, void *arg)
{
    printf("[Disconnected][%s]...\n", (char *)arg);
    is_reconnecting = 1; // Set the flag to indicate reconnection is needed
    return 0;
}
//retorna o tempo atual em timespec
struct timespec tempo_atual_timespec() {
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);

    return tempo_atual;
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

// Função para calcular a diferença entre dois tempos em nanossegundos
long long diferenca_tempo(struct timespec tempo1, struct timespec tempo2) {
    long long diff_sec = (long long)(tempo1.tv_sec - tempo2.tv_sec);
    long long diff_nsec = (long long)(tempo1.tv_nsec - tempo2.tv_nsec);
    return diff_sec * 1000000000LL + diff_nsec;
}


static int
msg_recv_cb(void *rmsg, void * arg)
{	
	struct timespec tempo_sub;
	tempo_sub = tempo_atual_timespec();
	printf("[Msg Arrived][%s]...\n", (char *)arg);
	nng_msg *msg = rmsg;
	uint32_t topicsz, payloadsz;

	char *topic   = (char *)nng_mqtt_msg_get_publish_topic(msg, &topicsz);
	char *payload = (char *)nng_mqtt_msg_get_publish_payload(msg, &payloadsz);

    clock_gettime(CLOCK_REALTIME, &end_time_rtt);
    
	struct timespec tempo_pub;
	long long diferenca;
	tempo_pub = string_para_timespec(payload);
	diferenca = diferenca_tempo(end_time_rtt, start_time_rtt);//tempo do sub é o mais recente
	char *valor_redis;
	valor_redis = diferenca_para_varchar(diferenca);

   
	printf("topic   => %.*s\n"
	       "payload => %.*s\n",topicsz, topic, payloadsz, payload);
	//store_in_redis(valor_redis, g_redis_key);
	store_in_redis_async_call(valor_redis, g_redis_key);
	return 0;
	
}


static int msg_send_cb(void *rmsg, void *arg)
{
    printf("[Msg Sent][%s]...\n", (char *)arg);
    // Use arg to indicate when the message is sent(eh so uma variavel estranha)
    *((int *)arg) = 1;
    return 0;
}

static nng_msg *mqtt_msg_compose(int type, int qos, char *topic, char *payload)
{
    // Mqtt connect message
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
    }  else if (type == SUB) {
		nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_SUBSCRIBE);

		nng_mqtt_topic_qos subscriptions[] = {
			{
				.qos   = qos,
				.topic = {
					.buf    = (uint8_t *) topic,
					.length = strlen(topic)
				}
			},
		};
		int count = sizeof(subscriptions) / sizeof(nng_mqtt_topic_qos);

		nng_mqtt_msg_set_subscribe_topics(msg, subscriptions, count);
	} 

    return msg;
}
void subscription(nng_socket sock, const char *topic, const char *qos) {
	int q;
    printf("debug");
	q =0;
    nng_msg *msg = mqtt_msg_compose(SUB, q, (char *)topic, NULL);
    if (msg == NULL) {
        printf("Failed to compose subscribe message.\n");
        return;
    }
    int rv = nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
    if (rv != 0) {
        printf("Failed to send subscribe message: %d\n", rv);
		nng_msleep(1000);//esperar umm segundo para tentar novamente
    } else {
        //printf("Successfully subscribed to topic: %s\n", topic);
    }
	//return rv,msg;
}
static int connect_cb(void *rmsg, void *arg)
{
    //Se o contador for 1 e o type for um subscriber
	if((g_count >= 1) && (g_type == SUB)){
        // Construir tópico de resposta
		
		snprintf(g_alternative_topic, sizeof(g_alternative_topic), "%s/resultado", g_topic);
	}
	g_count ++;
    if (is_reconnecting) {
        printf("[Reconnected][%s]...\n", (char *)arg);
        is_reconnecting = 0; // Reset reconnection flag
        printf("Waiting for %u milliseconds after reconnection...\n", reconnection_delay);
        nng_msleep(reconnection_delay); // Usar a variável de atraso
    } else {
        printf("[Connected][%s]...\n", (char *)arg);
    }
    return 0;
}

char *tempo_para_varchar()
{
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);

    char *tempo_varchar = (char *)malloc(MAX_STR_LEN * sizeof(char));
    if (tempo_varchar == NULL) {
        perror("Erro ao alocar memória");
        exit(EXIT_FAILURE);
    }

    snprintf(tempo_varchar, MAX_STR_LEN, "%ld.%09ld", tempo_atual.tv_sec, tempo_atual.tv_nsec);

    return tempo_varchar;
}


//Aqui é onde vai formalizar a mensagem de publish com a operação e os valores
int publish_operation(const char *topic, int qos, const char *operation, int val1, int val2, int *msg_sent)
{
    int rv;
    *msg_sent = 0;

    char payload[64];
    snprintf(payload, sizeof(payload), "%s %d %d", operation, val1, val2); 

    nng_msg *msg = mqtt_msg_compose(PUB, qos, (char *)topic, payload);
    if (msg == NULL) {
        printf("Erro ao compor mensagem publish.\n");
        return -1;
    }

    //tempo inicial. Guarda o tempo inicial para calcular o RTT
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

void publish(const char *topic, int q, int *msg_sent)
{
    
    *msg_sent = 0; // Reset the flag
	char *tempo_atual_varchar = tempo_para_varchar();
    nng_msg *msg = mqtt_msg_compose(PUB, q, (char *)topic, tempo_para_varchar());
    printf("Tempo atual em varchar: %s\n", tempo_atual_varchar);
    nng_sendmsg(g_sock, msg, NNG_FLAG_ALLOC);

    // Wait for the message to be sent
    while (!(*msg_sent)) {
        nng_msleep(1); // Sleep for a short duration to avoid busy waiting
    }
	*msg_sent = 0; // Reset the flag
    free(tempo_atual_varchar);
}

int client(int type, const char *url, const char *qos, const char *topic, const char *numero_pub, const char *interval, const char *redis_key)
{
    nng_socket sock;
    int rv, q, qpub, i;
    nng_msg *msg;
    const char *arg = "CLIENT FOR QUIC";
    uint32_t intervalo = atoi(interval);
    int msg_sent = 0; // Flag to indicate message sent
	reconnection_delay = intervalo*2; // Set reconnection delay to 2x the interval
    g_topic = topic;
	g_qos  =qos;
	g_type = type;
    g_redis_key = redis_key;

    qpub = atoi(numero_pub);

    if (qpub <= 0) {
        printf("Número de publicações inválido.\n");
        printf("Número de publicações deve ser maior que 0.\n");
        return 0;
    }

    if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &config_user)) != 0) {
        printf("error in quic client open.\n");
    }

    if (0 != nng_mqtt_quic_set_connect_cb(&sock, connect_cb, (void *)arg) ||
        0 != nng_mqtt_quic_set_disconnect_cb(&sock, disconnect_cb, (void *)arg) ||
        0 != nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb, (void *)arg) ||
        0 != nng_mqtt_quic_set_msg_send_cb(&sock, msg_send_cb, &msg_sent)) {
        printf("error in quic client cb set.\n");
    }
    g_sock = sock;

    msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
    clock_gettime(CLOCK_REALTIME, &end_time);
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long nanoseconds = end_time.tv_nsec - start_time.tv_nsec;
    double elapsed = seconds + nanoseconds * 1e-9;
    time_connection = elapsed;
    printf("Time taken to connect: %.9f seconds\n", elapsed);

    if (qos) {
        q = atoi(qos);
        if (q < 0 || q > 2) {
            printf("Qos should be in range(0~2).\n");
            q = 0;
        }
    }
    const char *operacoes[] = {"add", "sub", "mul", "div"};
    int topicsz = strlen(topic);
    
	snprintf(g_alternative_topic, sizeof(g_alternative_topic), "%s/resultado", g_topic);
    subscription(sock, g_alternative_topic, q);
    if (numero_pub) {
        for (i = 0; i < qpub; i++) {
            int val1_rand = (rand() % 100) + 1;
            int val2_rand = (rand() % 100) + 1;
            const char *operacao_aleatoria = operacoes[rand() % 4];
            //publish(topic, q, &msg_sent);
            publish_operation(topic, q, operacao_aleatoria, val1_rand, val2_rand, &msg_sent);
            nng_msleep(intervalo);
        }
    }


#if defined(NNG_SUPP_SQLITE)
    sqlite_config(&sock, MQTT_PROTOCOL_VERSION_v311);
#endif

    printf("terminou o envio\n");
    printf("Time taken to connect: %.9f seconds\n", time_connection);
    nng_close(sock);
    fprintf(stderr, "Done.\n");

    return (0);
}

static void printf_helper(char *exec)
{
    fprintf(stderr, "Usage: %s <url> <qos> <topic> <num_packets> <interval> <redis_key>\n", exec);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    srand(time(NULL));
    if (argc < 5) {
        goto error;
    }

    client(PUB, argv[1], argv[2], argv[3], argv[4], argv[5], argv[6] ? argv[6] : "valores");

    return 0;

error:
    printf_helper(argv[0]);
    return 0;
}