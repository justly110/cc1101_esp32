#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SIGNAL_DATA_SIZE      (1024 * 1024)  // 1MB max signal data
#define MAX_WEB_SOCKETS          4              // Maximum concurrent connections
#define SIGNAL_STORAGE_PATH       "/spiffs"

typedef struct {
    httpd_handle_t server;
    bool is_running;
} web_server_t;

/* Signal data structure for JSON serialization */
typedef struct {
    uint32_t timestamp;
    uint32_t frequency;
    uint32_t samples_count;
    char* data;                   // Base64 encoded signal data
    uint32_t data_size;
    char* filename;               // Signal filename
} signal_data_t;

/* Function prototypes */
esp_err_t web_server_start(void);
void web_server_stop(void);
bool web_server_is_running(void);
esp_err_t web_server_save_signal(const uint8_t* data, uint32_t size, const char* filename);
esp_err_t web_server_load_signal(const char* filename, signal_data_t* signal_data);
esp_err_t web_server_delete_signal(const char* filename);
esp_err_t web_server_list_signals(char* buffer, uint32_t buffer_size);

/* HTTPD URI handlers */
esp_err_t signal_capture_get_handler(httpd_req_t *req);
esp_err_t signal_capture_start_handler(httpd_req_t *req);
esp_err_t signal_capture_stop_handler(httpd_req_t *req);
esp_err_t signal_scan_start_handler(httpd_req_t *req);
esp_err_t signal_scan_stop_handler(httpd_req_t *req);
esp_err_t signal_scan_status_handler(httpd_req_t *req);
esp_err_t signal_transmit_handler(httpd_req_t *req);
esp_err_t signal_capture_post_handler(httpd_req_t *req);
esp_err_t signal_capture_delete_handler(httpd_req_t *req);
esp_err_t signal_capture_list_get_handler(httpd_req_t *req);
esp_err_t signal_file_get_handler(httpd_req_t *req);
esp_err_t web_ui_get_handler(httpd_req_t *req);
esp_err_t api_status_get_handler(httpd_req_t *req);
esp_err_t api_frequency_get_handler(httpd_req_t *req);
esp_err_t api_frequency_set_handler(httpd_req_t *req);

/* Base64 utility functions */
esp_err_t base64_encode(const uint8_t* data, uint32_t size, char* output, uint32_t* output_size);
esp_err_t base64_decode(const char* input, uint8_t* output, uint32_t* output_size);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H