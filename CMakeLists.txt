# CMake para projeto RFID com Raspberry Pi Pico

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == NÃO EDITE AS LINHAS ABAIXO (necessário para extensão VS Code) ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================

# Configuração da placa
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Importar SDK do Raspberry Pi Pico
include(pico_sdk_import.cmake)

# Definir projeto
project(RFID C CXX ASM)

# Inicializar SDK
pico_sdk_init()

# Adicionar executável principal e biblioteca MFRC522
add_executable(RFID
    main.c
    lib/mfrc522.c
)

# Configurações do programa
pico_set_program_name(RFID "RFID")
pico_set_program_version(RFID "0.1")

# Habilitar saída UART e USB para debug
pico_enable_stdio_uart(RFID 1)
pico_enable_stdio_usb(RFID 1)

# Incluir diretórios de headers
target_include_directories(RFID PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/lib
)

# Bibliotecas necessárias
target_link_libraries(RFID
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_uart
)

# Gerar arquivos de saída (.uf2, .bin, .hex)
pico_add_extra_outputs(RFID)
