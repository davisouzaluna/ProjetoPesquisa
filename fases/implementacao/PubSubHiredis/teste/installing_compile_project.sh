#!/bin/bash

#verificar se um pacote está instalado
check_and_install_package() {
    package=$1
    if dpkg -l | grep -q "^ii  $package"; then
        echo "$package já está instalado."
    else
        echo "Instalando $package..."
        sudo apt-get install -y $package
    fi
}

# Atualiza a lista de pacotes do SO
echo "Atualizando a lista de pacotes..."
sudo apt-get update

# Dependências necessárias para o NanoSDK
dependencies=(
    cmake
    build-essential
    liblttng-ust-dev
    lttng-tools
    libssl-dev
    ninja-build
    libc6-dev
    gcc-multilib
    python3-pip
    libgconf2-dev
    ntp # para sincronização de tempo
)

# Verifica e instala cada dependência
for package in "${dependencies[@]}"; do
    check_and_install_package $package
done

# Instalar o Redis
if dpkg -l | grep -q "^ii  redis-server"; then
    echo "Redis já está instalado."
else
    echo "Instalando Redis..."
    sudo apt update
    sudo apt upgrade -y
    sudo apt install -y redis-server
fi

# Verifica o status do Redis
sudo systemctl status redis-server

echo "Instalação concluída."


is_compiled() {
    dir=$1
    binary_file=$2
    if [ -f "${dir}/build/${binary_file}" ]; then
        return 0
    else
        return 1
    fi
}

#Instalando o hiredis
if [ ! -d "./hiredis" ]; then
    echo "Clonando e instalando o hiredis..."
    git clone https://github.com/redis/hiredis.git
    cd hiredis
    mkdir build
    cd build
    cmake ..
    make && sudo make install
    cd ../..
else
    if is_compiled "./hiredis" "libhiredis.so"; then
        echo "hiredis já está compilado."
    else
        echo "Compilando o hiredis..."
        cd hiredis
        mkdir -p build
        cd build
        cmake ..
        make && sudo make install
        cd ../..
    fi
fi

#Instalando o mbedTLS
if [ ! -d "./mbedtls" ]; then
    echo "Clonando e instalando o mbedTLS..."
    git clone https://github.com/Mbed-TLS/mbedtls.git
    cd mbedtls
    git submodule update --init
    python3 -m pip install --user -r scripts/basic.requirements.txt
    mkdir build
    cd build
    cmake -DUSE_SHARED_MBEDTLS_LIBRARY=On .. #para a instalação de bibliotecas compartilhadas
    # Caso queira a instalação normal faça: cmake ..
    make && sudo make install
    cd ../..    
else
    if is_compiled "mbedtls" "/library/libmebdtls.so"; then
        echo "mbedTLS já está compilado."
    else
        echo "Compilando o mbedTLS..."
        cd mbedtls
        mkdir -p build
        cd build
        cmake -DUSE_SHARED_MBEDTLS_LIBRARY=On ..
        make && sudo make install
        cd ../..
    fi
fi


#Instalando o NanoSDK
if [ ! -d "./NanoSDK" ]; then
    echo "Clonando e instalando o NanoSDK..."
    git clone --branch v0.10.1 --depth 1 https://github.com/emqx/NanoSDK.git
    cd NanoSDK
    git submodule update --init --recursive 
    mkdir build && cd build
    cmake -G Ninja -DBUILD_SHARED_LIBS=OFF -DNNG_ENABLE_QUIC=ON ..
    ninja
    sudo ninja install
    cd ../..
else
    if is_compiled "./NanoSDK" "libnng.a"; then
        echo "NanoSDK já está compilado."
    else
        echo "Compilando o NanoSDK..."
        cd NanoSDK
        mkdir -p build
        cd build
        #cmake -G Ninja -DBUILD_SHARED_LIBS=OFF -DNNG_ENABLE_QUIC=ON -DNNG_ENABLE_TLS=ON ..
        #ninja
        #sudo ninja install
        #cd ../..
	cd ..
    fi
fi
