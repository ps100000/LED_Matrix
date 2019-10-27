/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_eth.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include <esp_task_wdt.h>

#include <esp_http_server.h>

uint8_t img[8][8];
const gpio_num_t column_pins[8] = {GPIO_NUM_23,GPIO_NUM_22,GPIO_NUM_21,GPIO_NUM_19,GPIO_NUM_18,GPIO_NUM_5,GPIO_NUM_4,GPIO_NUM_15};
const gpio_num_t row_pins[8] = {GPIO_NUM_13,GPIO_NUM_12,GPIO_NUM_14,GPIO_NUM_27,GPIO_NUM_26,GPIO_NUM_25,GPIO_NUM_33,GPIO_NUM_32};

const int IPV4_GOTIP_BIT = BIT0;
const int IPV6_GOTIP_BIT = BIT1;
/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "example";
static EventGroupHandle_t wifi_event_group;
/* An HTTP GET handler */
static esp_err_t hello_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
            }
        }
        free(buf);
    }

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, strlen(resp_str));

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t hello = {
    .uri       = "/hello",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "<!DOCTYPE html><html><head><title>-</title></head><body><table id=\"color\"></table><table id=\"root\"></table><button style=\"background: forestgreen; width: 128px; height: 32px; border: none\" onclick=\"ok()\">OKAY</button><script type=\"text/javascript\">var n=[];var a=[\"#000000\",\"#000050\",\"#0000A0\",\"#0000F0\"];var i=3;var t=\"<tr>\";for(var r=0;r<4;r++){t+='<td style=\"background:'+a[r]+'\" width=\"64px\" height=\"64px\" onclick=\"s('+r+')\"></td>'}t+=\"</tr>\";document.getElementById(\"color\").innerHTML=t;t=\"\";for(var r=0;r<8;r++){n.push([]);t+=\"<tr>\";for(var o=0;o<8;o++){n[r].push(0);t+='<td style=\"background:#000000\" width=\"64px\" height=\"64px\" onclick=\"c('+o+\",\"+r+',this)\"></td>'}t+=\"</tr>\"}document.getElementById(\"root\").innerHTML=t;function c(t,r,o){n[t][r]=i;o.style.background=a[i]}function s(t){i=t}function ok(){var t=new XMLHttpRequest;t.open(\"PUT\",\"img\",false);t.send(JSON.stringify(n).replace(/[\\[\\],]/g,\"\"))}</script></body></html>"
};

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

/* An HTTP PUT handler. This demonstrates realtime
 * registration and deregistration of URI handlers
 */
static esp_err_t ctrl_put_handler(httpd_req_t *req)
{
    char buffer[65];
    int ret;

    if ((ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buffer[sizeof(buffer) - 1] = '\0';
    ESP_LOGI(TAG, "%s", buffer);
    
    for(uint8_t x = 0; x < 8; x++){
        for(uint8_t y = 0; y < 8; y++){
            img[x][y] = buffer[x * 8 + y] - '0';
            img[x][y] *= img[x][y];
        }
    }

    for(uint8_t y = 0; y < 8; y++){
        char out[9];
        out[8] = '\0';
        for(uint8_t x = 0; x < 8; x++){
            out[x] = img[x][y] ? img[x][y] + '0' : ' ';
        }
        ESP_LOGI(TAG, "%s", out);
    }

    /* Respond with empty body */
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t ctrl = {
    .uri       = "/img",
    .method    = HTTP_PUT,
    .handler   = ctrl_put_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &ctrl);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        /* enable ipv6 */
        tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, IPV4_GOTIP_BIT);
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
        xEventGroupClearBits(wifi_event_group, IPV6_GOTIP_BIT);
        break;
    case SYSTEM_EVENT_AP_STA_GOT_IP6:
        xEventGroupSetBits(wifi_event_group, IPV6_GOTIP_BIT);
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP6");

        char *ip6 = ip6addr_ntoa(&event->event_info.got_ip6.ip6_info.ip);
        ESP_LOGI(TAG, "IPv6: %s", ip6);
    default:
        break;
    }
    return ESP_OK;
}


static void initialise_wifi(void)
{
	tcpip_adapter_init();
	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
	wifi_config_t apConfig = {
	   .ap = {
	      .ssid="ESP32_TESTAP",
	      .ssid_len=0,
	      .password="",
	      .channel=0,
	      .authmode=WIFI_AUTH_OPEN,
	      .ssid_hidden=0,
	      .max_connection=4,
	      .beacon_interval=100
	   }
	};
	ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &apConfig) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

inline void set_pin(uint8_t pin_num, bool state){
    if(state){
        if(pin_num < 32){
            *(uint32_t*)GPIO_OUT_W1TS_REG = 1 << pin_num;
        }else{
            *(uint32_t*)GPIO_OUT1_W1TS_REG = 1 << (pin_num - 32);
        }
    }else{
        if(pin_num < 32){
            *(uint32_t*)GPIO_OUT_W1TC_REG = 1 << pin_num;
        }else{
            *(uint32_t*)GPIO_OUT1_W1TC_REG = 1 << (pin_num - 32);
        }
    }
}

static void display_task(void *pvParameters){
    uint8_t cycle_counter = 0;
    while(1){
        for (uint_fast16_t i = 0; i < 256; i++){
            for(uint8_t y = 0; y < 8; y++){
                ets_delay_us(50);
                set_pin(row_pins[(y + 7) & 0b111], 0);
                for(uint8_t x = 0; x < 8; x++){
                    set_pin(column_pins[x], cycle_counter < img[x][y] );
                    //printf("%c", cycle_counter < img[x][y] ? '#' : ' ');
                }
                set_pin(row_pins[y],1);
                ets_delay_us(125);
                //printf("\n");
            }
            //printf("\n######################\n");
            cycle_counter++;
            cycle_counter %= 10;
        }
        set_pin(row_pins[7], 0);
        esp_task_wdt_reset();
    }
}

void app_main(void)
{
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[2], PIN_FUNC_GPIO);//Pin 2 als GPIO setzen
    gpio_set_direction(2,GPIO_MODE_OUTPUT);             //Pin 2 als Ausgang setzen
    for (uint8_t i = 0; i < 8; i++){
        PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[row_pins[i]], PIN_FUNC_GPIO);
        gpio_set_direction(row_pins[i],GPIO_MODE_OUTPUT);
        PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[column_pins[i]], PIN_FUNC_GPIO);
        gpio_set_direction(column_pins[i],GPIO_MODE_OUTPUT);
    }
    set_pin(2, true);
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    //ESP_ERROR_CHECK(example_connect());
    initialise_wifi();
    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */

    /* Start the server for the first time */
    server = start_webserver();
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 5, NULL, 1);
}
