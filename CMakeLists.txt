cmake_minimum_required(VERSION 2.8)
project(xmrig-cuda)

option(WITH_CN_LITE         "Enable CryptoNight-Lite algorithms family" ON)
option(WITH_CN_HEAVY        "Enable CryptoNight-Heavy algorithms family" ON)
option(WITH_CN_PICO         "Enable CryptoNight-Pico algorithm" ON)
option(WITH_CN_GPU          "Enable CryptoNight-GPU algorithm" ON)
option(WITH_RANDOMX         "Enable RandomX algorithms family" ON)
option(WITH_ARGON2          "Enable Argon2 algorithms family" OFF) # unsupported
option(WITH_ASTROBWT        "Enable AstroBWT algorithms family" ON)
option(WITH_KAWPOW_RVN      "Enable KawPOW algorithms family" ON)

if (WITH_CN_LITE)
    add_definitions(/DXMRIG_ALGO_CN_LITE)
endif()

if (WITH_CN_HEAVY)
    add_definitions(/DXMRIG_ALGO_CN_HEAVY)
endif()

if (WITH_CN_PICO)
    add_definitions(/DXMRIG_ALGO_CN_PICO)
endif()

if (WITH_CN_GPU)
    add_definitions(/DXMRIG_ALGO_CN_GPU)
endif()

if (WITH_RANDOMX)
    add_definitions(/DXMRIG_ALGO_RANDOMX)
endif()

if (WITH_ARGON2)
    add_definitions(/DXMRIG_ALGO_ARGON2)
endif()

if (WITH_ASTROBWT)
    add_definitions(/DXMRIG_ALGO_ASTROBWT)
endif()

if (WITH_KAWPOW_RVN)
  add_definitions(/DXMRIG_ALGO_KAWPOW_RVN)
endif()

include_directories(src)
add_definitions(/DCUB_IGNORE_DEPRECATED_CPP_DIALECT)


include(cmake/cpu.cmake)
include(cmake/flags.cmake)
include(cmake/CUDA.cmake)


set(SOURCES
    src/crypto/cn/c_blake256.c
    src/crypto/common/Algorithm.h
    src/crypto/common/Algorithm.cpp
    src/CudaCryptonightR_gen.cpp
    src/version.h
    src/xmrig-cuda.cpp
    src/xmrig-cuda.h
    )


if (WIN32)
    set(SOURCES_OS
        res/app.rc
        )
else()
    set(SOURCES_OS "")
endif()


add_library(${CMAKE_PROJECT_NAME} SHARED ${SOURCES} ${SOURCES_OS})
target_link_libraries(${CMAKE_PROJECT_NAME} xmrig-cu ${LIBS})

if (WIN32)
    file(GLOB NVRTCDLL "${CUDA_TOOLKIT_ROOT_DIR}/bin/nvrtc64*.dll")
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NVRTCDLL}" $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>)

    file(GLOB NVRTCBUILTINDLL "${CUDA_TOOLKIT_ROOT_DIR}/bin/nvrtc-builtins64*.dll")
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NVRTCBUILTINDLL}" $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>)
endif()
