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
#define SUBSCRIBE "sub"

//int keepRunning = 1;

typedef struct
{
    nng_socket sock;
    nng_dialer dialer;
    const char *url;
} reconnect_info;

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

    //enviando connect pkt
    
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

    subscription(&sock, topic, qos);
    while (keepRunning) {
        nng_msg *msg;
        if ((rv = nng_recvmsg(sock, &msg, 0)) != 0) {
            fatal("nng_recvmsg", rv);
            continue;
        }

        // Processar mensagem recebida
        uint32_t topicsz, payload_len;
        char *recv_topic = (char *)nng_mqtt_msg_get_publish_topic(msg, &topicsz);
        uint8_t *payload = nng_mqtt_msg_get_publish_payload(msg, &payload_len);

        printf("Tópico => %.*s\nPayload => %.*s\n", topicsz, recv_topic, payload_len, payload);

        // Extrair operação e valores
        char oper[16];
        int v1, v2;
        if (sscanf((char *)payload, "%15s %d %d", oper, &v1, &v2) == 3) {
            // Resolver operação usando função do common
            int result = resolve_operation(oper, v1, v2);
            printf("Operação: %s, v1: %d, v2: %d, resultado: %d\n", oper, v1, v2, result);

            // Criar tópico de resultado
            char result_topic[256];
            snprintf(result_topic, sizeof(result_topic), "%.*s/resultados", topicsz, recv_topic);

            // Publicar resultado
            char result_payload[64];
            snprintf(result_payload, sizeof(result_payload), "%d", result);

            nng_msg *pubmsg;
            if ((rv = nng_mqtt_msg_alloc(&pubmsg, 0)) == 0) {
                nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
                nng_mqtt_msg_set_publish_topic(pubmsg, result_topic);
                nng_mqtt_msg_set_publish_qos(pubmsg, qos);
                nng_mqtt_msg_set_publish_payload(pubmsg, result_payload, strlen(result_payload));

                if ((rv = nng_sendmsg(sock, pubmsg, 0)) != 0) {
                    printf("Erro ao publicar no tópico '%s': %s\n", 
                           result_topic, nng_strerror(rv));
                    nng_msg_free(pubmsg);
                } else {
                    printf("Publicado com sucesso: %s = %s\n", result_topic, result_payload);
                }
            }
        }

        nng_msg_free(msg);
    }

    nng_close(sock);
    return 0;
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
