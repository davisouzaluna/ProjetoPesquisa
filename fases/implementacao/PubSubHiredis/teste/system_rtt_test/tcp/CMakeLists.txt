#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.

cmake_minimum_required(VERSION 3.13)

project(system_rtt_test_tcp)

if (BUILD_DEMO)
else ()
  find_package(nng CONFIG REQUIRED)
endif (BUILD_DEMO)

find_package(Threads)

if (DEBUG)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
  if (ASAN)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  endif (ASAN)
  if (TSAN)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
  endif (TSAN)
endif (DEBUG)


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# Variável com o arquivo(s) comum(s) para compilar junto
set(COMMON_SOURCES ../common.c)  # ajuste o nome do arquivo conforme o que existir em common
add_executable(pub pub.c ${COMMON_SOURCES})
add_executable(sub_tcp sub_tcp.c ${COMMON_SOURCES})


target_link_libraries(pub nng::nng hiredis )
target_link_libraries(pub ${CMAKE_THREAD_LIBS_INIT} )

target_link_libraries(sub_tcp nng::nng hiredis)
target_link_libraries(sub_tcp ${CMAKE_THREAD_LIBS_INIT})

if(NNG_ENABLE_TLS)
    find_package(MbedTLS REQUIRED)
    target_link_libraries(pub mbedtls::mbedtls mbedx509::mbedx509 mbedcrypto::mbedcrypto)
    target_link_libraries(sub_tls mbedtls::mbedtls mbedx509::mbedx509 mbedcrypto::mbedcrypto)
    add_definitions(-DNNG_SUPP_TLS)
endif()

if(NNG_ENABLE_SQLITE)
    add_definitions(-DNNG_SUPP_SQLITE)
    target_link_libraries(pub dl)
endif(NNG_ENABLE_SQLITE)

target_compile_definitions(pub PRIVATE NNG_ELIDE_DEPRECATED)
target_compile_definitions(sub_tcp PRIVATE NNG_ELIDE_DEPRECATED)