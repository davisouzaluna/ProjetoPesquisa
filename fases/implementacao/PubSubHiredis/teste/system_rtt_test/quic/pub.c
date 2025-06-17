#define _POSIX_C_SOURCE 199309L
#include <unistd.h>
#include <sys/time.h>
#include <nng/nng.h>
#include <nng/mqtt/mqtt_client.h>
#include <nng/mqtt/mqtt_quic.h>
#include "msquic.h"
#include "../common.h"

#include <time.h>

#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

static nng_socket g_sock;
const char * g_topic; //topico padrao para se inscrever
const char * g_qos;
int g_count = 0; //contador para saber se é uma possível reconexão(quando o callback de conectado for chamado novamente ele incrementa o valor em 1 e entra dentro de u if que se subscreve novamente no tópico)	
int g_type;
char g_alternative_topic[200];

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



static int disconnect_cb(void *rmsg, void *arg)
{
    printf("[Disconnected][%s]...\n", (char *)arg);
    is_reconnecting = 1; // Set the flag to indicate reconnection is needed
    return 0;
}


static int msg_send_cb(void *rmsg, void *arg)
{
    printf("[Msg Sent][%s]...\n", (char *)arg);
    // Use arg to indicate when the message is sent(eh so uma variavel estranha)
    *((int *)arg) = 1;
    return 0;
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


/*
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
*/
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
    
    char qos_str[2];
    snprintf(qos_str, sizeof(qos_str), "%d", q);
	snprintf(g_alternative_topic, sizeof(g_alternative_topic), "%s/resultado", g_topic);
    subscription(&sock, g_alternative_topic, qos_str);
    if (numero_pub) {
        for (i = 0; i < qpub; i++) {
            int val1_rand = (rand() % 100) + 1;
            int val2_rand = (rand() % 100) + 1;
            const char *operacao_aleatoria = operacoes[rand() % 4];
            //publish(topic, q, &msg_sent);
            publish_operation(&sock,topic, q, operacao_aleatoria, val1_rand, val2_rand, &msg_sent);
            nng_msleep(intervalo);
        }
    }


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