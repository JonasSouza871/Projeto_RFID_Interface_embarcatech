/*
 * Projeto RFID - Raspberry Pi Pico
 * Sistema de cadastro e identificação de itens via RFID
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "mfrc522.h"

// Pinagem do MFRC522
#define PIN_MISO    4   // GP4 - Master In Slave Out
#define PIN_CS      5   // GP5 - Chip Select (SDA)
#define PIN_SCK     2   // GP2 - Clock SPI
#define PIN_MOSI    3   // GP3 - Master Out Slave In
#define PIN_RST     0   // GP0 - Reset

// Configurações de armazenamento
#define MAX_ITEMS       50      // Número máximo de itens cadastrados
#define MAX_NAME_LEN    32      // Tamanho máximo do nome do item
#define UID_SIZE        10      // Tamanho máximo do UID

// Configurações de memória flash
#define FLASH_TARGET_OFFSET (256 * 1024)  // 256KB offset na flash
#define FLASH_MAGIC_NUMBER  0x52464944    // "RFID" em hexadecimal

// Estrutura para armazenar item cadastrado
typedef struct {
    uint8_t uid[UID_SIZE];      // UID do cartão RFID
    uint8_t uid_size;           // Tamanho real do UID
    char name[MAX_NAME_LEN];    // Nome do item
    bool active;                // Item ativo ou vazio
} RFIDItem;

// Banco de dados de itens em memória
typedef struct {
    uint32_t magic;             // Número mágico para validação
    RFIDItem items[MAX_ITEMS];
    uint32_t count;             // Número de itens cadastrados
} RFIDDatabase;

// Variável global do banco de dados
RFIDDatabase database = {0};

// Protótipos de funções
void setup_gpio(void);
void show_menu(void);
void register_item(MFRC522Ptr_t mfrc);
void identify_item(MFRC522Ptr_t mfrc);
void list_items(void);
void rename_item(MFRC522Ptr_t mfrc);
int find_item_by_uid(uint8_t *uid, uint8_t uid_size);
void print_uid(uint8_t *uid, uint8_t size);
void read_line(char *buffer, int max_len);
void save_database(void);
void load_database(void);

int main() {
    // Inicializar stdio
    stdio_init_all();
    sleep_ms(2000);

    printf("\n========================================\n");
    printf("  Sistema de Cadastro RFID\n");
    printf("========================================\n\n");

    // Carregar banco de dados da flash
    load_database();

    // Configurar GPIO
    setup_gpio();

    // Inicializar MFRC522
    MFRC522Ptr_t mfrc = MFRC522_Init();

    if (mfrc == NULL) {
        printf("Erro: Falha ao inicializar MFRC522!\n");
        while(1) {
            sleep_ms(1000);
        }
    }

    // Inicializar comunicação SPI e MFRC522
    PCD_Init(mfrc, spi0);
    printf("MFRC522 inicializado com sucesso!\n");
    printf("Itens carregados: %lu\n\n", database.count);

    // Loop principal
    while (1) {
        show_menu();

        char option = getchar();
        while(getchar() != '\n'); // Limpar buffer

        printf("\n");

        switch(option) {
            case '1':
                register_item(mfrc);
                break;
            case '2':
                identify_item(mfrc);
                break;
            case '3':
                list_items();
                break;
            case '4':
                rename_item(mfrc);
                break;
            case '5':
                printf("Encerrando sistema...\n");
                return 0;
            default:
                printf("Opcao invalida! Tente novamente.\n\n");
        }

        sleep_ms(500);
    }

    return 0;
}

/**
 * Configura os pinos GPIO necessários
 */
void setup_gpio(void) {
    // Configurar pino de reset do MFRC522
    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_RST, 1);  // Reset inativo (high)

    // Configurar SPI
    spi_init(spi0, 1000000);  // 1 MHz

    // Configurar pinos SPI
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Configurar Chip Select
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);  // CS inativo (high)
}

/**
 * Exibe o menu principal
 */
void show_menu(void) {
    printf("========================================\n");
    printf("           MENU PRINCIPAL\n");
    printf("========================================\n");
    printf("1 - Cadastrar novo item\n");
    printf("2 - Identificar item\n");
    printf("3 - Listar itens cadastrados\n");
    printf("4 - Renomear item\n");
    printf("5 - Sair\n");
    printf("========================================\n");
    printf("Escolha uma opcao: ");
}

/**
 * Lê uma linha de entrada do usuário
 */
void read_line(char *buffer, int max_len) {
    int i = 0;
    char c;

    while (i < max_len - 1) {
        c = getchar();
        if (c == '\n' || c == '\r') {
            break;
        }
        buffer[i++] = c;
    }
    buffer[i] = '\0';
}

/**
 * Imprime UID formatado
 */
void print_uid(uint8_t *uid, uint8_t size) {
    for (uint8_t i = 0; i < size; i++) {
        printf("%02X", uid[i]);
        if (i < size - 1) printf(":");
    }
}

/**
 * Busca item pelo UID
 * Retorna índice do item ou -1 se não encontrado
 */
int find_item_by_uid(uint8_t *uid, uint8_t uid_size) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (database.items[i].active &&
            database.items[i].uid_size == uid_size) {

            bool match = true;
            for (int j = 0; j < uid_size; j++) {
                if (database.items[i].uid[j] != uid[j]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                return i;
            }
        }
    }
    return -1;
}

/**
 * Cadastra um novo item
 */
void register_item(MFRC522Ptr_t mfrc) {
    printf("\n--- CADASTRO DE ITEM ---\n\n");

    // Verificar se há espaço
    if (database.count >= MAX_ITEMS) {
        printf("Erro: Limite de itens atingido (%d itens)!\n\n", MAX_ITEMS);
        return;
    }

    printf("Aproxime o cartao RFID do leitor...\n");

    // Aguardar leitura do cartão (timeout de 10 segundos)
    int timeout = 100; // 10 segundos (100 x 100ms)
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

    // Verificar se já está cadastrado
    int existing = find_item_by_uid(mfrc->uid.uidByte, mfrc->uid.size);
    if (existing != -1) {
        printf("\nCartao ja cadastrado como: %s\n", database.items[existing].name);
        printf("UID: ");
        print_uid(mfrc->uid.uidByte, mfrc->uid.size);
        printf("\n\n");
        PCD_StopCrypto1(mfrc);
        return;
    }

    // Exibir UID lido
    printf("\nCartao detectado!\n");
    printf("UID: ");
    print_uid(mfrc->uid.uidByte, mfrc->uid.size);
    printf("\n\n");

    // Solicitar nome do item
    printf("Digite o nome do item: ");
    char item_name[MAX_NAME_LEN];
    read_line(item_name, MAX_NAME_LEN);

    // Validar nome
    if (strlen(item_name) == 0) {
        printf("Erro: Nome invalido!\n\n");
        PCD_StopCrypto1(mfrc);
        return;
    }

    // Encontrar slot vazio
    int slot = -1;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!database.items[i].active) {
            slot = i;
            break;
        }
    }

    // Cadastrar item
    memcpy(database.items[slot].uid, mfrc->uid.uidByte, mfrc->uid.size);
    database.items[slot].uid_size = mfrc->uid.size;
    strncpy(database.items[slot].name, item_name, MAX_NAME_LEN - 1);
    database.items[slot].name[MAX_NAME_LEN - 1] = '\0';
    database.items[slot].active = true;
    database.count++;

    printf("\n** Item cadastrado com sucesso! **\n");
    printf("Nome: %s\n", database.items[slot].name);
    printf("UID: ");
    print_uid(database.items[slot].uid, database.items[slot].uid_size);
    printf("\n");
    printf("Total de itens: %lu\n\n", database.count);

    // Salvar na flash
    save_database();
    printf("Dados salvos na memoria!\n\n");

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
