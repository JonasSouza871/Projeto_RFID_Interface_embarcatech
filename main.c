// =====================================================
// Sistema RFID - Raspberry Pi Pico W
// Cadastro e identificação de itens via RFID
// Interface web + Serial
// =====================================================

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/spi.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "mfrc522.h"
#include "pico_http_server.h"

// ========== CONFIGURAÇÕES ==========

// Configurações de WiFi
#define WIFI_ENABLED    1                  // 1 = WiFi habilitado, 0 = apenas serial
#define WIFI_SSID       "NOME_REDE"        // Nome da rede WiFi
#define WIFI_PASSWORD   "SENHA_REDE"       // Senha da rede WiFi

// Pinagem do leitor RFID MFRC522
#define PIN_MISO    4                      // SPI MISO (Master In Slave Out)
#define PIN_CS      5                      // SPI CS (Chip Select)
#define PIN_SCK     2                      // SPI Clock
#define PIN_MOSI    3                      // SPI MOSI (Master Out Slave In)
#define PIN_RST     0                      // Reset do MFRC522

// Configurações de armazenamento
#define MAX_ITEMS       50                 // Máximo de itens que podem ser cadastrados
#define MAX_NAME_LEN    32                 // Tamanho máximo do nome de um item
#define UID_SIZE        10                 // Tamanho máximo do UID do cartão RFID

// Configurações de memória flash
#define FLASH_TARGET_OFFSET (256 * 1024)   // Offset de 256KB na flash para salvar dados
#define FLASH_MAGIC_NUMBER  0x52464944     // Número mágico "RFID" para validar dados

// ========== ESTRUTURAS DE DADOS ==========

// Estrutura que representa um item cadastrado
typedef struct {
    uint8_t uid[UID_SIZE];           // UID único do cartão RFID
    uint8_t uid_size;                // Tamanho real do UID (varia de 4 a 10 bytes)
    char name[MAX_NAME_LEN];         // Nome descritivo do item
    bool active;                     // true = slot ocupado, false = slot vazio
} RFIDItem;

// Estrutura do banco de dados completo
typedef struct {
    uint32_t magic;                  // Número mágico para validar dados na flash
    RFIDItem items[MAX_ITEMS];       // Array de todos os itens
    uint32_t count;                  // Quantidade de itens ativos
} RFIDDatabase;

// ========== VARIÁVEIS GLOBAIS ==========

// Banco de dados principal (salvo na RAM e flash)
RFIDDatabase database = {0};

// Flag que indica se WiFi está funcionando
bool wifi_enabled = false;

// Variáveis para controle de operações via web
// (volatile porque são acessadas no loop principal e nos handlers HTTP)
volatile bool web_register_mode = false;        // true = aguardando cartão para cadastro
volatile bool web_identify_mode = false;        // true = aguardando cartão para identificação
volatile bool web_rename_mode = false;          // true = aguardando cartão para renomear
volatile char web_item_name[MAX_NAME_LEN] = {0}; // Nome temporário para operações web
volatile uint8_t last_uid[UID_SIZE] = {0};      // Último UID lido
volatile uint8_t last_uid_size = 0;             // Tamanho do último UID
volatile char last_item_found[MAX_NAME_LEN] = {0}; // Resultado da última identificação

// ========== PROTÓTIPOS DE FUNÇÕES ==========

// Funções de operação via serial
void setup_gpio(void);                              // Configura pinos GPIO e SPI
void show_menu(void);                               // Exibe menu no serial
void register_item(MFRC522Ptr_t mfrc);             // Cadastra novo item via serial
void identify_item(MFRC522Ptr_t mfrc);             // Identifica item via serial
void list_items(void);                              // Lista todos os itens no serial
void rename_item(MFRC522Ptr_t mfrc);               // Renomeia item via serial
void read_line(char *buffer, int max_len);          // Lê linha de entrada do usuário

// Funções de gerenciamento do banco de dados
int find_item_by_uid(uint8_t *uid, uint8_t uid_size); // Busca item pelo UID
void print_uid(uint8_t *uid, uint8_t size);          // Imprime UID formatado
void save_database(void);                             // Salva database na flash
void load_database(void);                             // Carrega database da flash

// Handlers HTTP (APIs REST)
const char* handle_get_items(const char *req);       // GET /api/items - Lista itens
const char* handle_get_status(const char *req);      // GET /api/status - Status do sistema
const char* handle_register_mode(const char *req);   // GET /api/register - Cadastrar
const char* handle_identify_mode(const char *req);   // GET /api/identify - Identificar
const char* handle_rename_mode(const char *req);     // GET /api/rename - Renomear
const char* handle_delete_item(const char *req);     // GET /api/delete - Deletar
void init_web_server(void);                           // Inicializa servidor web

// ========== FUNÇÃO PRINCIPAL ==========

int main() {
    // Inicializa comunicação serial (USB e UART)
    stdio_init_all();
    sleep_ms(3000);  // Aguarda estabilização

    printf("\n========================================\n");
    printf("  Sistema de Cadastro RFID\n");
    printf("========================================\n\n");

    // Carrega dados salvos da memória flash
    load_database();

#if WIFI_ENABLED
    // Inicializa WiFi e servidor web (deve ser antes do SPI do RFID)
    printf("Inicializando WiFi...\n");
    init_web_server();
    sleep_ms(1000);
#else
    printf("WiFi desabilitado (WIFI_ENABLED = 0)\n");
#endif

    // Configura hardware do leitor RFID
    printf("\nConfigurando RFID...\n");
    setup_gpio();

    // Inicializa biblioteca MFRC522
    MFRC522Ptr_t mfrc = MFRC522_Init();
    if (mfrc == NULL) {
        printf("Erro: Falha ao inicializar MFRC522!\n");
        while(1) sleep_ms(1000);  // Trava em caso de erro
    }

    // Inicializa comunicação com o chip RFID
    PCD_Init(mfrc, spi0);
    printf("MFRC522 OK! Itens cadastrados: %lu\n\n", database.count);

    // ========== LOOP PRINCIPAL ==========
    // Processa requisições WiFi e entrada serial continuamente
    while (1) {
#if WIFI_ENABLED
        if (wifi_enabled) {
            // Processa pacotes WiFi/TCP pendentes
            cyw43_arch_poll();
            sleep_ms(1);

            // Processa operações RFID iniciadas via web
            if (web_register_mode || web_identify_mode || web_rename_mode) {
                // Verifica se há cartão próximo ao leitor
                if (PICC_IsNewCardPresent(mfrc) && PICC_ReadCardSerial(mfrc)) {
                    // Salva UID lido
                    memcpy((void*)last_uid, mfrc->uid.uidByte, mfrc->uid.size);
                    last_uid_size = mfrc->uid.size;

                    // MODO CADASTRO VIA WEB
                    if (web_register_mode) {
                        int existing = find_item_by_uid(mfrc->uid.uidByte, mfrc->uid.size);
                        if (existing == -1 && strlen((const char*)web_item_name) > 0) {
                            // Encontra slot vazio e cadastra
                            for (int i = 0; i < MAX_ITEMS; i++) {
                                if (!database.items[i].active) {
                                    memcpy(database.items[i].uid, mfrc->uid.uidByte, mfrc->uid.size);
                                    database.items[i].uid_size = mfrc->uid.size;
                                    strncpy(database.items[i].name, (const char*)web_item_name, MAX_NAME_LEN - 1);
                                    database.items[i].active = true;
                                    database.count++;
                                    save_database();
                                    printf("[WEB] Cadastrado: %s\n", web_item_name);
                                    break;
                                }
                            }
                        }
                        web_register_mode = false;
                        memset((void*)web_item_name, 0, MAX_NAME_LEN);
                    }
                    // MODO IDENTIFICAÇÃO VIA WEB
                    else if (web_identify_mode) {
                        int idx = find_item_by_uid(mfrc->uid.uidByte, mfrc->uid.size);
                        if (idx != -1) {
                            strncpy((char*)last_item_found, database.items[idx].name, MAX_NAME_LEN - 1);
                            printf("[WEB] Identificado: %s\n", last_item_found);
                        } else {
                            strcpy((char*)last_item_found, "NAO_CADASTRADO");
                        }
                        web_identify_mode = false;
                    }
                    // MODO RENOMEAR VIA WEB
                    else if (web_rename_mode) {
                        int idx = find_item_by_uid(mfrc->uid.uidByte, mfrc->uid.size);
                        if (idx != -1 && strlen((const char*)web_item_name) > 0) {
                            strncpy(database.items[idx].name, (const char*)web_item_name, MAX_NAME_LEN - 1);
                            save_database();
                            printf("[WEB] Renomeado: %s\n", web_item_name);
                        }
                        web_rename_mode = false;
                        memset((void*)web_item_name, 0, MAX_NAME_LEN);
                    }
                    PCD_StopCrypto1(mfrc);  // Finaliza comunicação com cartão
                }
            }
        }
#endif

        // Verifica se há entrada do usuário no serial (não bloqueia)
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) continue;  // Nenhuma tecla, volta ao início

        // Processa comando do usuário
        char option = (char)c;
        // Limpa buffer até encontrar Enter
        while (getchar_timeout_us(100000) != '\n' && getchar_timeout_us(0) != PICO_ERROR_TIMEOUT);

        show_menu();
        printf("Opcao: %c\n\n", option);

        // Executa ação baseada na opção
        switch(option) {
            case '1': register_item(mfrc); break;   // Cadastrar
            case '2': identify_item(mfrc); break;   // Identificar
            case '3': list_items(); break;          // Listar
            case '4': rename_item(mfrc); break;     // Renomear
            case '5':                                // Sair
                printf("Encerrando...\n");
#if WIFI_ENABLED
                if (wifi_enabled) cyw43_arch_deinit();  // Desliga WiFi
#endif
                return 0;
            default:
                printf("Opcao invalida!\n\n");
        }
        sleep_ms(500);
    }
    return 0;
}

// ========== FUNÇÕES AUXILIARES ==========

// Configura os pinos GPIO e inicializa SPI para o MFRC522
void setup_gpio(void) {
    // Configura pino de reset
    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_RST, 1);  // Reset inativo (HIGH)

    // Inicializa SPI0 a 1MHz
    spi_init(spi0, 1000000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Configura Chip Select (CS)
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);  // CS inativo (HIGH)
}

// Exibe menu de opções no serial
void show_menu(void) {
    printf("========================================\n");
    printf("1 - Cadastrar  | 2 - Identificar\n");
    printf("3 - Listar     | 4 - Renomear\n");
    printf("5 - Sair\n");
    printf("========================================\n");
    printf("Opcao: ");
}

// Lê uma linha de texto do usuário via serial
void read_line(char *buffer, int max_len) {
    int i = 0;
    char c;
    while (i < max_len - 1) {
        c = getchar();
        if (c == '\n' || c == '\r') break;  // Enter pressionado
        buffer[i++] = c;
    }
    buffer[i] = '\0';  // Finaliza string
}

// Imprime UID em formato hexadecimal (ex: A1:B2:C3:D4)
void print_uid(uint8_t *uid, uint8_t size) {
    for (uint8_t i = 0; i < size; i++) {
        printf("%02X", uid[i]);
        if (i < size - 1) printf(":");
    }
}

// Busca um item no banco de dados pelo UID
// Retorna: índice do item (0-49) ou -1 se não encontrado
int find_item_by_uid(uint8_t *uid, uint8_t uid_size) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        // Verifica se slot está ativo e tamanho do UID confere
        if (database.items[i].active && database.items[i].uid_size == uid_size) {
            // Compara byte a byte
            bool match = true;
            for (int j = 0; j < uid_size; j++) {
                if (database.items[i].uid[j] != uid[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return i;  // Encontrado!
        }
    }
    return -1;  // Não encontrado
}

// ========== OPERAÇÕES VIA SERIAL ==========

// Cadastra um novo item via serial
// Aguarda cartão por 10 segundos, solicita nome e salva no banco
void register_item(MFRC522Ptr_t mfrc) {
    printf("\n--- CADASTRO ---\n");

    // Verifica se há espaço disponível
    if (database.count >= MAX_ITEMS) {
        printf("Limite atingido!\n\n");
        return;
    }

    printf("Aproxime o cartao...\n");
    int timeout = 100;  // 10 segundos (100 x 100ms)
    bool card_read = false;

    // Loop de espera pelo cartão
    while (timeout > 0 && !card_read) {
        if (PICC_IsNewCardPresent(mfrc) && PICC_ReadCardSerial(mfrc)) {
            card_read = true;
            break;
        }
        sleep_ms(100);
        timeout--;
    }

    if (!card_read) {
        printf("Timeout!\n\n");
        return;
    }

    // Verifica se já está cadastrado
    int existing = find_item_by_uid(mfrc->uid.uidByte, mfrc->uid.size);
    if (existing != -1) {
        printf("Ja cadastrado: %s\n", database.items[existing].name);
        printf("UID: "); print_uid(mfrc->uid.uidByte, mfrc->uid.size); printf("\n\n");
        PCD_StopCrypto1(mfrc);
        return;
    }

    // Mostra UID e solicita nome
    printf("Cartao detectado! UID: ");
    print_uid(mfrc->uid.uidByte, mfrc->uid.size);
    printf("\n\nNome: ");

    char item_name[MAX_NAME_LEN];
    read_line(item_name, MAX_NAME_LEN);

    if (strlen(item_name) == 0) {
        printf("Nome invalido!\n\n");
        PCD_StopCrypto1(mfrc);
        return;
    }

    // Encontra primeiro slot vazio
    int slot = -1;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!database.items[i].active) {
            slot = i;
            break;
        }
    }

    // Cadastra o item
    memcpy(database.items[slot].uid, mfrc->uid.uidByte, mfrc->uid.size);
    database.items[slot].uid_size = mfrc->uid.size;
    strncpy(database.items[slot].name, item_name, MAX_NAME_LEN - 1);
    database.items[slot].name[MAX_NAME_LEN - 1] = '\0';
    database.items[slot].active = true;
    database.count++;

    printf("Cadastrado: %s\n", database.items[slot].name);
    printf("Total: %lu\n", database.count);

    // Salva na memória flash
    save_database();
    printf("Salvo!\n\n");
    PCD_StopCrypto1(mfrc);
}

/**
 * Identifica um item cadastrado
 */
void identify_item(MFRC522Ptr_t mfrc) {
    printf("\n--- IDENTIFICACAO DE ITEM ---\n\n");
    printf("Aproxime o cartao RFID do leitor...\n");

    // Aguardar leitura do cartão (timeout de 10 segundos)
    int timeout = 100; // 10 segundos
    bool card_read = false;

    while (timeout > 0 && !card_read) {
        if (PICC_IsNewCardPresent(mfrc)) {
            if (PICC_ReadCardSerial(mfrc)) {
                card_read = true;
                break;
            }
        }
        sleep_ms(100);
        timeout--;
    }

    if (!card_read) {
        printf("Timeout: Nenhum cartao detectado!\n\n");
        return;
    }

    // Buscar item
    int item_index = find_item_by_uid(mfrc->uid.uidByte, mfrc->uid.size);

    printf("\n");
    printf("UID lido: ");
    print_uid(mfrc->uid.uidByte, mfrc->uid.size);
    printf("\n\n");

    if (item_index != -1) {
        printf("========================================\n");
        printf("      ITEM IDENTIFICADO!\n");
        printf("========================================\n");
        printf("Nome: %s\n", database.items[item_index].name);
        printf("========================================\n\n");
    } else {
        printf("** Item nao cadastrado **\n");
        printf("Utilize a opcao 1 para cadastrar.\n\n");
    }

    PCD_StopCrypto1(mfrc);
}

/**
 * Lista todos os itens cadastrados
 */
void list_items(void) {
    printf("\n--- ITENS CADASTRADOS ---\n\n");

    if (database.count == 0) {
        printf("Nenhum item cadastrado.\n\n");
        return;
    }

    printf("Total: %lu itens\n\n", database.count);

    int count = 0;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (database.items[i].active) {
            count++;
            printf("%d. %s\n", count, database.items[i].name);
            printf("   UID: ");
            print_uid(database.items[i].uid, database.items[i].uid_size);
            printf("\n\n");
        }
    }
}

/**
 * Renomeia um item cadastrado
 */
void rename_item(MFRC522Ptr_t mfrc) {
    printf("\n--- RENOMEAR ITEM ---\n\n");

    if (database.count == 0) {
        printf("Nenhum item cadastrado para renomear.\n\n");
        return;
    }

    printf("Aproxime o cartao RFID do leitor...\n");

    // Aguardar leitura do cartão (timeout de 10 segundos)
    int timeout = 100; // 10 segundos
    bool card_read = false;

    while (timeout > 0 && !card_read) {
        if (PICC_IsNewCardPresent(mfrc)) {
            if (PICC_ReadCardSerial(mfrc)) {
                card_read = true;
                break;
            }
        }
        sleep_ms(100);
        timeout--;
    }

    if (!card_read) {
        printf("Timeout: Nenhum cartao detectado!\n\n");
        return;
    }

    // Buscar item
    int item_index = find_item_by_uid(mfrc->uid.uidByte, mfrc->uid.size);

    printf("\n");
    printf("UID lido: ");
    print_uid(mfrc->uid.uidByte, mfrc->uid.size);
    printf("\n\n");

    if (item_index == -1) {
        printf("** Item nao cadastrado **\n");
        printf("Utilize a opcao 1 para cadastrar.\n\n");
        PCD_StopCrypto1(mfrc);
        return;
    }

    // Exibir nome atual
    printf("========================================\n");
    printf("Item encontrado!\n");
    printf("Nome atual: %s\n", database.items[item_index].name);
    printf("========================================\n\n");

    // Solicitar novo nome
    printf("Digite o novo nome do item: ");
    char new_name[MAX_NAME_LEN];
    read_line(new_name, MAX_NAME_LEN);

    // Validar nome
    if (strlen(new_name) == 0) {
        printf("Erro: Nome invalido! Renomeacao cancelada.\n\n");
        PCD_StopCrypto1(mfrc);
        return;
    }

    // Salvar nome antigo para exibir confirmação
    char old_name[MAX_NAME_LEN];
    strncpy(old_name, database.items[item_index].name, MAX_NAME_LEN);

    // Atualizar nome
    strncpy(database.items[item_index].name, new_name, MAX_NAME_LEN - 1);
    database.items[item_index].name[MAX_NAME_LEN - 1] = '\0';

    printf("\n** Item renomeado com sucesso! **\n");
    printf("Nome anterior: %s\n", old_name);
    printf("Nome novo: %s\n", database.items[item_index].name);
    printf("UID: ");
    print_uid(database.items[item_index].uid, database.items[item_index].uid_size);
    printf("\n\n");

    // Salvar na flash
    save_database();
    printf("Alteracao salva na memoria!\n\n");

    PCD_StopCrypto1(mfrc);
}

/**
 * Salva o banco de dados na memória flash
 */
void save_database(void) {
    // Definir número mágico
    database.magic = FLASH_MAGIC_NUMBER;

    // Calcular tamanho a ser escrito (deve ser múltiplo de 256 bytes)
    uint32_t data_size = sizeof(RFIDDatabase);
    uint32_t write_size = (data_size + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);

    // Preparar buffer alinhado
    uint8_t buffer[write_size];
    memset(buffer, 0xFF, write_size);  // Flash apagada tem todos os bits em 1
    memcpy(buffer, &database, data_size);

    // Desabilitar interrupções durante escrita
    uint32_t interrupts = save_and_disable_interrupts();

    // Apagar setor (4096 bytes)
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);

    // Escrever dados
    flash_range_program(FLASH_TARGET_OFFSET, buffer, write_size);

    // Restaurar interrupções
    restore_interrupts(interrupts);
}

/**
 * Carrega o banco de dados da memória flash
 */
void load_database(void) {
    // Ler dados da flash (XIP - Execute In Place)
    const uint8_t *flash_data = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

    // Copiar dados para a estrutura
    RFIDDatabase temp_db;
    memcpy(&temp_db, flash_data, sizeof(RFIDDatabase));

    // Verificar número mágico
    if (temp_db.magic == FLASH_MAGIC_NUMBER) {
        // Dados válidos - copiar para database
        memcpy(&database, &temp_db, sizeof(RFIDDatabase));
        printf("Banco de dados carregado da flash.\n");
    } else {
        // Primeira execução ou dados corrompidos - inicializar vazio
        memset(&database, 0, sizeof(RFIDDatabase));
        database.magic = FLASH_MAGIC_NUMBER;
        printf("Banco de dados inicializado (vazio).\n");
    }
}

// ========================================
// FUNÇÕES DO SERVIDOR WEB
// ========================================

/**
 * Handler para obter lista de itens em JSON
 */
const char* handle_get_items(const char *req) {
    static char json_response[4096];
    char *ptr = json_response;

    http_server_set_content_type(HTTP_CONTENT_TYPE_JSON);

    ptr += sprintf(ptr, "{\"count\":%lu,\"items\":[", database.count);

    bool first = true;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (database.items[i].active) {
            if (!first) {
                ptr += sprintf(ptr, ",");
            }
            first = false;

            ptr += sprintf(ptr, "{\"name\":\"%s\",\"uid\":\"", database.items[i].name);

            for (uint8_t j = 0; j < database.items[i].uid_size; j++) {
                ptr += sprintf(ptr, "%02X", database.items[i].uid[j]);
                if (j < database.items[i].uid_size - 1) {
                    ptr += sprintf(ptr, ":");
                }
            }

            ptr += sprintf(ptr, "\"}");
        }
    }

    ptr += sprintf(ptr, "]}");

    return json_response;
}

/**
 * Handler para obter status do sistema
 */
const char* handle_get_status(const char *req) {
    static char json_response[512];

    http_server_set_content_type(HTTP_CONTENT_TYPE_JSON);

    sprintf(json_response,
            "{\"status\":\"online\",\"total_items\":%lu,\"max_items\":%d,"
            "\"register_mode\":%s,\"identify_mode\":%s,\"rename_mode\":%s,"
            "\"last_item\":\"%s\"}",
            database.count, MAX_ITEMS,
            web_register_mode ? "true" : "false",
            web_identify_mode ? "true" : "false",
            web_rename_mode ? "true" : "false",
            last_item_found);

    return json_response;
}

/**
 * Handler para ativar modo cadastro
 */
const char* handle_register_mode(const char *req) {
    static char json_response[256];

    http_server_set_content_type(HTTP_CONTENT_TYPE_JSON);

    // Extrair nome do item da query string
    char *name_param = strstr(req, "name=");
    if (name_param) {
        name_param += 5; // Pular "name="
        char *end = strchr(name_param, ' ');
        if (end) {
            int len = end - name_param;
            if (len > MAX_NAME_LEN - 1) len = MAX_NAME_LEN - 1;
            strncpy((char*)web_item_name, name_param, len);
            ((char*)web_item_name)[len] = '\0';

            // Decodificar URL encoding básico
            for (int i = 0; i < strlen((const char*)web_item_name); i++) {
                if (web_item_name[i] == '+') web_item_name[i] = ' ';
            }

            web_register_mode = true;
            web_identify_mode = false;
            web_rename_mode = false;
            strcpy((char*)last_item_found, "");

            sprintf(json_response, "{\"success\":true,\"message\":\"Aproxime o cartao RFID\"}");
        } else {
            sprintf(json_response, "{\"success\":false,\"message\":\"Nome invalido\"}");
        }
    } else {
        sprintf(json_response, "{\"success\":false,\"message\":\"Nome nao fornecido\"}");
    }

    return json_response;
}

/**
 * Handler para ativar modo identificação
 */
const char* handle_identify_mode(const char *req) {
    static char json_response[256];

    http_server_set_content_type(HTTP_CONTENT_TYPE_JSON);

    web_identify_mode = true;
    web_register_mode = false;
    web_rename_mode = false;
    strcpy((char*)last_item_found, "");

    sprintf(json_response, "{\"success\":true,\"message\":\"Aproxime o cartao RFID\"}");

    return json_response;
}

/**
 * Handler para ativar modo renomear
 */
const char* handle_rename_mode(const char *req) {
    static char json_response[256];

    http_server_set_content_type(HTTP_CONTENT_TYPE_JSON);

    // Extrair nome do item da query string
    char *name_param = strstr(req, "name=");
    if (name_param) {
        name_param += 5;
        char *end = strchr(name_param, ' ');
        if (end) {
            int len = end - name_param;
            if (len > MAX_NAME_LEN - 1) len = MAX_NAME_LEN - 1;
            strncpy((char*)web_item_name, name_param, len);
            ((char*)web_item_name)[len] = '\0';

            // Decodificar URL encoding básico
            for (int i = 0; i < strlen((const char*)web_item_name); i++) {
                if (web_item_name[i] == '+') web_item_name[i] = ' ';
            }

            web_rename_mode = true;
            web_register_mode = false;
            web_identify_mode = false;
            strcpy((char*)last_item_found, "");

            sprintf(json_response, "{\"success\":true,\"message\":\"Aproxime o cartao RFID\"}");
        } else {
            sprintf(json_response, "{\"success\":false,\"message\":\"Nome invalido\"}");
        }
    } else {
        sprintf(json_response, "{\"success\":false,\"message\":\"Nome nao fornecido\"}");
    }

    return json_response;
}

/**
 * Handler para deletar item por UID
 */
const char* handle_delete_item(const char *req) {
    static char json_response[256];

    http_server_set_content_type(HTTP_CONTENT_TYPE_JSON);

    // Extrair UID da query string
    char *uid_param = strstr(req, "uid=");
    if (uid_param) {
        uid_param += 4;
        char *end = strchr(uid_param, ' ');

        // Procurar item com esse UID
        for (int i = 0; i < MAX_ITEMS; i++) {
            if (database.items[i].active) {
                // Converter UID para string e comparar
                char uid_str[64] = {0};
                for (uint8_t j = 0; j < database.items[i].uid_size; j++) {
                    char byte_str[4];
                    sprintf(byte_str, "%02X", database.items[i].uid[j]);
                    strcat(uid_str, byte_str);
                    if (j < database.items[i].uid_size - 1) strcat(uid_str, ":");
                }

                if (strstr(uid_param, uid_str) == uid_param) {
                    // Item encontrado - marcar como inativo
                    database.items[i].active = false;
                    database.count--;
                    save_database();

                    sprintf(json_response, "{\"success\":true,\"message\":\"Item deletado\"}");
                    return json_response;
                }
            }
        }

        sprintf(json_response, "{\"success\":false,\"message\":\"Item nao encontrado\"}");
    } else {
        sprintf(json_response, "{\"success\":false,\"message\":\"UID nao fornecido\"}");
    }

    return json_response;
}

/**
 * Página principal HTML completa
 */
const char* homepage_html =
"<!DOCTYPE html><html><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Sistema RFID</title><style>"
"body{font-family:Arial;max-width:700px;margin:10px auto;padding:10px;background:#f5f5f5}"
"h1{color:#333;text-align:center;margin:10px 0}"
".box{background:#fff;padding:15px;margin:10px 0;border-radius:5px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
".btn{background:#4CAF50;color:#fff;padding:12px;border:none;width:100%;cursor:pointer;border-radius:4px;font-size:14px;margin:5px 0}"
".btn:hover{background:#45a049}"
".btn-blue{background:#2196F3}.btn-blue:hover{background:#0b7dda}"
".btn-orange{background:#ff9800}.btn-orange:hover{background:#e68900}"
".btn-red{background:#f44336}.btn-red:hover{background:#da190b}"
".item{padding:12px;border-bottom:1px solid #eee;display:flex;justify-content:space-between;align-items:center}"
".item:last-child{border-bottom:none}"
".item-info{flex:1}"
".item-name{font-weight:bold;color:#333}"
".item-uid{color:#666;font-size:12px;margin-top:4px}"
"input{width:100%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
".status{background:#e8f5e9;padding:10px;border-radius:4px;text-align:center;margin:10px 0;color:#2e7d32}"
".error{background:#ffebee;color:#c62828}"
".tabs{display:flex;gap:5px;margin-bottom:10px}"
".tab{flex:1;padding:10px;background:#ddd;border:none;cursor:pointer;border-radius:4px 4px 0 0}"
".tab.active{background:#4CAF50;color:#fff}"
".content{display:none}.content.active{display:block}"
"</style></head><body>"
"<h1>Sistema RFID</h1>"
"<div class='box'><div id='msg'></div></div>"
"<div class='tabs'>"
"<button class='tab active' onclick='showTab(0)'>Lista</button>"
"<button class='tab' onclick='showTab(1)'>Cadastrar</button>"
"<button class='tab' onclick='showTab(2)'>Identificar</button>"
"<button class='tab' onclick='showTab(3)'>Renomear</button>"
"</div>"
"<div class='content active' id='tab0'>"
"<div class='box'><p>Total: <strong id='count'>0</strong> itens</p>"
"<button class='btn' onclick='load()'>Atualizar Lista</button></div>"
"<div class='box' id='items'>Carregando...</div></div>"
"<div class='content' id='tab1'><div class='box'>"
"<h3>Cadastrar Novo Item</h3>"
"<input type='text' id='regName' placeholder='Nome do item'>"
"<button class='btn' onclick='register()'>Aguardar Cartao</button></div></div>"
"<div class='content' id='tab2'><div class='box'>"
"<h3>Identificar Item</h3>"
"<button class='btn btn-blue' onclick='identify()'>Aguardar Cartao</button>"
"<div id='identResult'></div></div></div>"
"<div class='content' id='tab3'><div class='box'>"
"<h3>Renomear Item</h3>"
"<input type='text' id='renName' placeholder='Novo nome'>"
"<button class='btn btn-orange' onclick='rename()'>Aguardar Cartao</button></div></div>"
"<script>"
"let curTab=0;"
"function showTab(n){"
"document.querySelectorAll('.tab').forEach((t,i)=>{t.classList.toggle('active',i==n);});"
"document.querySelectorAll('.content').forEach((c,i)=>{c.classList.toggle('active',i==n);});"
"curTab=n;if(n==0)load();}"
"function msg(t,err){"
"let m=document.getElementById('msg');"
"m.textContent=t;m.className=err?'status error':'status';"
"setTimeout(()=>m.textContent='',5000);}"
"function load(){"
"fetch('/api/items').then(r=>r.json()).then(d=>{"
"document.getElementById('count').textContent=d.count;"
"let html=d.count==0?'<p style=\"text-align:center;color:#999\">Nenhum item cadastrado</p>':"
"d.items.map(i=>'<div class=\"item\"><div class=\"item-info\"><div class=\"item-name\">'+i.name+'</div>"
"<div class=\"item-uid\">UID: '+i.uid+'</div></div>"
"<button class=\"btn btn-red\" style=\"width:80px;padding:8px\" onclick=\"del(\\''+i.uid+'\\')\">"
"Deletar</button></div>').join('');"
"document.getElementById('items').innerHTML=html;}).catch(e=>msg('Erro ao carregar',1));}"
"function register(){"
"let name=document.getElementById('regName').value;"
"if(!name){msg('Digite um nome',1);return;}"
"fetch('/api/register?name='+encodeURIComponent(name)).then(r=>r.json()).then(d=>{"
"if(d.success){msg(d.message);pollStatus();}else{msg(d.message,1);}}).catch(e=>msg('Erro',1));}"
"function identify(){"
"fetch('/api/identify').then(r=>r.json()).then(d=>{"
"if(d.success){msg(d.message);pollIdent();}else{msg(d.message,1);}}).catch(e=>msg('Erro',1));}"
"function rename(){"
"let name=document.getElementById('renName').value;"
"if(!name){msg('Digite um nome',1);return;}"
"fetch('/api/rename?name='+encodeURIComponent(name)).then(r=>r.json()).then(d=>{"
"if(d.success){msg(d.message);pollStatus();}else{msg(d.message,1);}}).catch(e=>msg('Erro',1));}"
"function del(uid){"
"if(!confirm('Deletar este item?'))return;"
"fetch('/api/delete?uid='+uid).then(r=>r.json()).then(d=>{"
"msg(d.message,!d.success);if(d.success)load();}).catch(e=>msg('Erro',1));}"
"function pollStatus(){"
"let cnt=0;let iv=setInterval(()=>{"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"if(!d.register_mode&&!d.rename_mode){clearInterval(iv);msg('Operacao concluida!');load();}"
"if(++cnt>20){clearInterval(iv);msg('Timeout',1);}});},500);}"
"function pollIdent(){"
"let cnt=0;let iv=setInterval(()=>{"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"if(!d.identify_mode){clearInterval(iv);"
"if(d.last_item&&d.last_item!=''){"
"document.getElementById('identResult').innerHTML=d.last_item=='NAO_CADASTRADO'?"
"'<p class=\"status error\">Item nao cadastrado</p>':"
"'<p class=\"status\">Item: <strong>'+d.last_item+'</strong></p>';}"
"}"
"if(++cnt>20){clearInterval(iv);msg('Timeout',1);}});},500);}"
"load();setInterval(()=>{if(curTab==0)load();},10000);"
"</script></body></html>";

/**
 * Inicializa o servidor web
 */
void init_web_server(void) {
    printf("Tentando conectar ao WiFi: %s\n", WIFI_SSID);

    // Inicializar WiFi e servidor HTTP
    int result = http_server_init(WIFI_SSID, WIFI_PASSWORD);

    if (result == 0) {
        wifi_enabled = true;  // Marcar WiFi como habilitado
        printf("Servidor web inicializado com sucesso!\n");

        // Configurar página principal
        http_server_set_homepage(homepage_html);

        // Registrar handlers de API
        http_request_handler_t handler_items = {
            .path = "/api/items",
            .handler = handle_get_items
        };
        http_server_register_handler(handler_items);

        http_request_handler_t handler_status = {
            .path = "/api/status",
            .handler = handle_get_status
        };
        http_server_register_handler(handler_status);

        http_request_handler_t handler_register = {
            .path = "/api/register",
            .handler = handle_register_mode
        };
        http_server_register_handler(handler_register);

        http_request_handler_t handler_identify = {
            .path = "/api/identify",
            .handler = handle_identify_mode
        };
        http_server_register_handler(handler_identify);

        http_request_handler_t handler_rename = {
            .path = "/api/rename",
            .handler = handle_rename_mode
        };
        http_server_register_handler(handler_rename);

        http_request_handler_t handler_delete = {
            .path = "/api/delete",
            .handler = handle_delete_item
        };
        http_server_register_handler(handler_delete);

        printf("Acesse o sistema pelo navegador no IP exibido acima!\n");
        printf("Funcionalidades disponiveis:\n");
        printf("  - Listar itens cadastrados\n");
        printf("  - Cadastrar novos itens\n");
        printf("  - Identificar itens\n");
        printf("  - Renomear itens\n");
        printf("  - Deletar itens\n\n");
    } else {
        wifi_enabled = false;  // Marcar WiFi como desabilitado
        printf("AVISO: Falha ao inicializar servidor web!\n");
        printf("Verifique:\n");
        printf("  - SSID: %s\n", WIFI_SSID);
        printf("  - Senha WiFi configurada corretamente\n");
        printf("  - Roteador ligado e acessivel\n");
        printf("O sistema continuara funcionando via serial.\n\n");
    }
}
