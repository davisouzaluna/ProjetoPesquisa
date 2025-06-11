cmake_minimum_required(VERSION 3.13)

project(system_rtt_test)

find_package(nng CONFIG REQUIRED)

find_package(Threads)
find_package(msquic)
find_package(OpenSSL)




if (DEBUG)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
  if (ASAN)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  endif (ASAN)
  if (TSAN)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
  endif (TSAN)
endif (DEBUG)

add_executable(sub_quic sub.c)
add_executable(pub_quic pub.c)


if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIRS})
    target_link_libraries(sub_quic OpenSSL::Crypto OpenSSL::SSL)
    target_link_libraries(pub_quic OpenSSL::Crypto OpenSSL::SSL)
endif()


target_link_libraries(sub_quic nng::nng msquic pthread hiredis)
target_compile_definitions(sub_quic PRIVATE NNG_ELIDE_DEPRECATED)


target_link_libraries(pub_quic nng::nng msquic pthread hiredis)
target_compile_definitions(pub_quic PRIVATE NNG_ELIDE_DEPRECATED)