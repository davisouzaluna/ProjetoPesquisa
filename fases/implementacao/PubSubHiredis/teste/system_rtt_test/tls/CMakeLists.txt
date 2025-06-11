#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.

cmake_minimum_required(VERSION 3.13)

project(system_rtt_tls)
# Define the option with a default value of ON
option(NNG_ENABLE_TLS "Enable TLS support" ON)
set(MBEDsub_tls_ROOT_DIR "../../mbedsub_tls/cmake")
if (BUILD_DEMO)
else ()
# Call this from your own project's makefile.
  find_package(nng CONFIG REQUIRED)
endif (BUILD_DEMO)

find_package(Threads)
# Definir MBEDsub_tls_ROOT_DIR usando variável de ambiente




if (DEBUG)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
  if (ASAN)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  endif (ASAN)
  if (TSAN)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
  endif (TSAN)
endif (DEBUG)

add_executable(sub_tls sub_tls.c)
target_link_libraries(sub_tls nng::nng hiredis)
target_link_libraries(sub_tls ${CMAKE_THREAD_LIBS_INIT})

add_executable(pub_tls tls.c)
target_link_libraries(pub_tls nng::nng hiredis)
target_link_libraries(pub_tls ${CMAKE_THREAD_LIBS_INIT})

if(NNG_ENABLE_TLS)
    find_package(MbedTLS)
    target_link_libraries(sub_tls mbedtls mbedx509 mbedcrypto)
    target_link_libraries(pub_tls mbedtls mbedx509 mbedcrypto)
    add_definitions(-DNNG_SUPP_TLS)
endif()


if (NNG_ENABLE_SQLITE)
    add_definitions(-DNNG_SUPP_SQLITE)
    target_link_libraries(sub_tls dl)
    target_link_libraries(pub_tls dl)
endif (NNG_ENABLE_SQLITE)

target_compile_definitions(sub_tls PRIVATE NNG_ELIDE_DEPRECATED)

target_compile_definitions(pub_tls PRIVATE NNG_ELIDE_DEPRECATED)