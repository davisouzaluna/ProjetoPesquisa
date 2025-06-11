// Author: eeff <eeff at eeff dot dev>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "nng/mqtt/mqtt_client.h"
#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"
#include <pthread.h>

#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#define BILLION 1000000000

// Subcommand
#define PUBLISH "pub"

double time_connection;

void fatal(const char *msg, int rv) {
    fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}




long long tempo_atual_nanossegundos() {
    struct timespec tempo_atual;
    clock_gettime(CLOCK_REALTIME, &tempo_atual);

    // Converter segundos para nanossegundos e adicionar nanossegundos
    return tempo_atual.tv_sec * BILLION + tempo_atual.tv_nsec;
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

int keepRunning = 1;
void intHandler(int dummy) {
    keepRunning = 0;
    fprintf(stderr, "\nclient exit(0).\n");
    exit(0);
}

// Connect to the given address.
int client_connect(nng_socket *sock, nng_dialer *dialer, const char *url, bool verbose) {
    int rv;

    if ((rv = nng_mqtt_client_open(sock)) != 0) {
        fatal("nng_socket", rv);
    }

    if ((rv = nng_dialer_create(dialer, *sock, url)) != 0) {
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
    double elapsed = seconds + nanoseconds*1e-9;
    time_connection = elapsed;
    printf("Time taken to connect: %.9f seconds\n", elapsed);

    return (0);
}

int publish_operation(nng_socket sock, const char *topic, uint8_t qos, const char *operation, int val1, int val2, bool verbose) {
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

    if (verbose) {
        uint8_t print[1024] = {0};
        nng_mqtt_msg_dump(pubmsg, print, 1024, true);
        printf("%s\n", print);
    }

    printf("Publishing to '%s' with payload '%s'...\n", topic, payload);

    // Enviar a mensagem
    if ((rv = nng_sendmsg(sock, pubmsg, NNG_FLAG_NONBLOCK)) != 0) {
        fatal("nng_sendmsg", rv);
    }

    return rv;
}



// Publish a message to the given topic and with the given QoS.
int client_publish(nng_socket sock, const char *topic, uint8_t qos, bool verbose) {
    int rv;

    // Criar uma mensagem PUBLISH
    nng_msg *pubmsg;
    nng_mqtt_msg_alloc(&pubmsg, 0);
    nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
    nng_mqtt_msg_set_publish_dup(pubmsg, 0);
    nng_mqtt_msg_set_publish_qos(pubmsg, qos);
    nng_mqtt_msg_set_publish_retain(pubmsg, 0);

    char payload[64];

    // Gerar o timestamp atual e usá-lo como payload
    char *tempo_atual_varchar = tempo_para_varchar();
    nng_mqtt_msg_set_publish_payload(pubmsg, (uint8_t *)tempo_atual_varchar, strlen(tempo_atual_varchar));

    // Definir o tópico da mensagem
    nng_mqtt_msg_set_publish_topic(pubmsg, topic);

    if (verbose) {
        uint8_t print[1024] = {0};
        nng_mqtt_msg_dump(pubmsg, print, 1024, true);
        printf("%s\n", print);
    }

    printf("Publishing to '%s' with timestamp '%s'...\n", topic, tempo_atual_varchar);

    // Enviar a mensagem
    if ((rv = nng_sendmsg(sock, pubmsg, NNG_FLAG_NONBLOCK)) != 0) {
        fatal("nng_sendmsg", rv);
    }

    // Liberar a memória alocada para o timestamp
    free(tempo_atual_varchar);

    return rv;
}


struct pub_params {
    nng_socket *sock;
    const char *topic;
    uint8_t qos;
    bool verbose;
    uint32_t interval;
};

void publish_cb(void *args) {
    struct pub_params *params = args;

    do {
        // Chama client_publish que agora gera o payload com o timestamp
        client_publish(*params->sock, params->topic, params->qos, params->verbose);

        // Espera o intervalo especificado antes de publicar novamente, se interval > 0
        if (params->interval > 0) {
            nng_msleep(params->interval); // Usa a função de sono da NNG para esperar
        }
    } while (params->interval > 0 && keepRunning); // Continua enquanto interval for positivo

    printf("thread_exit\n");
}


int main(const int argc, const char **argv) {
    nng_socket sock;
    nng_dialer dialer;

    if (argc < 6 || 0 != strcmp(argv[1], PUBLISH)) {
        goto error;
    }
    const char *url = argv[2];
    uint8_t qos = atoi(argv[3]);
    const char *topic = argv[4];
    uint32_t interval_ms = atoi(argv[5]);
    uint32_t num_packets = argc > 6 ? atoi(argv[6]) : 1; 

    int rv = 0;
    char *verbose_env = getenv("VERBOSE");
    bool verbose = (verbose_env != NULL && strcmp(verbose_env, "1") == 0);

    printf("verbose is %d\n", verbose);
    client_connect(&sock, &dialer, url, verbose);

    // Setar a flag para tratamento de interrupção de saída com Ctrl+C
    signal(SIGINT, intHandler);

    /*Parte do código onde é enviado os pacotes somente com um intervalo de tempo entre cada(laço infinito) 

    while (keepRunning) {
        client_publish(sock, topic, qos, verbose);

        if (interval_ms > 0) {
            nng_msleep(interval_ms); // Espera o intervalo especificado antes de publicar novamente
        }
    }
    */
   const char *operacoes[] = {"add", "sub", "mul", "div"};
   for (uint32_t i = 0; i < num_packets && keepRunning; i++) {
        //client_publish(sock, topic, qos, verbose);
        int val1_rand = (rand() % 100) + 1;
        int val2_rand = (rand() % 100) + 1;
        const char *operacao_aleatoria = operacoes[rand() % 4];
        publish_operation(sock, topic, qos, operacao_aleatoria, val1_rand, val2_rand, verbose);

        if (interval_ms > 0) {
            nng_msleep(interval_ms); // Espera o intervalo especificado antes de publicar novamente
        }
        printf("Time taken to connect: %.9f seconds\n", time_connection);
    }
    printf("Time taken to connect: %.9f seconds\n", time_connection);

    if ((rv = nng_close(sock)) != 0) {
        fatal("nng_close", rv);
    }
    printf("Time taken to connect: %.9f seconds\n", time_connection);
    return 0;

error:
    fprintf(stderr, "Usage: %s %s url qos topic interval_ms [num_packets]\n", argv[0], PUBLISH);
    return 1;
}