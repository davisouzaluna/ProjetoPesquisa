//
// The application has two sub-commands: `conn` and `sub`.
// The `conn` sub-command connects to the server.
// The `sub` sub-command subscribes to the given topic filter and blocks
// waiting for incoming messages.
//
// # Example:
//
// Connect to the specific server:
// ```
// $ ./sub conn 'mqtt-quic://127.0.0.1:14567'
// ```
//
// Subscribe to `topic` and waiting for messages and save the difference between the time of the message and the time of the subscription in Redis:
// ```
// $ ./sub sub "mqtt-quic://127.0.0.1:14567" 0 topic
// ```
//

#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>
#include <nng/mqtt/mqtt_quic.h>
#include <nng/mqtt/mqtt_client.h>
#include <hiredis/hiredis.h>
#include <time.h>
#include <sys/time.h>


#include "msquic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


#define MAX_STR_LEN 30
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#define PUB 3
#define BILLION 1000000000
#define CONN 1
#define SUB 2

static nng_socket  g_sock;
const char * g_redis_key; //chave padrao para salvar no redis
const char * g_topic; //topico padrao para se inscrever
const char * g_qos;
int g_count = 0; //contador para saber se é uma possível reconexão(quando o callback de conectado for chamado novamente ele incrementa o valor em 1 e entra dentro de u if que se subscreve novamente no tópico)	
int g_type;

conf_quic config_user = {
	.tls = {
		.enable = false,
		.cafile = "",
		.certfile = "",
		.keyfile  = "",
		.key_password = "",
		.verify_peer = true,
		.set_fail = true,
	},
	.multi_stream = false,
	.qos_first  = false,
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

//retorna o temppo de nanosegundos em long long
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

// Função para calcular a diferença entre dois tempos em nanossegundos
long long diferenca_tempo(struct timespec tempo1, struct timespec tempo2) {
    long long diff_sec = (long long)(tempo1.tv_sec - tempo2.tv_sec);
    long long diff_nsec = (long long)(tempo1.tv_nsec - tempo2.tv_nsec);
    return diff_sec * 1000000000LL + diff_nsec;
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




static nng_msg *
mqtt_msg_compose(int type, int qos, char *topic, char *payload)
{
	// Mqtt connect message
	nng_msg *msg;
	nng_mqtt_msg_alloc(&msg, 0);

	if (type == CONN) {
		nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);

		nng_mqtt_msg_set_connect_proto_version(msg, 4);
		nng_mqtt_msg_set_connect_keep_alive(msg, 30);
		nng_mqtt_msg_set_connect_clean_session(msg, true);
	} else if (type == SUB) {
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
	else if (type == PUB) {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
        nng_mqtt_msg_set_publish_dup(msg, 0);
        nng_mqtt_msg_set_publish_qos(msg, qos);
        nng_mqtt_msg_set_publish_retain(msg, 0);
        nng_mqtt_msg_set_publish_topic(msg, topic);
        nng_mqtt_msg_set_publish_payload(msg, (uint8_t *)payload, strlen(payload));
    } 
	return msg;
}

void subscription(nng_socket *sock, const char *topic, const char *qos) {
	int q;
	q =atoi(qos);
    nng_msg *msg = mqtt_msg_compose(SUB, q, (char *)topic, NULL);
    if (msg == NULL) {
        printf("Failed to compose subscribe message.\n");
        return;
    }
    int rv = nng_sendmsg(*sock, msg, NNG_FLAG_ALLOC);
    if (rv != 0) {
        printf("Failed to send subscribe message: %d\n", rv);
		nng_msleep(1000);//esperar umm segundo para tentar novamente
    } else {
        //printf("Successfully subscribed to topic: %s\n", topic);
    }
	//return rv,msg;
}



static int
connect_cb(void *rmsg, void * arg)
{
	printf("[Connected][%s]...\n", (char *)arg);

	//Se o contador for 1 e o type for um subscriber
	if((g_count >= 1) && (g_type == SUB)){

		subscription(&g_sock, g_topic, g_qos);
	}
	g_count ++;
	
	return 0;
}

static int
disconnect_cb(void *rmsg, void * arg)
{
	printf("[Disconnected][%s]...\n", (char *)arg);
	
	return 0;
	/*
	static int retry_count = 0;
	printf("[Disconnected][%s]...\n", (char *)arg);
	return 0;
	printf("tentando reconexao...\n");

	int backoff_time = (1 << retry_count); //Algoritmo de exponencial backoff
	// Reenviar a mensagem de conexão
	nng_msg *msg = mqtt_msg_compose(CONN, 0, NULL);
    nng_sendmsg(*g_sock, msg, NNG_FLAG_ALLOC);

	nng_msleep(backoff_time * 1000); //Esperar um tempo exponencialmente crescente	
	retry_count++;
    // Reinscrever-se no tópico
    //subscription(g_sock, g_topic, g_qos);

    return 0;
	*/
}

static int
msg_send_cb(void *rmsg, void * arg)
{
	printf("[Msg Sent][%s]...\n", (char *)arg);
	
	return 0;
}



int publish_operation_resolved(const char *topic, int qos, const char *operation, int val1, int val2, int *msg_sent) {
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
    rv = nng_sendmsg(g_sock, msg, NNG_FLAG_NONBLOCK);
    if (rv != 0) {
        fatal("nng_sendmsg", rv);
        return rv;
    }


    

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




//callback do subscriber
static int
msg_recv_cb(void *rmsg, void * arg)
{	
	int q = atoi(g_qos);

	struct timespec tempo_sub;
	tempo_sub = tempo_atual_timespec();
	printf("[Msg Arrived][%s]...\n", (char *)arg);
	nng_msg *msg = rmsg;
	uint32_t topicsz, payloadsz;

	char *topic   = (char *)nng_mqtt_msg_get_publish_topic(msg, &topicsz);
	char *payload = (char *)nng_mqtt_msg_get_publish_payload(msg, &payloadsz);

	

   
	printf("topic   => %.*s\n"
	       "payload => %.*s\n",topicsz, topic, payloadsz, payload);
		   char oper[16];
		   int v1, v2;
		   if (sscanf(payload, "%15s %d %d", oper, &v1, &v2) != 3) {
			   fprintf(stderr, "Erro: payload inválido. Esperado: <op> <val1> <val2>\n");
			   return 0;
		   }
	   
		   // Construir tópico de resposta
		   char response_topic[256];
		   snprintf(response_topic, sizeof(response_topic), "%.*s/resultado", topicsz, topic);
	   
		   // Flag de confirmação de envio
		   int flag = 0;
	   
		   // Chamar publish com a operação, valores e tópico de resultado
		   publish_operation_resolved(response_topic, 0, oper, v1, v2, &flag);
	//store_in_redis(valor_redis, g_redis_key);
	//store_in_redis_async_call(valor_redis, g_redis_key);
	nng_msg_free(msg);
	return 0;
	
}



int
client(int type, const char *url, const char *qos, const char *topic, const char *redis_key)
{
	nng_socket  sock;
	int         rv, sz, q;
	nng_msg *   msg;
	const char *arg = "CLIENT FOR QUIC";
	g_topic = topic;
	g_qos  =qos;
	g_type = type;


	/*
	// Open a quic socket without configuration
	if ((rv = nng_mqtt_quic_client_open(&sock, url)) != 0) {
		printf("error in quic client open.\n");
	}
	*/

	if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &config_user)) != 0) {
		printf("error in quic client open.\n");
	}


	if (0 != nng_mqtt_quic_set_connect_cb(&sock, connect_cb, (void *)arg) ||
	    0 != nng_mqtt_quic_set_disconnect_cb(&sock, disconnect_cb, (void *)arg) ||
	    0 != nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb, (void *)arg) ||
	    0 != nng_mqtt_quic_set_msg_send_cb(&sock, msg_send_cb, (void *)arg)) {
		printf("error in quic client cb set.\n");
	}
	g_sock = sock;



	if (qos) {
		q = atoi(qos);
		if (q < 0 || q > 2) {
			printf("Qos should be in range(0~2).\n");
			q = 0;
		}
	}

	switch (type) {
	case CONN:
				// MQTT Connect...
		msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
	nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
		break;
	case SUB:
			
        g_redis_key= redis_key; //aqui eu defino o valor da variavel global para que o callback possa acessar
		msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
		nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
		subscription(&sock, topic, qos);
		printf("subscrito em : %s\n", topic);
		
			

		
		break;
		

    
	default:
		printf("Unknown command.\n");
	}

	for (;;){

		//caso queira testar a reconexão do subscriber(envio da mensagem subscriber ao broker) é só descomentar abaixo

		/*nng_msleep(1000); //inserindo um tempo de 100ms para o subscriber para fazer com que a função ocorra de forma correta

		//if (type == SUB) {
			//subscription(&sock, topic, q);//chamada para o subscriber
		}*/
	}
	//nng_msleep(1000);
	nng_close(sock);
	fprintf(stderr, "Done.\n");

	return (0);
}

static void
printf_helper(char *exec)
{
    fprintf(stderr, "Uso: %s conn <url>\n"
                    "     %s sub  <url> <qos> <topic> [<redis_key>]\n", exec, exec);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	int rc;

	if (argc < 3) {
		goto error;
	}
	if (0 == strncmp(argv[1], "conn", 4) && argc == 3) {
		client(CONN, argv[2], NULL, NULL,NULL);
	}
	else if (strncmp(argv[1], "sub", 3) == 0 && argc >= 5 && argc <= 6) {
        const char *redis_key = argc == 6 ? argv[5] : NULL; // Se o argumento opcional do Redis key for fornecido
        return client(SUB, argv[2], argv[3], argv[4], redis_key); // Chamada para comando de subscrição
	}
    else {
		goto error;
	}

	return 0;

error:

	printf_helper(argv[0]);
	return 0;
}