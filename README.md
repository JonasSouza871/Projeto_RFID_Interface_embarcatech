# Sistema RFID - Raspberry Pi Pico W

![C](https://img.shields.io/badge/C-blue) ![Pico W](https://img.shields.io/badge/Pico%20W-green) ![RFID](https://img.shields.io/badge/MFRC522-orange)

Sistema de cadastro e identificação de itens via RFID com interface web e serial. Dados persistidos na flash interna.

## ✨ Funcionalidades

- ✅ Cadastro, identificação e renomeação de itens
- ✅ Interface web responsiva com abas
- ✅ Controle via monitor serial (115200 baud)
- ✅ Persistência em flash (até 50 itens)
- ✅ APIs REST para integração
- ✅ Exclusão de itens via web

## 🔌 Hardware

| Componente | GPIO | Observação |
|------------|------|------------|
| MFRC522 SDA | GP5 | Chip Select |
| MFRC522 SCK | GP2 | Clock SPI |
| MFRC522 MOSI | GP3 | Master Out |
| MFRC522 MISO | GP4 | Master In |
| MFRC522 RST | GP0 | Reset |
| MFRC522 VCC | 3V3 | ⚠️ Não use 5V! |

## 🚀 Como Usar

### Configuração

Edite as credenciais WiFi em `main.c`:
```c
#define WIFI_SSID       "SUA_REDE"
#define WIFI_PASSWORD   "SUA_SENHA"
```

### Compilação

```bash
mkdir build && cd build
cmake ..
make
# Copie RFID.uf2 para o Pico W (modo BOOTSEL)
```

### Interface Web

Acesse o IP exibido no serial. Abas disponíveis:
- **Lista**: Visualiza itens
- **Cadastrar**: Nome + cartão RFID
- **Identificar**: Lê cartão
- **Renomear**: Novo nome + cartão

### Monitor Serial

```
1 - Cadastrar | 2 - Identificar
3 - Listar    | 4 - Renomear
5 - Sair
```

## 🌐 APIs REST

| Endpoint | Descrição |
|----------|-----------|
| `/api/items` | Lista itens (JSON) |
| `/api/status` | Status do sistema |
| `/api/register?name=X` | Cadastrar |
| `/api/identify` | Identificar |
| `/api/rename?name=X` | Renomear |
| `/api/delete?uid=X` | Deletar |

**Exemplo JSON:**
```json
{"count":2,"items":[{"name":"Chave","uid":"A1:B2:C3:D4"}]}
```

## 📁 Estrutura

```
RFID/
├── main.c                  # Código principal
├── CMakeLists.txt          # Build config
└── lib/
    ├── mfrc522.c/h         # Driver RFID
    ├── pico_http_server.c/h # Servidor web
    └── lwipopts.h          # Config lwIP
```

## 🐛 Problemas Comuns

**WiFi não conecta**: Verifique SSID/senha e use Pico **W**

**RFID não lê**: Confira conexões SPI e alimentação 3.3V

**Web não carrega**: Confirme IP e mesma rede

**Serial sem saída**: Baud 115200, aguarde 3s após conectar

**Dados não salvam**: Verifique mensagem "Salvo!" no serial

## ⚙️ Configurações

Desabilitar WiFi (apenas serial):
```c
#define WIFI_ENABLED 0
```

Alterar capacidade:
```c
#define MAX_ITEMS 100
```

## 📊 Especificações

- **MCU**: RP2040 @ 133MHz
- **Flash**: 2MB (dados em 256KB offset)
- **RFID**: ISO 14443A, 13.56MHz, alcance 0-6cm
- **WiFi**: 802.11n 2.4GHz
- **Servidor**: HTTP porta 80

---

⭐ **Desenvolvido com Pico SDK 2.1.0**
