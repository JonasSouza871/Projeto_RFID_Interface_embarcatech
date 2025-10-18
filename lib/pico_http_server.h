#ifndef PICO_HTTP_SERVER_H
#define PICO_HTTP_SERVER_H


#include "pico/cyw43_arch.h"
#include "lwip/err.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

// Enumeração para o tipo de conteúdo da resposta HTTP
typedef enum
{
    HTTP_CONTENT_TYPE_HTML,
    HTTP_CONTENT_TYPE_JSON,
    HTTP_CONTENT_TYPE_PLAIN
} http_content_type_t;

// Estrutura para representar um manipulador de requisição
typedef struct
{
    const char *path;
    const char *(*handler)(const char *);
} http_request_handler_t;

// --- Funções da Biblioteca ---

/**
 * @brief Inicia o Wi-Fi e o servidor HTTP no Pico W.
 *
 * Esta função configura o Wi-Fi e inicia o servidor na porta 80.
 *
 * @param ssid O nome da rede Wi-Fi.
 * @param password A senha da rede Wi-Fi.
 * @return Retorna 0 em caso de sucesso, -1 em caso de falha.
 */
int http_server_init(const char *ssid, const char *password);

/**
 * @brief Define o conteúdo HTML da página principal.
 *
 * Esta função define a página que será servida na URL raiz ("/").
 *
 * @param html_content A string contendo o HTML.
 */
void http_server_set_homepage(const char *html_content);

/**
 * @brief Cadastra um manipulador de requisição para uma URL específica.
 *
 * Permite que você defina funções de callback para lidar com diferentes URLs
 * (ex: "/sensordata", "/set_settings").
 *
 * @param handler A estrutura http_request_handler_t com o caminho e a função de callback.
 */
void http_server_register_handler(http_request_handler_t handler);

/**
 * @brief Define o cabeçalho "Content-Type" para a resposta.
 *
 * Use esta função dentro de seus manipuladores para especificar o tipo de conteúdo.
 *
 * @param type O tipo de conteúdo (HTML, JSON, PLAIN).
 */
void http_server_set_content_type(http_content_type_t type);

/**
 * @brief Extrai um valor float de um parâmetro em uma string de requisição HTTP GET.
 *
 * Esta função busca um parâmetro (ex: "temp_offset=") na string de requisição e converte
 * o valor associado para um float, armazenando-o no ponteiro fornecido. Útil para
 * processar dados de formulários ou APIs REST simples.
 *
 * @param req A string da requisição HTTP completa (ex: "GET /settings?temp_offset=2.5...").
 * @param param O nome do parâmetro a ser buscado, incluindo o sinal de igual (ex: "temp_offset=").
 * @param value Um ponteiro para a variável float onde o valor será armazenado.
 */
void http_server_parse_float_param(const char *req, const char *param, float *value);

/**
 * @brief Lê o conteúdo de um arquivo HTML e o armazena em uma string.
 *
 * Esta função abre o arquivo especificado, lê todo o seu conteúdo e o
 * aloca dinamicamente em uma string. É responsabilidade do chamador
 * liberar a memória alocada usando free() quando a string não for mais necessária.
 *
 * @param filename O nome do arquivo HTML a ser lido.
 * @return Um ponteiro para a string que contém o conteúdo do arquivo,
 * ou NULL em caso de erro (arquivo não encontrado, erro de leitura,
 * ou erro de alocação de memória).
 */
char *http_server_read_html_file(const char *filename);

#endif // PICO_HTTP_SERVER_H