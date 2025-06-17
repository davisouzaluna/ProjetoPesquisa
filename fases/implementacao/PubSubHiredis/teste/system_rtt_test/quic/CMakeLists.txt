cmake_minimum_required(VERSION 3.13)

project(system_rtt_test)

find_package(nng CONFIG REQUIRED)

find_package(Threads)
find_package(msquic)
find_package(OpenSSL)

if (DEBUG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
  if (ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  endif()
  if (TSAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
  endif()
endif()

# Adiciona o diretório common para includes
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# Variável com o arquivo(s) comum(s) para compilar junto
set(COMMON_SOURCES ../common.c)  # ajuste o nome do arquivo conforme o que existir em common

# Adiciona os executáveis incluindo os arquivos comuns
add_executable(publisher pub.c ${COMMON_SOURCES})
add_executable(sub sub.c ${COMMON_SOURCES})
# add_executable(sub2 sub_2.c ${COMMON_SOURCES})  # se quiser usar sub2 também

if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIRS})
    target_link_libraries(publisher OpenSSL::Crypto OpenSSL::SSL)
    target_link_libraries(sub OpenSSL::Crypto OpenSSL::SSL)
endif()

target_link_libraries(sub nng::nng msquic pthread hiredis)
target_compile_definitions(sub PRIVATE NNG_ELIDE_DEPRECATED)

# target_link_libraries(sub2 nng::nng msquic pthread hiredis)
# target_compile_definitions(sub2 PRIVATE NNG_ELIDE_DEPRECATED)

target_link_libraries(publisher nng::nng msquic pthread hiredis)
target_compile_definitions(publisher PRIVATE NNG_ELIDE_DEPRECATED)
