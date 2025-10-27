# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
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
# ====================================================================================
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()

# Configuração da placa (Pico W para WiFi)
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Importar SDK do Raspberry Pi Pico
include(pico_sdk_import.cmake)

# Definir projeto
project(RFID_MQTT C CXX ASM)

# Inicializar SDK
pico_sdk_init()

# Adicionar executável principal com MQTT
add_executable(RFID_MQTT
    main_mqtt.c
    lib/mfrc522.c
)

# Configurações do programa
pico_set_program_name(RFID_MQTT "RFID_MQTT")
pico_set_program_version(RFID_MQTT "1.0")

# Habilitar saída UART e USB para debug
pico_enable_stdio_uart(RFID_MQTT 1)
pico_enable_stdio_usb(RFID_MQTT 1)

# Incluir diretórios de headers
target_include_directories(RFID_MQTT PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/lib
)

# Bibliotecas necessárias para MQTT
target_link_libraries(RFID_MQTT
    pico_stdlib               # Biblioteca padrão do Pico
    pico_cyw43_arch_lwip_poll # WiFi com lwIP (necessário para MQTT)
    pico_lwip_mqtt            # Cliente MQTT do lwIP
    hardware_spi              # Comunicação SPI (para RFID)
    hardware_i2c              # I2C (caso precise no futuro)
    hardware_uart             # UART (para debug)
)

# Gerar arquivos de saída (.uf2, .bin, .hex)
pico_add_extra_outputs(RFID_MQTT)
