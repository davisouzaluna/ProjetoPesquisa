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
#include "../common.h"


#include "msquic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>



static nng_socket  g_sock;
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

//callback do subscriber
static int
msg_recv_cb_subscriber(void *rmsg, void * arg)
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
		   publish_operation_resolved(&g_sock,response_topic, 0, oper, v1, v2, &flag);
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
	    0 != nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb_subscriber, (void *)arg) ||
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