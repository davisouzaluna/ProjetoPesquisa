#include "common.h"


const char *g_redis_key = "valores";
void fatal(const char *msg, int rv)
{
    fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

void store_in_redis(const char *value, const char *redis_key)
{
    // Conectar ao servidor Redis na porta 6379 (padrão)
    redisContext *context = redisConnect("127.0.0.1", 6379);
    if (context == NULL || context->err) {
        if (context) {
            fprintf(stderr, "Erro ao conectar ao Redis: %s\n", context->errstr);
            redisFree(context);
        } else {
            fprintf(stderr, "Não foi possível alocar contexto Redis\n");
        }
        return;
    }

    // Usar g_redis_key se redis_key for NULL
    const char *key = redis_key ? redis_key : g_redis_key;
    
    redisReply *reply = redisCommand(context, "RPUSH %s \"%s\"", key, value);
    if (reply == NULL) {
        fprintf(stderr, "Erro ao executar comando Redis\n");
        redisFree(context);
        return;
    }
    
    printf("Valor salvo com sucesso no Redis: %s, na chave: %s\n", value, key);
    freeReplyObject(reply);
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
        free(params);
        exit(EXIT_FAILURE);
    }

    pthread_detach(thread);
}

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

    char *tempo_varchar = (char *)malloc(MAX_STR_LEN * sizeof(char));
    if (tempo_varchar == NULL)
    {
        perror("Erro ao alocar memória");
        exit(EXIT_FAILURE);
    }

    snprintf(tempo_varchar, MAX_STR_LEN, "%ld.%09ld", tempo_atual.tv_sec, tempo_atual.tv_nsec);
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

// Variáveis globais
struct timespec start_time_rtt, end_time_rtt;
double time_connection;

nng_msg *mqtt_msg_compose(int type, int qos, char *topic, char *payload)
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);

    switch (type)
    {
    case CONN:
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
        nng_mqtt_msg_set_connect_proto_version(msg, 4);
        nng_mqtt_msg_set_connect_keep_alive(msg, 60);
        nng_mqtt_msg_set_connect_clean_session(msg, true);
        break;

    case PUB:
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
        nng_mqtt_msg_set_publish_dup(msg, 0);
        nng_mqtt_msg_set_publish_qos(msg, qos);
        nng_mqtt_msg_set_publish_retain(msg, 0);
        nng_mqtt_msg_set_publish_topic(msg, topic);
        nng_mqtt_msg_set_publish_payload(msg, (uint8_t *)payload, strlen(payload));
        break;

    case SUB:
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_SUBSCRIBE);

        nng_mqtt_topic_qos subscriptions[] = {
            {.qos = qos,
             .topic = {
                 .buf = (uint8_t *)topic,
                 .length = strlen(topic)}},
        };
        int count = sizeof(subscriptions) / sizeof(nng_mqtt_topic_qos);

        nng_mqtt_msg_set_subscribe_topics(msg, subscriptions, count);
    }

    return msg;
}

void subscription(nng_socket *sock, const char *topic, const char *qos)
{
    int q = qos ? atoi(qos) : 0;
    nng_msg *msg = mqtt_msg_compose(SUB, q, (char *)topic, NULL);
    if (msg == NULL)
    {
        printf("Failed to compose subscribe message.\n");
        return;
    }
    int rv = nng_sendmsg(*sock, msg, NNG_FLAG_ALLOC);
    if (rv != 0)
    {
        printf("Failed to send subscribe message: %d\n", rv);
        nng_msleep(1000); // esperar um segundo para tentar novamente
    }
}

int connect_cb(void *rmsg, void *arg)
{
    printf("[Connected][%s]...\n", (char *)arg);
    return 0;
}

int disconnect_cb(void *rmsg, void *arg)
{
    printf("[Disconnected][%s]...\n", (char *)arg);
    return 0;
}

int msg_recv_cb(void *rmsg, void *arg)
{
    struct timespec tempo_sub = tempo_atual_timespec();
    printf("[Msg Arrived][%s]...\n", (char *)arg);
    nng_msg *msg = rmsg;
    uint32_t topicsz, payloadsz;

    char *topic = (char *)nng_mqtt_msg_get_publish_topic(msg, &topicsz);
    char *payload = (char *)nng_mqtt_msg_get_publish_payload(msg, &payloadsz);

    clock_gettime(CLOCK_REALTIME, &end_time_rtt);

    struct timespec tempo_pub = string_para_timespec(payload);
    long long diferenca = diferenca_tempo(end_time_rtt, start_time_rtt);
    char *valor_redis = diferenca_para_varchar(diferenca);

    printf("topic   => %.*s\npayload => %.*s\n", topicsz, topic, payloadsz, payload);
    store_in_redis_async_call(valor_redis, NULL);
    return 0;
}



int msg_send_cb(void *rmsg, void *arg)
{
    printf("[Msg Sent][%s]...\n", (char *)arg);
    *((int *)arg) = 1;
    return 0;
}


int publish_operation(nng_socket *sock,const char *topic, int qos, const char *operation, int val1, int val2, int *msg_sent)
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

    if ((rv = nng_sendmsg(*sock, msg, NNG_FLAG_NONBLOCK)) != 0) {
        fatal("nng_sendmsg", rv);
        return rv;
    }

    while (!(*msg_sent)) {
        nng_msleep(1);
    }

    *msg_sent = 0;
    return 0;
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



int publish_operation_resolved(nng_socket *sock,const char *topic, int qos, const char *operation, int val1, int val2, int *msg_sent) {
    int rv;

    int resultado = 0;
    char payload[64];  // buffer para resultado em texto

    // Executa a operação
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

    nng_msg *msg = mqtt_msg_compose(PUB, qos, (char *)topic, payload);
    if (msg == NULL) {
        printf("Erro ao compor mensagem publish.\n");
        return -1;
    }

    // Enviar a mensagem
    rv = nng_sendmsg(*sock, msg, NNG_FLAG_NONBLOCK);
    if (rv != 0) {
        fatal("nng_sendmsg", rv);
        return rv;
    }


    

    return 0;
}

int configurar_dialer(nng_socket *sock, nng_dialer *dialer, const char *url, 
                     tls_config *tls, bool verbose) {
    int rv;

    // Criar dialer
    if ((rv = nng_dialer_create(dialer, *sock, url)) != 0) {
        printf("Erro ao criar dialer: %s\n", nng_strerror(rv));
        return rv;
    }

    // Configurar TLS se necessário
    if (tls != NULL && strstr(url, "tls+mqtt") != NULL) {
        nng_tls_config *cfg;
        
        if ((rv = nng_tls_config_alloc(&cfg, NNG_TLS_MODE_CLIENT)) != 0) {
            printf("Erro na configuração TLS: %s\n", nng_strerror(rv));
            return rv;
        }

        if (tls->cert != NULL && tls->key != NULL) {
            nng_tls_config_auth_mode(cfg, NNG_TLS_AUTH_MODE_REQUIRED);
            if ((rv = nng_tls_config_own_cert(cfg, tls->cert, tls->key, tls->pass)) != 0) {
                nng_tls_config_free(cfg);
                return rv;
            }
        }

        if (tls->ca != NULL) {
            if ((rv = nng_tls_config_ca_chain(cfg, tls->ca, NULL)) != 0) {
                nng_tls_config_free(cfg);
                return rv;
            }
        }

        rv = nng_dialer_set_ptr(*dialer, NNG_OPT_TLS_CONFIG, cfg);
        nng_tls_config_free(cfg);
        
        if (rv != 0) {
            return rv;
        }
    }

    // Criar e configurar mensagem CONNECT
    nng_msg *connmsg;
    if ((rv = nng_mqtt_msg_alloc(&connmsg, 0)) != 0) {
        return rv;
    }

    // Configurar mensagem CONNECT
    nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
    nng_mqtt_msg_set_connect_proto_version(connmsg, 4);
    nng_mqtt_msg_set_connect_keep_alive(connmsg, 60);
    nng_mqtt_msg_set_connect_clean_session(connmsg, true);

    // Gerar client_id único
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "client_%ld", time(NULL));
    nng_mqtt_msg_set_connect_client_id(connmsg, client_id);

    // Configurar mensagem CONNECT no dialer
    if ((rv = nng_dialer_set_ptr(*dialer, NNG_OPT_MQTT_CONNMSG, connmsg)) != 0) {
        nng_msg_free(connmsg);
        return rv;
    }

    if (verbose) {
        printf("Dialer configurado para %s\n", url);
    }

    return 0;
}

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


int publish_operation_tcp(nng_socket sock, const char *topic, uint8_t qos, const char *operation, int val1, int val2, bool verbose) {
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
