#!/bin/bash

if [ $# -lt 2 ]; then
    echo "Uso: $0 <qtd> <ip_broker> [redis_key]"
    exit 1
fi

QTD=$1
IP=$2
REDIS_KEY=${3:-valores}  # Usa "valores" como padrão se não informado

for i in $(seq 1 $QTD); do
    echo "Execução $i"
    ./build/connect tls "$IP" 1 "$REDIS_KEY"
    #sleep 1  # Opcional: aguarda 1 segundo entre execuções
done
#exemplo de uso:
# ./run_project.sh 10 tls+mqtt-tcp://broker.emqx.io:8883 minha_chave_redis