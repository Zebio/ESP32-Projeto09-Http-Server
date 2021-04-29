#include <esp_http_server.h>
#include <stdio.h>
#include <esp_wifi.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include <tcpip_adapter.h>

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1


#define EXAMPLE_ESP_WIFI_SSID      "tira_o_zoio"
#define EXAMPLE_ESP_WIFI_PASS      "jabuticaba"
#define EXAMPLE_ESP_MAXIMUM_RETRY  10

/*----------------------Mapeamento de Hardware----------------------*/
#define led1 32
#define led2 33


/*---------------------Variáveis GLobais    ----------------------*/
bool led1_status = false;
bool led2_status = false;


static EventGroupHandle_t s_wifi_event_group;

tcpip_adapter_ip_info_t ipInfo; 
char str[256];

/*---------------------Variáveis para Armazenar código HTML ----------------------*/
const char *index_html=       "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><link rel=\"icon\" href=\"data:,\"></head><link rel=\"icon\" href=\"data:,\"><title>Projeto05 - Automacao com ESP32</title><style> html{color: #ffffff;font-family: Verdana;text-align: center;background-color: #272727fd}.botao_on{color: #ffffff;padding: 15px 25px;font-size: 30px;margin: 2px;font-family: Tahoma;background-color: #313891a6;}.botao_off{color: #ffffff;padding: 15px 25px;font-size: 30px;margin: 2px;font-family: Tahoma;background-color: #7c7c7ca6;}</style><body><h2>Controle de Saidas</h2><p>";
const char *led1_ligado=      "<a href=\"/led1\"><button class=\"botao_on\" > LED 1   </button></a></p>";
const char *led1_desligado=   "<a href=\"/led1\"><button class=\"botao_off\" > LED 1   </button></a></p>";
const char *led2_ligado=      "<p><a href=\"/led2\"><button class=\"botao_on\" > LED 2   </button></a></p>";
const char *led2_desligado=   "<p><a href=\"/led2\"><button class=\"botao_off\" > LED 2   </button></a></p>";
const char *html_final=       "</body></html>\n";


static const char *TAG = "ESP";

static int s_retry_num = 0;


void print_webpage(httpd_req_t *req)
{
    char buffer[760] =""; 
    strcat(buffer, index_html);

    if (led1_status)
        strcat(buffer, led1_ligado);
    else
        strcat(buffer, led1_desligado);

    if (led2_status)
        strcat(buffer, led2_ligado);
    else
        strcat(buffer, led2_desligado);  

    strcat(buffer, html_final);  

    httpd_resp_send(req, buffer, strlen(buffer));

}


//handler do Get da Página Principal
static esp_err_t main_page_get_handler(httpd_req_t *req)
{
    //imprime a página
    print_webpage(req);
    //retorna OK
    return ESP_OK;
}

//a declaração da página Principal
static const httpd_uri_t main_page = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = main_page_get_handler,
    .user_ctx  = NULL
};

//handler do Get do Led1. Altera o Valor do LED e imprime a página novamente
static esp_err_t led1_get_handler(httpd_req_t *req)
{
    led1_status= !led1_status; //Inverte o Estado do Led 1
    //Envia a resposta na nova Página
     print_webpage(req);

    //Retorna OK
    return ESP_OK;
}

//a declaração da página do Led1
static const httpd_uri_t led1_get = {
    .uri       = "/led1",
    .method    = HTTP_GET,
    .handler   = led1_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};


//handler do Get do Led1. Altera o Valor do LED e imprime a página novamente
static esp_err_t led2_get_handler(httpd_req_t *req)
{
    led2_status= !led2_status; //Inverte o Estado do Led 2
    //Envia a resposta na nova Página
     print_webpage(req);

    //Retorna OK
    return ESP_OK;
}


//a declaração da página do Led2
static const httpd_uri_t led2_get = {
    .uri       = "/led2",
    .method    = HTTP_GET,
    .handler   = led2_get_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    printf("Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        printf("Registering URI handlers");
        httpd_register_uri_handler(server, &main_page);
        httpd_register_uri_handler(server, &led1_get);
        httpd_register_uri_handler(server, &led2_get);
        return server;
    }

    printf("Error starting server!");
    return NULL;
}




static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}


void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    static httpd_handle_t server =NULL;
    /* Start the server for the first time */
    server = start_webserver();
}