
cmake_minimum_required(VERSION 2.6)

project("mbedtls my test" C)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Wall -W ")
elseif(CMAKE_BUILD_TYPE MATCHES "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -w")
endif()

find_path(ssl_INCLUDE mbedtls/ssl.h     HINTS ${INSTALL_PREFIX}/include)
find_library(crypto_LIBRARY mbedcrypto  HINTS ${INSTALL_PREFIX}/lib)

get_filename_component(crypto_LIBRARY_PATH ${crypto_LIBRARY} PATH)

if(ssl_INCLUDE AND crypto_LIBRARY_PATH)
    set(MBEDTLS_FOUND TRUE)
    set(MBEDTLS_INCLUDE_DIR ${ssl_INCLUDE})
    set(MBEDTLS_CRYPTO_LIBRARY ${crypto_LIBRARY_PATH})
    set(MBEDTLS_LIBRARIES ${crypto_LIBRARY_PATH})
else ()
    message(FATAL_ERROR "Failed to locate mbedtls")
endif()

if (MBEDTLS_FOUND)
    message("MBEDTLS_INCLUDE_DIR=" ${MBEDTLS_INCLUDE_DIR})
    message("MBEDTLS_CRYPTO_LIBRARY=" ${MBEDTLS_CRYPTO_LIBRARY})
    message("MBEDTLS_LIBRARIES=" ${MBEDTLS_LIBRARIES})
else()
    message(FATAL_ERROR "could not find mbedtls module")
endif(MBEDTLS_FOUND)

add_subdirectory(aes)
add_subdirectory(bignum)
add_subdirectory(cert)
add_subdirectory(dh)
add_subdirectory(ec)
add_subdirectory(hash)
add_subdirectory(random)
add_subdirectory(rsa)
