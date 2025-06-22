#include "../common.h"
#include "nng/mqtt/mqtt_client.h"
#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"
#include <assert.h>
#include <stdint.h>

// Escolha uma das linhas abaixo conforme o protocolo desejado:

// Só TCP:
//cmake -DNNG_ENABLE_TLS=OFF -DNNG_ENABLE_QUIC=OFF ..

//Só TLS:
//cmake -DNNG_ENABLE_TLS=ON -DNNG_ENABLE_QUIC=OFF ..

//Só QUIC:
//cmake -DNNG_ENABLE_TLS=OFF -DNNG_ENABLE_QUIC=ON ..


#ifdef NNG_SUPP_QUIC
#include <nng/mqtt/mqtt_quic.h>
#include "msquic.h"


void connect_cb_quic(void *rmsg, void *arg)
{
printf("[Connected][%s]...\n", (char *)arg);    
}

void disconnect_cb_quic(void *rmsg, void *arg)
{
    printf("[Disconnected][%s]...\n", (char *)arg);
}

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


int quic_connect(const char *url, const char *redis_key)
{
    nng_socket sock;
    int rv;
    nng_msg *msg;

    if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &config_user)) != 0) {
        printf("Error in quic client open: %s\n", nng_strerror(rv));
        return rv;
    }

    if (0 != nng_mqtt_quic_set_connect_cb(&sock, connect_cb_quic, (void *)"QUIC Client") ||
        0 != nng_mqtt_quic_set_disconnect_cb(&sock, disconnect_cb_quic, (void *)"QUIC Client")) {
        printf("Error in setting callbacks: %s\n", nng_strerror(rv));
        return rv;
    }

    msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
    clock_gettime(CLOCK_REALTIME, &end_time);
    long long diff_rtt=diferenca_tempo(end_time, start_time);
    const char *diff = diferenca_para_varchar(diff_rtt);
    


    // Close the socket
    if ((rv = nng_close(sock)) != 0) {
        printf("Error closing socket: %s\n", nng_strerror(rv));
        return rv;
    }
    store_in_redis(diff, redis_key);
    return 0;
}


#endif // NNG_SUPP_QUIC

#ifdef NNG_SUPP_TLS

static void disconnect_cb_tls(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    int reason = 0;
    nng_pipe_get_int(p, NNG_OPT_MQTT_DISCONNECT_REASON, &reason);
    printf("%s: disconnected! RC [%d] \n", __FUNCTION__, reason);


}

static void connect_cb_tls(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    int reason;
    nng_pipe_get_int(p, NNG_OPT_MQTT_CONNECT_REASON, &reason);
    printf("%s: connected! RC [%d] \n", __FUNCTION__, reason);
    (void) ev;
    (void) arg;
}

int tls_connect(const char *url, const char *redis_key,
                const char *ca, const char *cert, const char *key, const char *pass)
{
    nng_socket sock;
    int rv;
    nng_dialer dialer;
    nng_msg *msg;
    bool verbose = getenv("VERBOSE") && strlen(getenv("VERBOSE")) > 0;

    if ((rv = nng_mqtt_client_open(&sock)) != 0)
        fatal("nng_socket", rv);

    tls_config tls_cfg = {
        .cert = cert,
        .key = key,
        .ca = ca,
        .pass = pass,
    };

    if (0 != nng_mqtt_set_connect_cb(sock, connect_cb_tls, (void *)"TLS Client") ||
        0 != nng_mqtt_set_disconnect_cb(sock, disconnect_cb_tls, (void *)"TLS Client")) {
        printf("Error setting callbacks: %s\n", nng_strerror(rv));
        return rv;
    }

    if ((rv = configurar_dialer(&sock, &dialer, url, &tls_cfg, verbose)) != 0) {
        fatal("configurar_dialer", rv);
    }

    msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    if ((rv = nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, msg)) != 0) {
        fatal("nng_dialer_set_ptr", rv);
    }

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    if ((rv = nng_dialer_start(dialer, NNG_FLAG_ALLOC)) != 0)
    {
        fatal("nng_dialer_start", rv);
    }

    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
    clock_gettime(CLOCK_REALTIME, &end_time);
    long long diff_rtt=diferenca_tempo(end_time, start_time);
    const char *diff = diferenca_para_varchar(diff_rtt);
    


    // Close the socket
    if ((rv = nng_close(sock)) != 0) {
        printf("Error closing socket: %s\n", nng_strerror(rv));
        return rv;
    }
    store_in_redis(diff, redis_key);
    return 0;
}


#endif // NNG_SUPP_TLS
static void connect_cb(nng_pipe pipe, nng_pipe_ev ev, void *arg)
{
    printf("[Connected][%s]...\n", (char *)arg);
}

static void disconnect_cb(nng_pipe pipe, nng_pipe_ev ev, void *arg)
{
    printf("[Disconnected][%s]...\n", (char *)arg);
}

int tcp_connect(const char *url, const char *redis_key)
{
    nng_dialer dialer;
    nng_socket sock;
    int rv;
    nng_msg *msg;

    char *verbose_env = getenv("VERBOSE");
    bool verbose = verbose_env && strlen(verbose_env) > 0;

    if ((rv = nng_mqtt_client_open(&sock)) != 0) {
        printf("Error opening MQTT client: %s\n", nng_strerror(rv));
        return rv;
    }

    if (0 != nng_mqtt_set_connect_cb(sock, connect_cb, (void *)"TCP Client") ||
        0 != nng_mqtt_set_disconnect_cb(sock, disconnect_cb, (void *)"TCP Client")) {
        printf("Error setting callbacks: %s\n", nng_strerror(rv));
        return rv;
    }

    msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    if ((rv = configurar_dialer(&sock, &dialer, url, NULL, false)) != 0) {
        fatal("configurar_dialer", rv);
    }
    

    // Configurar mensagem CONNECT no dialer
    if ((rv = nng_dialer_set_ptr(dialer, NNG_OPT_MQTT_CONNMSG, msg)) != 0) {
        fatal("nng_dialer_set_ptr", rv);
    }

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    if ((rv = nng_dialer_start(dialer, NNG_FLAG_NONBLOCK)) != 0) {
        fatal("nng_dialer_start", rv);
    }

    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
    

    clock_gettime(CLOCK_REALTIME, &end_time);

    long long diff_rtt=diferenca_tempo(end_time, start_time);
    const char *diff = diferenca_para_varchar(diff_rtt);
    


    // Close the socket
    if ((rv = nng_close(sock)) != 0) {
        printf("Error closing socket: %s\n", nng_strerror(rv));
        return rv;
    }
    store_in_redis(diff, redis_key);
    return 0;

    
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Uso: %s <protocolo> <ip> <quantidade_conexoes> [redis_key]\n", argv[0]);
        printf("  protocolo: tcp | tls | quic\n");
        printf("  ip: endereço do broker (ex: mqtt-tcp://127.0.0.1:1883)\n");
        printf("  quantidade_conexoes: número de conexões a serem feitas\n");
        printf("  redis_key: (opcional) chave para salvar no Redis (default: valores)\n");
        return 1;
    }
    signal(SIGINT, intHandler);
    const char *protocolo = argv[1];
    const char *ip = argv[2];
    int qtd = atoi(argv[3]);
    if (qtd < 1) qtd = 1;
    const char *redis_key = (argc >= 5) ? argv[4] : "valores";

    // TLS params (ajuste se necessário)
    const char *ca = NULL, *cert = NULL, *key = NULL, *pass = NULL;

    for (int i = 0; i < qtd; i++) {
        if (strcmp(protocolo, "tcp") == 0) {
            tcp_connect(ip, redis_key);
        }
#ifdef NNG_SUPP_TLS
        else if (strcmp(protocolo, "tls") == 0) {
            tls_connect(ip, redis_key, ca, cert, key, pass);
        }
#endif
#ifdef NNG_SUPP_QUIC
        else if (strcmp(protocolo, "quic") == 0) {
            quic_connect(ip, redis_key);
        }
#endif
        else {
            printf("Protocolo não suportado: %s\n", protocolo);
            return 1;
        }
    }

    return 0;
}