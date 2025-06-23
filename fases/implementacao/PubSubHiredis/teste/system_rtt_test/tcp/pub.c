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

#include "nng/mqtt/mqtt_client.h"
#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"
#include "../common.h"

#define SUBSCRIBE "sub"

#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif


#define PUBLISH "pub"

// Iniciar thread de publicação
pthread_t pub_thread;
extern struct timespec start_time_rtt, end_time_rtt;
//int keepRunning = 1;



void *publisher_thread(void *arg) {
    publisher_args_t *args = (publisher_args_t *)arg;
    const char *operacoes[] = {"add", "sub", "mul", "div"};
    
    for (uint32_t i = 0; i < args->num_packets && keepRunning; i++) {
        clock_gettime(CLOCK_REALTIME, &start_time_rtt);
        int val1 = (rand() % 100) + 1;
        int val2 = (rand() % 100) + 1;
        const char *op = operacoes[rand() % 4];

       //caso queira ver outros parametros de transporte, basta mudar o ultimo parametro para true(modo verboso)
        int rv = publish_operation_tcp(*(args->sock), args->topic, args->qos, 
                                 op, val1, val2, false);
                
        if (args->interval_ms > 0) {
            nng_msleep(args->interval_ms);
        }
    }


    return NULL;
}
static void disconnect_cb_tcp(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    printf("Disconnected!\n");
}

static void connect_cb_tcp(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    printf("Connected!\n");
}


int main(const int argc, const char **argv) {
    nng_socket sock;
    int rv;
    nng_dialer dailer;

    if (argc < 6 || argc > 8) {
        fprintf(stderr, "Usage: %s pub <URL> <QOS> <TOPIC> <INTERVAL_MS> [NUM_PACKETS] [REDIS_KEY]\n", argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    if (strcmp(mode, "pub") != 0) {
        fprintf(stderr, "Only 'pub' mode is supported in this version.\n");
        return 1;
    }

    const char *url = argv[2];
    uint8_t qos = atoi(argv[3]);
    const char *topic = argv[4];
    uint32_t interval_ms = atoi(argv[5]);
    uint32_t num_packets = (argc >= 7) ? atoi(argv[6]) : 1;
    const char *redis_key = (argc == 8) ? argv[7] : "valores";
    int msg_sent = 0;

    
    char *verbose_env = getenv("VERBOSE");
    bool verbose = verbose_env && strlen(verbose_env) > 0;

    // Primeiro, abrir o socket MQTT
    if ((rv = nng_mqtt_client_open(&sock)) != 0) {
        fatal("nng_socket", rv);
    }
    printf("Socket opened successfully.\n");

    // Configurar callbacks
    if (nng_mqtt_set_connect_cb(sock, connect_cb_tcp, NULL) != 0 ||
        nng_mqtt_set_disconnect_cb(sock, disconnect_cb_tcp, NULL) != 0) {
        fatal("nng_mqtt_set_cb", rv);
    }



    // Criar mensagem CONNECT
    nng_msg *msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    if (msg == NULL) {
        fatal("mqtt_msg_compose", NNG_ENOMEM);
    }

    // Criar e configurar dialer
    nng_dialer dialer;
    if ((rv = configurar_dialer(&sock, &dialer, url, NULL, verbose)) != 0) {
        fatal("configurar_dialer", rv);
    }

    // Configurar mensagem CONNECT no dialer
    if ((rv = nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, msg)) != 0) {
        fatal("nng_dialer_set_ptr", rv);
    }

    // Iniciar medição do tempo
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);

    // Iniciar dialer em modo não-bloqueante
    if ((rv = nng_dialer_start(dialer, NNG_FLAG_NONBLOCK)) != 0) {
        fatal("nng_dialer_start", rv);
    }

    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
    // Aguardar conexão ser estabelecida
    //nng_msleep(100);

    // Finalizar medição do tempo
    clock_gettime(CLOCK_REALTIME, &end_time);
    time_connection = (end_time.tv_sec - start_time.tv_sec) + 
                     (end_time.tv_nsec - start_time.tv_nsec) * 1e-9;
    
    printf("Tempo de conexão: %.9f segundos\n", time_connection);


    signal(SIGINT, intHandler);


    char qos_str[2];
    snprintf(qos_str, sizeof(qos_str), "%d", qos);
    // Tópico para assinatura
    char topic_resultados[256];
    snprintf(topic_resultados, sizeof(topic_resultados), "%s/resultados", topic);
    subscription(&sock, topic_resultados, qos_str);
    
    //struct pra facilitar a manipulacao de parametros
    publisher_args_t pub_args = {
        .sock = &sock,
        .topic = topic,
        .qos = qos,
        .interval_ms = interval_ms,
        .num_packets = num_packets,
        .redis_key = redis_key,
        .msg_sent = msg_sent,
    };

    nng_msleep(1000);
    pthread_create(&pub_thread, NULL, publisher_thread, &pub_args);
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