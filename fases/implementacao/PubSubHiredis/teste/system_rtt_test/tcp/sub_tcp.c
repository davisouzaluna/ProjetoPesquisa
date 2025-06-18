#include <assert.h>
#include <stdint.h>
#include "../common.h"

int keepRunning = 1;
// Callback para quando recebe mensagem
static void send_callback(nng_mqtt_client *client, nng_msg *msg, void *arg)
{
    if (msg == NULL) return;
    
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

int main(const int argc, const char **argv)
{
    nng_socket sock;
    nng_dialer dialer;
    int rv;

    if (argc < 5 || argc > 6 || strcmp(argv[1], "sub") != 0) {
        fprintf(stderr, "Usage: %s sub <URL> <QOS> <TOPIC> [REDIS_KEY]\n", argv[0]);
        return 1;
    }

    const char *url = argv[2];
    uint8_t qos = atoi(argv[3]);
    const char *topic = argv[4];
    const char *redis_key = (argc == 6) ? argv[5] : "valores";
    bool verbose = getenv("VERBOSE") && strlen(getenv("VERBOSE")) > 0;

    // Abrir socket MQTT
    if ((rv = nng_mqtt_client_open(&sock)) != 0) {
        fatal("nng_socket", rv);
    }

    // Configurar dialer usando função do common
    if ((rv = configurar_dialer(&sock, &dialer, url, NULL, verbose)) != 0) {
        fatal("configurar_dialer", rv);
    }

    // Iniciar dialer
    if ((rv = nng_dialer_start(dialer, NNG_FLAG_NONBLOCK)) != 0) {
        fatal("nng_dialer_start", rv);
    }

    // Configurar handler para SIGINT
    signal(SIGINT, intHandler);

    // Configurar subscrição
    nng_mqtt_topic_qos subscriptions[] = {
        {
            .qos = qos,
            .topic = {
                .buf = (uint8_t *)topic,
                .length = strlen(topic),
            },
        },
    };

    // Subscrição assíncrona
    nng_mqtt_client *client = nng_mqtt_client_alloc(sock, &send_callback, NULL, true);
    nng_mqtt_subscribe_async(client, subscriptions,
        sizeof(subscriptions) / sizeof(nng_mqtt_topic_qos), NULL);

    printf("Iniciando loop de recebimento:\n");
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