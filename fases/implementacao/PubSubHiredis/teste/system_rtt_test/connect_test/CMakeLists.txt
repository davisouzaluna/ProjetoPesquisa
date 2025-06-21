cmake_minimum_required(VERSION 3.13)

project(connect_test)

find_package(nng CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(COMMON_SOURCES ../common.c)

add_executable(connect connect.c ${COMMON_SOURCES})

target_include_directories(connect PRIVATE ${HIREDIS_INCLUDE_DIRS})
target_link_libraries(connect nng::nng ${HIREDIS_LIBRARIES} Threads::Threads)

# TLS support
option(NNG_ENABLE_TLS "Enable TLS support" ON)
if(NNG_ENABLE_TLS)
    find_package(MbedTLS)
    if(MbedTLS_FOUND)
        target_link_libraries(connect mbedtls mbedx509 mbedcrypto)
        target_compile_definitions(connect PRIVATE NNG_SUPP_TLS)
    endif()
endif()

# QUIC support
option(NNG_ENABLE_QUIC "Enable QUIC support" ON)
if(NNG_ENABLE_QUIC)
    find_package(OpenSSL)
    find_package(msquic)
    if(OpenSSL_FOUND AND msquic_FOUND)
        target_link_libraries(connect OpenSSL::Crypto OpenSSL::SSL msquic)
        target_compile_definitions(connect PRIVATE NNG_SUPP_QUIC)
    endif()
endif()

target_compile_definitions(connect PRIVATE NNG_ELIDE_DEPRECATED)