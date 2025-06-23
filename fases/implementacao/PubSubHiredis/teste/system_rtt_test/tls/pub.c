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

#include <getopt.h>

#include <time.h>
#include <errno.h>

#include "../common.h"



pthread_t pub_thread;
extern struct timespec start_time_rtt, end_time_rtt;
//int keepRunning = 1;

#define PUBLISH "pub"


static void disconnect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    int reason = 0;
    nng_pipe_get_int(p, NNG_OPT_MQTT_DISCONNECT_REASON, &reason);
    printf("%s: disconnected! RC [%d] \n", __FUNCTION__, reason);


}

static void connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    int reason;
    nng_pipe_get_int(p, NNG_OPT_MQTT_CONNECT_REASON, &reason);
    printf("%s: connected! RC [%d] \n", __FUNCTION__, reason);
    (void) ev;
    (void) arg;
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


int tls_client(const char *url, uint8_t proto_ver, const char *ca, const char *cert, const char *key, const char *pass, const char *topic, uint8_t qos, bool verbose, uint32_t interval, uint32_t num_packets, const char *redis_key)
{
    nng_socket sock;
    nng_dialer dialer;
    int rv;
    int msg_sent = 0;

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

    tls_config tls_cfg = {
        .cert = cert,
        .key = key,
        .ca = ca,
        .pass = pass,
    };

    nng_mqtt_set_connect_cb(sock, connect_cb, &sock);
    nng_mqtt_set_disconnect_cb(sock, disconnect_cb, NULL);

    // Criar mensagem CONNECT
    nng_msg *msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    if (msg == NULL) {
        fatal("mqtt_msg_compose", NNG_ENOMEM);
    }

    if ((rv = configurar_dialer(&sock, &dialer, url, &tls_cfg, verbose)) != 0) {
        fatal("configurar_dialer", rv);
    }

    // Configurar mensagem CONNECT no dialer
    if ((rv = nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, msg)) != 0) {
        fatal("nng_dialer_set_ptr", rv);
    }


    // Marcar o início do tempo
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);

    
    
    if ((rv = nng_dialer_start(dialer, NNG_FLAG_ALLOC)) != 0)
    {
        fatal("nng_dialer_start", rv);
    }
    //enviando connect pkt
    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);

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
        .sock = &sock,
        .topic = topic,
        .qos = qos,
        .interval_ms = interval,
        .num_packets = num_packets,
        .redis_key = redis_key,
        .msg_sent = msg_sent,
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
