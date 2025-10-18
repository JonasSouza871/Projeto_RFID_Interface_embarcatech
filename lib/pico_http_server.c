#include "pico_http_server.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "pico/stdio.h"

// --- Variáveis internas da biblioteca ---
#define MAX_HANDLERS 10
static http_request_handler_t handlers[MAX_HANDLERS];
static int handler_count = 0;
static const char *homepage_content = NULL;
static http_content_type_t response_content_type = HTTP_CONTENT_TYPE_HTML;

// Estrutura para gerenciar o estado da conexão
struct http_state
{
    char response[16384]; // Tamanho do buffer de resposta
    size_t len;
    size_t sent;
};

// Callback para enviar dados após a escrita
static err_t http_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

// Roteador de requisições
static void handle_request(struct http_state *hs, const char *req_line)
{
    char *path_end = strchr(req_line, ' ');
    if (path_end == NULL)
    {
        hs->len = snprintf(hs->response, sizeof(hs->response), "HTTP/1.1 400 Bad Request\r\n\r\n");
        return;
    }
    size_t path_len = path_end - req_line;
    char path[path_len + 1];
    strncpy(path, req_line, path_len);
    path[path_len] = '\0';

    if ((strcmp(path, "/") == 0) && homepage_content)
    {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n\r\n%s",
                           (int)strlen(homepage_content), homepage_content);

        return;
    }

    // Procura por um handler registrado
    for (int i = 0; i < handler_count; i++)
    {
        if (strstr(path, handlers[i].path) && handlers[i].handler)
        {
            const char *content = handlers[i].handler(req_line);
            const char *content_type_str;
            switch (response_content_type)
            {
            case HTTP_CONTENT_TYPE_JSON:
                content_type_str = "application/json";
                break;
            case HTTP_CONTENT_TYPE_PLAIN:
                content_type_str = "text/plain";
                break;
            case HTTP_CONTENT_TYPE_HTML:
            default:
                content_type_str = "text/html";
                break;
            }
            hs->len = snprintf(hs->response, sizeof(hs->response),
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Type: %s\r\n"
                               "Content-Length: %d\r\n"
                               "Connection: close\r\n\r\n%s",
                               content_type_str, (int)strlen(content), content);
            return;
        }
    }

    // Chegará até aqui se nenhum handler for encontrado
    hs->len = snprintf(hs->response, sizeof(hs->response), "HTTP/1.1 404 Not Found\r\n\r\n");
}

// Callback principal de recepção de dados
static err_t http_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    printf("[HTTP] Requisicao recebida\n");

    struct http_state *hs = (struct http_state *)malloc(sizeof(struct http_state));
    if (!hs)
    {
        printf("[HTTP] ERRO: Falha ao alocar memoria\n");
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }

    hs->sent = 0;

    char *req = (char *)p->payload;
    if (strstr(req, "GET ") == req)
    {
        req += 4; // Avança o ponteiro após "GET "
        printf("[HTTP] GET request\n");
        handle_request(hs, req);
    }
    else
    {
        printf("[HTTP] Metodo nao permitido\n");
        hs->len = snprintf(hs->response, sizeof(hs->response), "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
    }

    printf("[HTTP] Enviando resposta (%d bytes)\n", hs->len);

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent_callback);

    err_t write_err = tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK)
    {
        printf("[HTTP] ERRO ao escrever: %d\n", write_err);
    }

    tcp_output(tpcb);
    pbuf_free(p);
    return ERR_OK;
}

// Callback de nova conexão
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    printf("[HTTP] Nova conexao recebida\n");
    tcp_recv(newpcb, http_recv_callback);
    return ERR_OK;
}

// Função de inicialização
int http_server_init(const char *ssid, const char *password)
{
    printf("Inicializando hardware WiFi (CYW43)...\n");
    printf("Isso pode demorar alguns segundos...\n");

    int init_result = cyw43_arch_init();

    if (init_result != 0)
    {
        printf("Falha ao inicializar o hardware WiFi (codigo: %d)\n", init_result);
        printf("Verifique se:\n");
        printf("  - Esta usando Raspberry Pi Pico W (nao Pico normal)\n");
        printf("  - CMakeLists.txt tem PICO_BOARD=pico_w\n");
        return -1;
    }

    printf("Hardware WiFi inicializado!\n");

    cyw43_arch_enable_sta_mode();
    printf("Conectando a rede: %s\n", ssid);
    printf("Aguarde...\n");

    int connect_result = cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000);

    if (connect_result != 0)
    {
        printf("Falha na conexao WiFi (codigo: %d)\n", connect_result);

        if (connect_result == -1)
        {
            printf("Erro: SSID nao encontrado\n");
        }
        else if (connect_result == -2)
        {
            printf("Erro: Senha incorreta\n");
        }
        else if (connect_result == -3)
        {
            printf("Erro: Timeout na conexao\n");
        }

        printf("Verifique:\n");
        printf("  1. SSID: '%s' esta correto\n", ssid);
        printf("  2. Senha WiFi esta correta\n");
        printf("  3. Roteador esta ligado e acessivel\n");
        cyw43_arch_deinit();  // Limpar recursos
        return -1;
    }

    printf("WiFi conectado!\n");

    // Aguardar DHCP obter IP (tentar até 5 segundos)
    printf("Aguardando endereco IP do DHCP");

    int ip_retry = 0;
    const ip4_addr_t *ip = NULL;

    while (ip_retry < 25)  // 25 x 200ms = 5 segundos máximo
    {
        if (netif_default != NULL && netif_is_up(netif_default))
        {
            ip = netif_ip4_addr(netif_default);
            if (ip != NULL && ip->addr != 0)
            {
                // IP obtido com sucesso
                break;
            }
        }

        // Processar polling do WiFi durante a espera
        cyw43_arch_poll();

        sleep_ms(200);
        ip_retry++;

        // Mostrar progresso
        if (ip_retry % 3 == 0)
        {
            printf(".");
        }
    }

    printf("\n");

    // Exibir resultado
    if (ip != NULL && ip->addr != 0)
    {
        printf("========================================\n");
        printf("  IP obtido: %s\n", ip4addr_ntoa(ip));
        printf("  Porta: 80\n");
        printf("========================================\n");
    }
    else
    {
        printf("AVISO: Nao foi possivel obter IP do DHCP\n");
        printf("Tentando obter IP atual...\n");

        // Tentar obter IP mesmo assim
        if (netif_default != NULL)
        {
            ip = netif_ip4_addr(netif_default);
            if (ip != NULL)
            {
                printf("IP atual: %s\n", ip4addr_ntoa(ip));
                if (ip->addr == 0)
                {
                    printf("IP invalido (0.0.0.0) - servidor pode nao funcionar\n");
                }
            }
            else
            {
                printf("Nao foi possivel obter IP\n");
            }
        }
    }

    // Só criar servidor se temos um IP válido
    if (ip == NULL || ip->addr == 0)
    {
        printf("\nServidor HTTP NAO iniciado (sem IP valido)\n");
        printf("Sistema funcionara apenas via serial.\n");
        // Não desabilitar WiFi, pode ser que obtenha IP depois
        return 0;  // Retorna sucesso para não bloquear o sistema
    }

    printf("\nIniciando servidor HTTP...\n");

    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL)
    {
        printf("Falha ao criar TCP PCB.\n");
        printf("Sistema continuara via serial.\n");
        return 0;  // Retorna sucesso
    }

    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao fazer bind na porta 80.\n");
        printf("Sistema continuara via serial.\n");
        tcp_abort(pcb);
        return 0;  // Retorna sucesso
    }

    pcb = tcp_listen(pcb);
    if (pcb == NULL)
    {
        printf("Falha ao escutar na porta 80.\n");
        printf("Sistema continuara via serial.\n");
        return 0;  // Retorna sucesso
    }

    tcp_accept(pcb, connection_callback);

    printf("Servidor HTTP ativo na porta 80!\n");
    return 0;
}

// Implementação das funções de interface
void http_server_set_homepage(const char *html_content)
{
    homepage_content = html_content;
}

void http_server_register_handler(http_request_handler_t handler)
{
    if (handler_count < MAX_HANDLERS)
    {
        handlers[handler_count++] = handler;
    }
}

void http_server_set_content_type(http_content_type_t type)
{
    response_content_type = type;
}

void http_server_parse_float_param(const char *req, const char *param, float *value)
{
    char *found = strstr(req, param);
    if (found)
    {
        *value = atof(found + strlen(param));
    }
}

char *http_server_read_html_file(const char *filename)
{
    FILE *file;
    long file_size;
    char *html_content_raw = NULL;
    char *html_content_minified = NULL;
    int write_index = 0;

    file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("Erro ao abrir o arquivo");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    html_content_raw = (char *)malloc(file_size + 1);
    if (html_content_raw == NULL)
    {
        perror("Erro ao alocar memória para o buffer");
        fclose(file);
        return NULL;
    }

    long bytes_read = fread(html_content_raw, 1, file_size, file);
    if (bytes_read != file_size)
    {
        perror("Erro ao ler o arquivo");
        free(html_content_raw);
        fclose(file);
        return NULL;
    }

    html_content_raw[file_size] = '\0';
    fclose(file);

    html_content_minified = (char *)malloc(file_size + 1);
    if (html_content_minified == NULL)
    {
        perror("Erro ao alocar memória para a string minificada");
        free(html_content_raw);
        return NULL;
    }

    for (int i = 0; i < file_size; i++)
    {
        if (html_content_raw[i] != '\n' && html_content_raw[i] != '\r')
        {
            html_content_minified[write_index++] = html_content_raw[i];
        }
    }

    html_content_minified[write_index] = '\0';

    free(html_content_raw);

    return html_content_minified;
}