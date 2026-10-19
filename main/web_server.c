#include "web_server.h"
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cJSON.h>
#include "cc1101_driver.h"

static const char *TAG = "WEB_SERVER";
static web_server_t server_state = {0};

static const char* MIME_TYPE_JSON = "application/json";
static const char* MIME_TYPE_TEXT_HTML = "text/html";
static const char* MIME_TYPE_APPLICATION_OCTET_STREAM = "application/octet-stream";

/* Base64 encoding table */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

esp_err_t base64_encode(const uint8_t* data, uint32_t size, char* output, uint32_t* output_size) {
    if (!data || !output || !output_size) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t j = 0;
    for (uint32_t i = 0; i < size; i += 3) {
        uint32_t remain = size - i;
        uint8_t b0 = data[i];
        uint8_t b1 = (remain > 1) ? data[i + 1] : 0;
        uint8_t b2 = (remain > 2) ? data[i + 2] : 0;

        output[j++] = base64_table[b0 >> 2];
        output[j++] = base64_table[((b0 & 0x03) << 4) | (b1 >> 4)];
        output[j++] = (remain > 1) ? base64_table[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=';
        output[j++] = (remain > 2) ? base64_table[b2 & 0x3F] : '=';
    }

    output[j] = '\0';
    *output_size = j;
    return ESP_OK;
}

static int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

esp_err_t base64_decode(const char* input, uint8_t* output, uint32_t* output_size) {
    if (!input || !output || !output_size) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t out_idx = 0;
    int quad[4];
    int qn = 0;

    for (const char* p = input; *p; p++) {
        if (*p == '=') {
            break;  /* padding marks the end of the payload */
        }
        int v = base64_value(*p);
        if (v < 0) {
            continue;  /* skip whitespace / invalid characters */
        }
        quad[qn++] = v;
        if (qn == 4) {
            output[out_idx++] = (uint8_t)((quad[0] << 2) | (quad[1] >> 4));
            output[out_idx++] = (uint8_t)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));
            output[out_idx++] = (uint8_t)(((quad[2] & 0x03) << 6) | quad[3]);
            qn = 0;
        }
    }

    if (qn == 2) {
        output[out_idx++] = (uint8_t)((quad[0] << 2) | (quad[1] >> 4));
    } else if (qn == 3) {
        output[out_idx++] = (uint8_t)((quad[0] << 2) | (quad[1] >> 4));
        output[out_idx++] = (uint8_t)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));
    }

    *output_size = out_idx;
    return ESP_OK;
}

/* ---- Signal file naming helpers ---- */

static bool is_safe_signal_filename(const char* filename) {
    if (!filename || filename[0] == '\0') {
        return false;
    }
    if (strchr(filename, '/') || strchr(filename, '\\')) {
        return false;
    }
    if (strstr(filename, "..")) {
        return false;
    }
    return true;
}

/* 315000000 -> "315", 433920000 -> "43392", 868350000 -> "86835" */
static void frequency_to_tag(uint32_t hz, char* out, size_t out_len) {
    uint32_t khz = (hz + 500U) / 1000U;
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)khz);

    size_t n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '0') {
        tmp[--n] = '\0';
    }
    snprintf(out, out_len, "%s", tmp);
}

/* Build the next filename for the current frequency: <tag>_<seq>, e.g. 315_1 */
static esp_err_t make_next_signal_filename(char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char tag[16];
    frequency_to_tag(cc1101_get_frequency(), tag, sizeof(tag));

    uint32_t max_seq = 0;
    DIR* dir = opendir(SIGNAL_STORAGE_PATH);
    if (dir) {
        size_t tag_len = strlen(tag);
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            const char* name = entry->d_name;
            if (strncmp(name, tag, tag_len) != 0 || name[tag_len] != '_') {
                continue;
            }
            const char* p = name + tag_len + 1;
            char* end = NULL;
            unsigned long seq = strtoul(p, &end, 10);
            if (end == p) {
                continue;
            }
            if (*end != '\0' && strcmp(end, ".dat") != 0) {
                continue;
            }
            if (seq > max_seq) {
                max_seq = (uint32_t)seq;
            }
        }
        closedir(dir);
    }

    snprintf(out, out_len, "%s_%lu", tag, (unsigned long)(max_seq + 1));
    return ESP_OK;
}

static esp_err_t load_signal_from_file(const char* filename, signal_data_t* signal_data) {
    if (!signal_data || !is_safe_signal_filename(filename)) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SIGNAL_STORAGE_PATH, filename);

    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGD(TAG, "Signal file not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > MAX_SIGNAL_DATA_SIZE) {
        fclose(f);
        ESP_LOGE(TAG, "Invalid file size: %ld", file_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Read file data
    uint8_t* file_data = (uint8_t*)malloc(file_size);
    if (!file_data) {
        fclose(f);
        ESP_LOGE(TAG, "Failed to allocate memory for file data");
        return ESP_ERR_NO_MEM;
    }
    
    size_t bytes_read = fread(file_data, 1, file_size, f);
    fclose(f);
    
    if (bytes_read != (size_t)file_size) {
        free(file_data);
        ESP_LOGE(TAG, "Failed to read entire file: %zu != %ld", bytes_read, file_size);
        return ESP_FAIL;
    }
    
    // Convert to base64
    uint32_t base64_size = file_size * 4 / 3 + 4;  // Rough estimate
    char* base64_data = (char*)malloc(base64_size);
    if (!base64_data) {
        free(file_data);
        ESP_LOGE(TAG, "Failed to allocate memory for base64 data");
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = base64_encode(file_data, file_size, base64_data, &base64_size);
    free(file_data);
    
    if (ret != ESP_OK) {
        free(base64_data);
        ESP_LOGE(TAG, "Failed to encode data to base64: %d", ret);
        return ret;
    }
    
    // Fill signal data structure
    signal_data->timestamp = esp_timer_get_time() / 1000000;
    signal_data->frequency = cc1101_get_frequency();
    signal_data->samples_count = file_size / 4;  // Assuming 4 bytes per sample
    signal_data->data_size = base64_size;
    signal_data->data = base64_data;
    
    size_t name_len = strlen(filename) + 1;
    signal_data->filename = (char*)malloc(name_len);
    if (!signal_data->filename) {
        free(base64_data);
        ESP_LOGE(TAG, "Failed to allocate memory for filename");
        return ESP_ERR_NO_MEM;
    }
    memcpy(signal_data->filename, filename, name_len);

    return ESP_OK;
}

static esp_err_t save_signal_to_file(const uint8_t* data, uint32_t size, const char* filename) {
    if (!data || size == 0 || !filename) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // SPIFFS is a flat filesystem, no subdirectories needed
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", SIGNAL_STORAGE_PATH, filename);
    
    // Write data to file
    FILE* f = fopen(full_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", full_path);
        return ESP_FAIL;
    }
    
    size_t bytes_written = fwrite(data, 1, size, f);
    fclose(f);
    
    if (bytes_written != size) {
        ESP_LOGE(TAG, "Failed to write entire file: %zu != %zu", bytes_written, (size_t)size);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Signal saved to: %s (%lu bytes)", full_path, (unsigned long)size);
    return ESP_OK;
}

static esp_err_t list_signals_json(char* output, uint32_t output_size) {
    if (!output) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Open SPIFFS root directory
    DIR* dir = opendir(SIGNAL_STORAGE_PATH);
    if (!dir) {
        ESP_LOGD(TAG, "Failed to open signal directory: %s", SIGNAL_STORAGE_PATH);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Create JSON array
    cJSON* signals_array = cJSON_CreateArray();
    if (!signals_array) {
        ESP_LOGE(TAG, "Failed to create signals array");
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }
    
    struct dirent* entry;
    uint32_t display_id = 0;

    while ((entry = readdir(dir)) != NULL) {
        /* SPIFFS is a flat filesystem; every non-hidden file is a signal. */
        if (entry->d_name[0] == '.') {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", SIGNAL_STORAGE_PATH, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        cJSON* signal_obj = cJSON_CreateObject();
        if (!signal_obj) {
            ESP_LOGE(TAG, "Failed to create signal object");
            continue;
        }

        cJSON_AddNumberToObject(signal_obj, "id", ++display_id);
        cJSON_AddStringToObject(signal_obj, "filename", entry->d_name);
        cJSON_AddNumberToObject(signal_obj, "size", st.st_size);
        cJSON_AddNumberToObject(signal_obj, "modified_time", st.st_mtime);

        cJSON_AddItemToArray(signals_array, signal_obj);
    }
    
    closedir(dir);
    
    // Convert JSON to string
    char* json_str = cJSON_Print(signals_array);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to create signals JSON string");
        cJSON_Delete(signals_array);
        return ESP_ERR_NO_MEM;
    }
    
    // Copy to output buffer
    size_t json_len = strlen(json_str);
    if (json_len + 1 > output_size) {
        ESP_LOGE(TAG, "Signals JSON output buffer too small: need %zu, have %zu", json_len + 1, output_size);
        free(json_str);
        cJSON_Delete(signals_array);
        return ESP_ERR_INVALID_SIZE;
    }
    
    strcpy(output, json_str);
    
    // Cleanup
    free(json_str);
    cJSON_Delete(signals_array);
    
    return ESP_OK;
}

esp_err_t web_server_save_signal(const uint8_t* data, uint32_t size, const char* filename) {
    if (!data || size == 0 || size > MAX_SIGNAL_DATA_SIZE) {
        ESP_LOGE(TAG, "Invalid signal data: size=%zu", size);
        return ESP_ERR_INVALID_ARG;
    }
    
    return save_signal_to_file(data, size, filename);
}

esp_err_t web_server_load_signal(const char* filename, signal_data_t* signal_data) {
    if (!signal_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize signal data structure
    memset(signal_data, 0, sizeof(signal_data_t));
    
    return load_signal_from_file(filename, signal_data);
}

esp_err_t web_server_delete_signal(const char* filename) {
    if (!is_safe_signal_filename(filename)) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SIGNAL_STORAGE_PATH, filename);
    
    if (remove(path) != 0) {
        ESP_LOGE(TAG, "Failed to delete signal file: %s", path);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Deleted signal file: %s", path);
    return ESP_OK;
}

esp_err_t web_server_list_signals(char* buffer, uint32_t buffer_size) {
    return list_signals_json(buffer, buffer_size);
}

// HTTPD URI handlers implementations
esp_err_t signal_capture_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Signal capture GET request received");
    
    // Create simple JSON response with status
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }
    
    cJSON_AddStringToObject(root, "status", "ready");
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000000);
    
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON response");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, MIME_TYPE_JSON);
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

/* ---- Pulse file format ---- */
#define PULSE_FILE_MAGIC 0x43313130u  /* 'C110' */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t pulse_count;
    int16_t  rssi;
    uint8_t  format;
    uint8_t  reserved;
    uint16_t pulses[CC1101_MAX_PULSES];
} pulse_file_t;

static bool s_detection_saved = false;
static char s_saved_filename[64];

static esp_err_t load_pulse_file(const char* filename, pulse_file_t* out)
{
    if (!is_safe_signal_filename(filename)) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SIGNAL_STORAGE_PATH, filename);
    FILE* f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t n = fread(out, 1, sizeof(*out), f);
    fclose(f);
    if (n < sizeof(pulse_file_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out->magic != PULSE_FILE_MAGIC || out->pulse_count > CC1101_MAX_PULSES) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static void send_json(httpd_req_t* req, cJSON* root)
{
    char* js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!js) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return;
    }
    httpd_resp_set_type(req, MIME_TYPE_JSON);
    httpd_resp_send(req, js, strlen(js));
    free(js);
}

/* ---- Working frequency selection ---- */

typedef struct {
    uint32_t hz;
    const char* label;
} freq_preset_t;

static const freq_preset_t s_freq_presets[] = {
    { 315000000U, "315.00 MHz 常见门禁/遥控" },
    { 330000000U, "330.00 MHz" },
    { 390000000U, "390.00 MHz" },
    { 418000000U, "418.00 MHz 车库门" },
    { 430000000U, "430.00 MHz" },
    { 433420000U, "433.42 MHz" },
    { 433920000U, "433.92 MHz 最常见" },
    { 443000000U, "443.00 MHz 本模块标称" },
    { 447000000U, "447.00 MHz" },
    { 868350000U, "868.35 MHz" },
    { 915000000U, "915.00 MHz" },
};

#define FREQ_PRESET_COUNT (sizeof(s_freq_presets) / sizeof(s_freq_presets[0]))

esp_err_t api_frequency_get_handler(httpd_req_t *req)
{
    uint32_t hz = cc1101_get_frequency();
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }

    cJSON_AddNumberToObject(root, "frequency", hz);
    cJSON_AddNumberToObject(root, "frequency_mhz", hz / 1000000.0);
    cJSON_AddBoolToObject(root, "cc1101_initialized", cc1101_is_initialized());

    cJSON* presets = cJSON_CreateArray();
    if (presets) {
        for (size_t i = 0; i < FREQ_PRESET_COUNT; i++) {
            cJSON* item = cJSON_CreateObject();
            if (!item) break;
            cJSON_AddNumberToObject(item, "hz", s_freq_presets[i].hz);
            cJSON_AddNumberToObject(item, "mhz", s_freq_presets[i].hz / 1000000.0);
            cJSON_AddStringToObject(item, "label", s_freq_presets[i].label);
            cJSON_AddItemToArray(presets, item);
        }
        cJSON_AddItemToObject(root, "presets", presets);
    }

    send_json(req, root);
    return ESP_OK;
}

esp_err_t api_frequency_set_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char val[32] = {0};
    uint32_t hz = 0;

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        query[0] == '\0') {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "缺少频率参数（hz 或 mhz）");
        send_json(req, root);
        return ESP_OK;
    }

    if (httpd_query_key_value(query, "hz", val, sizeof(val)) == ESP_OK) {
        hz = (uint32_t)strtoul(val, NULL, 10);
    } else if (httpd_query_key_value(query, "mhz", val, sizeof(val)) == ESP_OK) {
        double mhz = atof(val);
        hz = (uint32_t)(mhz * 1000000.0 + 0.5);
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "缺少频率参数（hz 或 mhz）");
        send_json(req, root);
        return ESP_OK;
    }

    if (hz < 100000000U || hz > 1000000000U) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "频率超出范围");
        send_json(req, root);
        return ESP_OK;
    }

    if (!cc1101_is_initialized() && cc1101_init() != ESP_OK) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "CC1101 初始化失败");
        send_json(req, root);
        return ESP_OK;
    }

    /* Changing frequency while scanning would leave the scan task on a stale
     * band; stop it first. */
    if (cc1101_scan_state() != CC1101_SCAN_IDLE) {
        cc1101_scan_stop();
    }

    esp_err_t ret = cc1101_set_frequency(hz);
    if (ret == ESP_OK) {
        cJSON_AddStringToObject(root, "status", "ok");
        cJSON_AddStringToObject(root, "message", "频率已切换");
        cJSON_AddNumberToObject(root, "frequency", hz);
        cJSON_AddNumberToObject(root, "frequency_mhz", hz / 1000000.0);
    } else if (ret == ESP_ERR_INVALID_ARG) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "CC1101 不支持该频率（仅 300-348 / 387-464 / 779-928 MHz）");
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "设置频率失败");
    }
    send_json(req, root);
    return ESP_OK;
}

esp_err_t signal_scan_start_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Scan START request");

    if (!cc1101_is_initialized()) {
        if (cc1101_init() != ESP_OK) {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "error");
            cJSON_AddStringToObject(root, "message", "CC1101 初始化失败");
            send_json(req, root);
            return ESP_OK;
        }
    }

    if (cc1101_check_connection() != ESP_OK) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "CC1101 未连接，请检查接线");
        send_json(req, root);
        return ESP_OK;
    }

    cc1101_config_t cfg = {
        .frequency = cc1101_get_frequency(),
        .channel = CC1101_CHANNEL_1,
        .modulation = CC1101_MODULATION_GFSK,
        .sample_rate = CC1101_SIGNAL_SAMPLE_RATE,
        .continuous = false,
    };
    cc1101_write_config(&cfg);

    s_detection_saved = false;
    esp_err_t ret = cc1101_scan_start();

    cJSON* root = cJSON_CreateObject();
    if (ret == ESP_OK) {
        cJSON_AddStringToObject(root, "status", "scanning");
        cJSON_AddStringToObject(root, "message", "扫描中，请按下遥控器");
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "启动扫描失败（可能已在扫描）");
    }
    send_json(req, root);
    return ESP_OK;
}

esp_err_t signal_scan_stop_handler(httpd_req_t *req)
{
    cc1101_scan_stop();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "idle");
    cJSON_AddStringToObject(root, "message", "已停止扫描");
    send_json(req, root);
    return ESP_OK;
}

esp_err_t signal_scan_status_handler(httpd_req_t *req)
{
    cc1101_scan_state_t st = cc1101_scan_state();
    cJSON* root = cJSON_CreateObject();

    if (st == CC1101_SCAN_DETECTED) {
        cc1101_capture_result_t r;
        if (cc1101_scan_get_result(&r) == ESP_OK) {
            if (!s_detection_saved) {
                make_next_signal_filename(s_saved_filename, sizeof(s_saved_filename));
                pulse_file_t* pf = (pulse_file_t*)calloc(1, sizeof(pulse_file_t));
                if (pf) {
                    pf->magic = PULSE_FILE_MAGIC;
                    pf->version = 1;
                    pf->pulse_count = (uint16_t)r.count;
                    pf->rssi = (int16_t)r.rssi;
                    pf->format = r.format;
                    memcpy(pf->pulses, r.pulses, r.count * sizeof(uint16_t));
                    if (web_server_save_signal((const uint8_t*)pf,
                                               sizeof(pulse_file_t), s_saved_filename) == ESP_OK) {
                        s_detection_saved = true;
                    }
                    free(pf);
                }
            }

            if (s_detection_saved) {
                cJSON_AddStringToObject(root, "filename", s_saved_filename);
            }
            cJSON_AddStringToObject(root, "state", "detected");
            cJSON_AddStringToObject(root, "format", r.format_name);
            cJSON_AddNumberToObject(root, "rssi", r.rssi);
            cJSON_AddNumberToObject(root, "pulses", r.count);
            cJSON_AddNumberToObject(root, "duration_us", r.duration_us);
            cJSON_AddBoolToObject(root, "saved", s_detection_saved);
        } else {
            cJSON_AddStringToObject(root, "state", "idle");
        }
    } else if (st == CC1101_SCAN_RUNNING) {
        cJSON_AddStringToObject(root, "state", "scanning");
    } else {
        cJSON_AddStringToObject(root, "state", "idle");
    }

    cJSON_AddNumberToObject(root, "frequency", cc1101_get_frequency());
    cJSON_AddNumberToObject(root, "frequency_mhz", cc1101_get_frequency() / 1000000.0);
    send_json(req, root);
    return ESP_OK;
}

esp_err_t signal_transmit_handler(httpd_req_t *req)
{
    char query[128];
    char file[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", file, sizeof(file)) != ESP_OK ||
        !is_safe_signal_filename(file)) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "缺少或无效的 file 参数");
        send_json(req, root);
        return ESP_OK;
    }

    pulse_file_t* pf = (pulse_file_t*)calloc(1, sizeof(pulse_file_t));
    cJSON* root = cJSON_CreateObject();
    if (!pf) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "内存不足");
        send_json(req, root);
        return ESP_OK;
    }

    if (load_pulse_file(file, pf) != ESP_OK) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "信号文件无效或不存在");
        free(pf);
        send_json(req, root);
        return ESP_OK;
    }

    esp_err_t ret = cc1101_transmit_pulses(pf->pulses, pf->pulse_count);
    if (ret == ESP_OK) {
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "message", "已发射信号");
        cJSON_AddNumberToObject(root, "pulses", pf->pulse_count);
        cJSON_AddStringToObject(root, "filename", file);
        cJSON_AddNumberToObject(root, "frequency", cc1101_get_frequency());
        cJSON_AddNumberToObject(root, "frequency_mhz", cc1101_get_frequency() / 1000000.0);
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "发射失败");
    }
    free(pf);
    send_json(req, root);
    return ESP_OK;
}

esp_err_t signal_capture_start_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Signal capture START request received");
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }
    
    /* Make sure the driver is initialized */
    if (!cc1101_is_initialized()) {
        if (cc1101_init() != ESP_OK) {
            cJSON_AddStringToObject(root, "status", "error");
            cJSON_AddStringToObject(root, "message", "CC1101 初始化失败");
            char* js = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, MIME_TYPE_JSON);
            httpd_resp_send(req, js, strlen(js));
            free(js);
            return ESP_OK;
        }
        cc1101_config_t cfg = {
            .frequency = cc1101_get_frequency(),
            .channel = CC1101_CHANNEL_1,
            .modulation = CC1101_MODULATION_GFSK,
            .sample_rate = CC1101_SIGNAL_SAMPLE_RATE,
            .continuous = false,
        };
        cc1101_write_config(&cfg);
    }
    
    /* Verify chip presence */
    if (cc1101_check_connection() != ESP_OK) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "CC1101 未连接，请检查接线");
        char* js = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, MIME_TYPE_JSON);
        httpd_resp_send(req, js, strlen(js));
        free(js);
        return ESP_OK;
    }
    
    esp_err_t ret = cc1101_capture_start();
    if (ret != ESP_OK) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "启动采集失败，可能正在采集");
        char* js = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, MIME_TYPE_JSON);
        httpd_resp_send(req, js, strlen(js));
        free(js);
        return ESP_OK;
    }
    
    /* Wait for the capture to finish (max ~4 s) */
    for (int i = 0; i < 40 && !cc1101_capture_is_done(); i++) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    cc1101_capture_stop();
    
    uint32_t len = 0;
    uint8_t* data = cc1101_capture_get_data(&len);
    
    if (data == NULL || len == 0) {
        cJSON_AddStringToObject(root, "status", "empty");
        cJSON_AddStringToObject(root, "message", "未捕获到信号，请靠近遥控器再试");
        cJSON_AddNumberToObject(root, "size", 0);
    } else {
        char filename[64];
        make_next_signal_filename(filename, sizeof(filename));
        
        ret = web_server_save_signal(data, len, filename);
        if (ret != ESP_OK) {
            cJSON_AddStringToObject(root, "status", "error");
            cJSON_AddStringToObject(root, "message", "保存信号失败");
        } else {
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "message", "捕获成功");
            cJSON_AddStringToObject(root, "filename", filename);
            cJSON_AddNumberToObject(root, "size", len);
        }
    }
    
    char* js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, MIME_TYPE_JSON);
    httpd_resp_send(req, js, strlen(js));
    free(js);
    
    return ESP_OK;
}

esp_err_t signal_capture_stop_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Signal capture STOP request received");
    
    cc1101_capture_stop();
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "status", "success");
    cJSON_AddStringToObject(root, "message", "已停止采集");
    
    char* js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, MIME_TYPE_JSON);
    httpd_resp_send(req, js, strlen(js));
    free(js);
    
    return ESP_OK;
}

esp_err_t signal_capture_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Signal capture POST request received");
    
    // Get content length
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }
    
    // Allocate buffer for request body
    char* buffer = (char*)malloc(content_len + 1);
    if (!buffer) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate buffer");
        return ESP_FAIL;
    }
    
    // Read request body
    int ret = httpd_req_recv(req, buffer, content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request body");
        free(buffer);
        return ESP_FAIL;
    }
    buffer[ret] = '\0';
    
    // Parse JSON request
    cJSON* root = cJSON_Parse(buffer);
    free(buffer);
    
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON in request body");
        return ESP_FAIL;
    }
    
    // Get signal data from JSON
    cJSON* data_json = cJSON_GetObjectItem(root, "data");
    if (!data_json || !cJSON_IsString(data_json)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'data' field");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Decode base64 signal data
    uint32_t decoded_size = content_len * 3 / 4 + 1;  // Rough estimate
    uint8_t* decoded_data = (uint8_t*)malloc(decoded_size);
    if (!decoded_data) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate decode buffer");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    esp_err_t decode_ret = base64_decode(data_json->valuestring, decoded_data, &decoded_size);
    cJSON_Delete(root);
    
    if (decode_ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to decode base64 data");
        free(decoded_data);
        return ESP_FAIL;
    }
    
    // Validate decoded data size
    if (decoded_size <= 0 || decoded_size > MAX_SIGNAL_DATA_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid signal data size");
        free(decoded_data);
        return ESP_FAIL;
    }
    
    // Create filename
    char filename[64];
    make_next_signal_filename(filename, sizeof(filename));
    
    // Save signal data
    decode_ret = save_signal_to_file(decoded_data, decoded_size, filename);
    free(decoded_data);
    
    if (decode_ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save signal data");
        return ESP_FAIL;
    }
    
    // Create success response
    cJSON* response = cJSON_CreateObject();
    if (!response) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create response");
        return ESP_FAIL;
    }
    
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Signal captured and saved successfully");
    cJSON_AddStringToObject(response, "filename", filename);
    cJSON_AddNumberToObject(response, "size", decoded_size);
    cJSON_AddNumberToObject(response, "timestamp", esp_timer_get_time() / 1000000);
    
    char* json_str = cJSON_Print(response);
    cJSON_Delete(response);
    
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create response JSON");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, MIME_TYPE_JSON);
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    ESP_LOGI(TAG, "Signal captured successfully: %s (%lu bytes)", filename, (unsigned long)decoded_size);
    return ESP_OK;
}

esp_err_t signal_capture_delete_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Signal capture DELETE request received");
    
    // Parse filename from query parameter
    char query_str[256];
    char file[64];
    if (httpd_req_get_url_query_str(req, query_str, sizeof(query_str)) != ESP_OK ||
        httpd_query_key_value(query_str, "file", file, sizeof(file)) != ESP_OK ||
        !is_safe_signal_filename(file)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid filename");
        return ESP_FAIL;
    }
    
    esp_err_t ret = web_server_delete_signal(file);
    
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete signal");
        return ESP_FAIL;
    }
    
    // Create success response
    cJSON* response = cJSON_CreateObject();
    if (!response) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create response");
        return ESP_FAIL;
    }
    
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Signal deleted successfully");
    cJSON_AddStringToObject(response, "filename", file);
    
    char* json_str = cJSON_Print(response);
    cJSON_Delete(response);
    
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create response JSON");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, MIME_TYPE_JSON);
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

esp_err_t signal_capture_list_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Signal list GET request received");
    
    // Allocate buffer for JSON response
    char* buffer = (char*)malloc(4096);
    if (!buffer) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate buffer");
        return ESP_FAIL;
    }
    
    esp_err_t ret = web_server_list_signals(buffer, 4096);
    
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to list signals");
        free(buffer);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, MIME_TYPE_JSON);
    httpd_resp_send(req, buffer, strlen(buffer));
    free(buffer);
    
    return ESP_OK;
}

esp_err_t signal_file_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Signal file GET request received");
    
    const char* prefix = "/api/signal/download/";
    const char* filename = req->uri + strlen(prefix);

    if (!is_safe_signal_filename(filename)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename in URI");
        return ESP_FAIL;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SIGNAL_STORAGE_PATH, filename);

    FILE* f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Signal not found");
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0 || file_size > MAX_SIGNAL_DATA_SIZE) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid signal file size");
        return ESP_FAIL;
    }

    uint8_t* data = (uint8_t*)malloc(file_size);
    if (!data) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate file buffer");
        return ESP_FAIL;
    }

    size_t bytes_read = fread(data, 1, file_size, f);
    fclose(f);
    if (bytes_read != (size_t)file_size) {
        free(data);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read signal file");
        return ESP_FAIL;
    }

    char disposition[96];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_type(req, MIME_TYPE_APPLICATION_OCTET_STREAM);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    httpd_resp_send(req, (const char*)data, file_size);
    free(data);

    return ESP_OK;
}

esp_err_t web_ui_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Web UI GET request received");
    
    const char* html_content =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>CC1101 遥控信号管理器</title>"
        "<style>"
        "body { font-family: \"Microsoft YaHei\", Arial, sans-serif; margin: 20px; background: #f5f5f5; }"
        ".container { max-width: 1000px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,.1); }"
        "h1 { color: #333; text-align: center; }"
        ".section { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }"
        ".status { background: #e8f5e8; }"
        ".controls { background: #f0f8ff; }"
        "button { background: #4CAF50; color: #fff; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; margin: 5px; }"
        "button:hover { background: #45a049; }"
        "button:disabled { background: #999; cursor: not-allowed; }"
        "button.stop { background: #f44336; }"
        "button.stop:hover { background: #da190b; }"
        "button.danger { background: #e91e63; }"
        "input[type=\"file\"] { display: none; }"
        ".file-label { background: #2196F3; color: #fff; padding: 10px 15px; border-radius: 4px; cursor: pointer; display: inline-block; margin: 5px; }"
        "table { width: 100%; border-collapse: collapse; margin-top: 15px; }"
        "th, td { border: 1px solid #ddd; padding: 10px; text-align: left; }"
        "th { background: #f2f2f2; }"
        ".signal-data { font-family: monospace; white-space: pre-wrap; background: #f5f5f5; padding: 10px; border-radius: 4px; max-height: 300px; overflow: auto; }"
        ".freq { background: #fffbe6; }"
        "select, input[type=\"number\"] { padding: 8px; border: 1px solid #ccc; border-radius: 4px; margin: 5px; font-size: 14px; }"
        "#freqStatus { margin-top: 10px; font-weight: bold; color: #b26a00; }"
        ".note { color: #666; font-size: 13px; }"
        ".tx-ok { color: #2e7d32; font-weight: bold; }"
        ".tx-err { color: #c62828; font-weight: bold; }"
        ".tx-pending { color: #b26a00; font-weight: bold; }"
        "#scanStatus { margin-top: 10px; font-weight: bold; color: #0b7dda; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class=\"container\">"
        "<h1>CC1101 遥控信号管理器</h1>"
        "<div class=\"section status\">"
        "<h2>系统状态</h2>"
        "<p><strong>系统状态：</strong><span id=\"status\">加载中...</span></p>"
        "<p><strong>CC1101：</strong><span id=\"cc1101\">检测中...</span></p>"
        "<p><strong>无线模式：</strong>SoftAP（http://192.168.4.1）</p>"
        "</div>"
        "<div class=\"section freq\">"
        "<h2>工作频率</h2>"
        "<select id=\"freqSelect\" onchange=\"onFreqSelectChange()\">"
        "<option value=\"315000000\">315.00 MHz 常见门禁/遥控</option>"
        "<option value=\"330000000\">330.00 MHz</option>"
        "<option value=\"390000000\">390.00 MHz</option>"
        "<option value=\"418000000\">418.00 MHz 车库门</option>"
        "<option value=\"430000000\">430.00 MHz</option>"
        "<option value=\"433420000\">433.42 MHz</option>"
        "<option value=\"433920000\">433.92 MHz 最常用</option>"
        "<option value=\"447000000\">447.00 MHz</option>"
        "<option value=\"868350000\">868.35 MHz</option>"
        "<option value=\"915000000\">915.00 MHz</option>"
        "<option value=\"custom\">自定义...</option>"
        "</select>"
        "<input type=\"number\" id=\"freqCustom\" step=\"0.01\" min=\"300\" max=\"928\" placeholder=\"自定义 MHz\">"
        "<button onclick=\"applyCustomFreq()\">应用自定义</button>"
        "<div id=\"freqStatus\">当前工作频率：读取中...</div>"
        "<p class=\"note\">CC1101 硬件支持 300-348 / 387-464 / 779-928 MHz。模块标称 443 MHz，若用 315 遥控器请选择 315；天线与频率不匹配时接收距离会明显变短，测试时请靠近。</p>"
        "</div>"
        "<div class=\"section controls\">"
        "<h2>信号捕获 / 控制</h2>"
        "<button id=\"startBtn\" onclick=\"startCapture()\">开始捕获</button>"
        "<button class=\"stop\" onclick=\"stopCapture()\">停止捕获</button>"
        "<label class=\"file-label\">上传信号文件<input type=\"file\" id=\"signalFileInput\" onchange=\"uploadSignalFile()\"></label>"
        "<p>点击“开始捕获”后会持续扫描，按下遥控器后自动识别并保存。</p>"
        "<div id=\"scanStatus\">未开始</div>"
        "</div>"
        "<div class=\"section\">"
        "<h2>已保存信号</h2>"
        "<div id=\"signalList\">加载中...</div>"
        "</div>"
        "<div class=\"section\">"
        "<h2>捕获数据</h2>"
        "<div id=\"signalData\" class=\"signal-data\">暂无数据</div>"
        "</div>"
        "<div class=\"section\">"
        "<h2>接口状态</h2>"
        "<div id=\"apiStatus\">加载中...</div>"
        "</div>"
        "</div>"
        "<script>"
        "let scanTimer = null;"
        "let currentFreq = 0;"
        "let txStats = {};"
        "try { txStats = JSON.parse(localStorage.getItem(\"cc1101_tx_stats\") || \"{}\"); } catch (e) { txStats = {}; }"
        "function saveTxStats() { try { localStorage.setItem(\"cc1101_tx_stats\", JSON.stringify(txStats)); } catch (e) {} }"
        "function txStatusText(key) {"
        "  const st = txStats[key];"
        "  if (!st || !st.last) return \"-\";"
        "  if (st.last === \"success\") return \"发射成功\";"
        "  if (st.last === \"pending\") return \"发射中...\";"
        "  return \"发射失败\" + (st.message ? \"：\" + st.message : \"\");"
        "}"
        "function txStatusClass(key) {"
        "  const st = txStats[key];"
        "  if (!st || !st.last) return \"\";"
        "  if (st.last === \"success\") return \"tx-ok\";"
        "  if (st.last === \"pending\") return \"tx-pending\";"
        "  return \"tx-err\";"
        "}"
        "function updateTxRow(key, id) {"
        "  const statusEl = document.getElementById(\"txStatus\" + id);"
        "  const countEl = document.getElementById(\"txCount\" + id);"
        "  const st = txStats[key];"
        "  if (statusEl) {"
        "    statusEl.textContent = txStatusText(key);"
        "    statusEl.className = txStatusClass(key);"
        "  }"
        "  if (countEl) countEl.textContent = (st && st.ok) ? st.ok : 0;"
        "}"
        "function freqText(hz) { return (hz / 1000000).toFixed(3) + \" MHz\"; }"
        "function updateFreqDisplay(hz) {"
        "  currentFreq = hz;"
        "  document.getElementById(\"freqStatus\").textContent = \"当前工作频率：\" + freqText(hz);"
        "}"
        "function loadFrequency() {"
        "  fetch(\"/api/frequency\").then(r => r.json()).then(d => {"
        "    updateFreqDisplay(d.frequency);"
        "    const sel = document.getElementById(\"freqSelect\");"
        "    let matched = false;"
        "    for (let i = 0; i < sel.options.length; i++) {"
        "      if (Number(sel.options[i].value) === d.frequency) { sel.selectedIndex = i; matched = true; break; }"
        "    }"
        "    if (!matched) {"
        "      sel.value = \"custom\";"
        "      document.getElementById(\"freqCustom\").value = (d.frequency / 1000000).toFixed(2);"
        "    }"
        "  }).catch(() => { document.getElementById(\"freqStatus\").textContent = \"频率读取失败\"; });"
        "}"
        "function setFrequency(hz) {"
        "  return fetch(\"/api/frequency?hz=\" + hz, { method: \"POST\" })"
        "    .then(r => r.json())"
        "    .then(d => {"
        "      if (d.status !== \"ok\") { alert(d.message || \"设置频率失败\"); return false; }"
        "      updateFreqDisplay(d.frequency);"
        "      return true;"
        "    })"
        "    .catch(() => { alert(\"设置频率失败\"); return false; });"
        "}"
        "function selectedFrequency() {"
        "  const sel = document.getElementById(\"freqSelect\");"
        "  if (sel.value !== \"custom\") return Number(sel.value);"
        "  const mhz = parseFloat(document.getElementById(\"freqCustom\").value);"
        "  if (!isFinite(mhz) || mhz < 300 || mhz > 928) return 0;"
        "  return Math.round(mhz * 1000000);"
        "}"
        "function applySelectedFrequency() {"
        "  const hz = selectedFrequency();"
        "  if (!hz) { alert(\"请输入 300-928 之间的频率（MHz）\"); return Promise.resolve(false); }"
        "  return setFrequency(hz);"
        "}"
        "function onFreqSelectChange() {"
        "  const sel = document.getElementById(\"freqSelect\");"
        "  if (sel.value === \"custom\") { document.getElementById(\"freqCustom\").focus(); return; }"
        "  applySelectedFrequency();"
        "}"
        "function applyCustomFreq() {"
        "  document.getElementById(\"freqSelect\").value = \"custom\";"
        "  applySelectedFrequency();"
        "}"
        "function refreshStatus() {"
        "  fetch(\"/api/status\").then(r => r.json()).then(d => {"
        "    document.getElementById(\"status\").textContent = (d.status === \"running\") ? \"运行中\" : (d.status || \"未知\");"
        "    document.getElementById(\"cc1101\").textContent = (d.cc1101_status === \"connected\") ? \"已连接\" : \"未连接\";"
        "    if (d.frequency) updateFreqDisplay(d.frequency);"
        "  }).catch(() => {"
        "    document.getElementById(\"status\").textContent = \"错误\";"
        "    document.getElementById(\"cc1101\").textContent = \"错误\";"
        "  });"
        "}"
        "function refreshSignalList() {"
        "  fetch(\"/api/signal/list\").then(r => r.json()).then(data => {"
        "    if (!Array.isArray(data) || data.length === 0) {"
        "      document.getElementById(\"signalList\").innerHTML = \"<p>暂无保存的信号</p>\";"
        "      return;"
        "    }"
        "    let html = \"<table><tr><th>ID</th><th>文件名</th><th>大小</th><th>操作</th><th>发射结果</th><th>成功次数</th></tr>\";"
        "    data.forEach(s => {"
        "      const st = txStats[s.filename];"
        "      const okCount = (st && st.ok) ? st.ok : 0;"
        "      const pending = st && st.last === \"pending\";"
        "      html += \"<tr><td>\" + s.id + \"</td><td>\" + s.filename + \"</td><td>\" + s.size + \" 字节</td>\";"
        "      html += \"<td><button onclick=\\\"transmitSignal('\" + s.filename + \"', this, \" + s.id + \")\\\"\" + (pending ? \" disabled\" : \"\") + \">\" + (pending ? \"发射中...\" : \"发射\") + \"</button>\";"
        "      html += \"<button onclick=\\\"loadSignalData('\" + s.filename + \"')\\\">查看</button>\";"
        "      html += \"<button class=\\\"danger\\\" onclick=\\\"deleteSignal('\" + s.filename + \"', \" + s.id + \")\\\">删除</button></td>\";"
        "      html += \"<td id=\\\"txStatus\" + s.id + \"\\\" class=\\\"\" + txStatusClass(s.filename) + \"\\\">\" + txStatusText(s.filename) + \"</td>\";"
        "      html += \"<td id=\\\"txCount\" + s.id + \"\\\">\" + okCount + \"</td></tr>\";"
        "    });"
        "    html += \"</table>\";"
        "    document.getElementById(\"signalList\").innerHTML = html;"
        "  }).catch(() => {"
        "    document.getElementById(\"signalList\").innerHTML = \"加载信号列表失败\";"
        "  });"
        "}"
        "function refreshAPIStatus() {"
        "  fetch(\"/api/status\").then(r => r.json()).then(data => {"
        "    let html = \"<ul>\";"
        "    Object.keys(data).forEach(k => { html += \"<li><strong>\" + k + \"：</strong>\" + data[k] + \"</li>\"; });"
        "    html += \"</ul>\";"
        "    document.getElementById(\"apiStatus\").innerHTML = html;"
        "  }).catch(() => {"
        "    document.getElementById(\"apiStatus\").innerHTML = \"加载接口状态失败\";"
        "  });"
        "}"
        "function resetScanButton() {"
        "  const btn = document.getElementById(\"startBtn\");"
        "  btn.disabled = false;"
        "  btn.textContent = \"开始捕获\";"
        "}"
        "function startCapture() {"
        "  const btn = document.getElementById(\"startBtn\");"
        "  btn.disabled = true;"
        "  btn.textContent = \"扫描中...\";"
        "  document.getElementById(\"scanStatus\").textContent = \"正在应用频率并启动扫描...\";"
        "  applySelectedFrequency().then(ok => {"
        "    if (!ok) {"
        "      document.getElementById(\"scanStatus\").textContent = \"频率设置失败\";"
        "      resetScanButton();"
        "      return;"
        "    }"
        "    fetch(\"/api/signal/scan/start\", { method: \"POST\" })"
        "      .then(r => r.json())"
        "      .then(d => {"
        "        if (d.status === \"error\") {"
        "          alert(d.message);"
        "          document.getElementById(\"scanStatus\").textContent = d.message || \"启动失败\";"
        "          resetScanButton();"
        "          return;"
        "        }"
        "        document.getElementById(\"scanStatus\").textContent ="
        "          \"正在 \" + freqText(currentFreq) + \" 扫描，请按下遥控器...\";"
        "        if (scanTimer) clearInterval(scanTimer);"
        "        scanTimer = setInterval(pollScanStatus, 500);"
        "      })"
        "      .catch(() => { alert(\"启动扫描失败\"); resetScanButton(); });"
        "  });"
        "}"
        "function pollScanStatus() {"
        "  fetch(\"/api/signal/scan/status\").then(r => r.json()).then(d => {"
        "    if (d.state === \"scanning\") {"
        "      document.getElementById(\"scanStatus\").textContent ="
        "        \"正在 \" + freqText(d.frequency || currentFreq) + \" 扫描，请按下遥控器...\";"
        "    } else if (d.state === \"detected\") {"
        "      if (scanTimer) { clearInterval(scanTimer); scanTimer = null; }"
        "      document.getElementById(\"scanStatus\").innerHTML ="
        "        \"检测到信号：<b>\" + d.format + \"</b>，RSSI \" + d.rssi + \" dBm，\" +"
        "        d.pulses + \" 个脉冲，\" + d.duration_us + \" us\" +"
        "        (d.saved ? \"（已保存 \" + d.filename + \"）\" : \"\");"
        "      resetScanButton();"
        "      refreshSignalList();"
        "    } else {"
        "      if (scanTimer) { clearInterval(scanTimer); scanTimer = null; }"
        "      document.getElementById(\"scanStatus\").textContent = \"已停止\";"
        "      resetScanButton();"
        "    }"
        "  }).catch(() => {});"
        "}"
        "function stopCapture() {"
        "  if (scanTimer) { clearInterval(scanTimer); scanTimer = null; }"
        "  fetch(\"/api/signal/scan/stop\", { method: \"POST\" })"
        "    .then(r => r.json())"
        "    .then(d => {"
        "      document.getElementById(\"scanStatus\").textContent = d.message || \"已停止\";"
        "      resetScanButton();"
        "    })"
        "    .catch(() => resetScanButton());"
        "}"
        "function uploadSignalFile() {"
        "  const input = document.getElementById(\"signalFileInput\");"
        "  if (!input.files || !input.files[0]) return;"
        "  const file = input.files[0];"
        "  const reader = new FileReader();"
        "  reader.onload = function(e) {"
        "    const bytes = new Uint8Array(e.target.result);"
        "    let binary = \"\";"
        "    for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);"
        "    const b64 = btoa(binary);"
        "    fetch(\"/api/signal/capture\", {"
        "      method: \"POST\","
        "      headers: { \"Content-Type\": \"application/json\" },"
        "      body: JSON.stringify({ type: \"upload\", data: b64 })"
        "    }).then(r => r.json()).then(d => { alert(d.message || \"上传完成\"); refreshSignalList(); })"
        "      .catch(() => alert(\"上传失败\"));"
        "  };"
        "  reader.readAsArrayBuffer(file);"
        "}"
        "function loadSignalData(filename) {"
        "  fetch(\"/api/signal/download/\" + encodeURIComponent(filename))"
        "    .then(r => r.arrayBuffer())"
        "    .then(buf => {"
        "      const b = new Uint8Array(buf);"
        "      const hex = Array.from(b).slice(0, 512).map(x => x.toString(16).padStart(2, \"0\")).join(\" \");"
        "      document.getElementById(\"signalData\").textContent = hex + (b.length > 512 ? \" ... (共 \" + b.length + \" 字节)\" : \"\");"
        "    }).catch(() => { document.getElementById(\"signalData\").textContent = \"加载失败\"; });"
        "}"
        "function deleteSignal(filename, id) {"
        "  if (!confirm(\"确定删除信号 \" + filename + \"？\")) return;"
        "  fetch(\"/api/signal/capture?file=\" + encodeURIComponent(filename), { method: \"DELETE\" })"
        "    .then(r => r.json()).then(d => {"
        "      delete txStats[filename]; saveTxStats();"
        "      alert(d.message || \"已删除\"); refreshSignalList();"
        "    })"
        "    .catch(() => alert(\"删除失败\"));"
        "}"
        "function transmitSignal(filename, btn, id) {"
        "  const st = txStats[filename] || (txStats[filename] = { ok: 0, fail: 0 });"
        "  st.last = \"pending\";"
        "  st.message = \"\";"
        "  updateTxRow(filename, id);"
        "  if (btn) { btn.disabled = true; btn.textContent = \"发射中...\"; }"
        "  fetch(\"/api/signal/transmit?file=\" + encodeURIComponent(filename), { method: \"POST\" })"
        "    .then(r => r.json())"
        "    .then(d => {"
        "      if (d.status === \"success\") {"
        "        st.ok = (st.ok || 0) + 1;"
        "        st.last = \"success\";"
        "        st.message = \"\";"
        "      } else {"
        "        st.fail = (st.fail || 0) + 1;"
        "        st.last = \"error\";"
        "        st.message = d.message || \"发射失败\";"
        "      }"
        "      saveTxStats();"
        "      updateTxRow(filename, id);"
        "    })"
        "    .catch(() => {"
        "      st.fail = (st.fail || 0) + 1;"
        "      st.last = \"error\";"
        "      st.message = \"网络错误\";"
        "      saveTxStats();"
        "      updateTxRow(filename, id);"
        "    })"
        "    .finally(() => {"
        "      if (btn) { btn.disabled = false; btn.textContent = \"发射\"; }"
        "    });"
        "}"
        "document.addEventListener(\"DOMContentLoaded\", () => {"
        "  for (const k in txStats) { if (txStats[k] && txStats[k].last === \"pending\") txStats[k].last = \"\"; }"
        "  saveTxStats();"
        "  refreshStatus(); refreshSignalList(); refreshAPIStatus(); loadFrequency();"
        "  setInterval(() => { refreshStatus(); refreshAPIStatus(); }, 5000);"
        "});"
        "</script>"
        "</body>"
        "</html>"
        ;
    
    httpd_resp_set_type(req, MIME_TYPE_TEXT_HTML);
    httpd_resp_send(req, html_content, strlen(html_content));
    return ESP_OK;
}

esp_err_t api_status_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "API status GET request received");
    
    // Check CC1101 connection status
    const char* cc1101_status = "disconnected";
    if (cc1101_check_connection() == ESP_OK) {
        cc1101_status = "connected";
    }
    
    // Create status JSON
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }
    
    cJSON_AddStringToObject(root, "status", "running");
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000000);
    cJSON_AddStringToObject(root, "wifi_status", "connected");
    cJSON_AddStringToObject(root, "cc1101_status", cc1101_status);
    cJSON_AddStringToObject(root, "web_server_status", "active");
    cJSON_AddNumberToObject(root, "frequency", cc1101_get_frequency());
    cJSON_AddNumberToObject(root, "frequency_mhz", cc1101_get_frequency() / 1000000.0);
    
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON response");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, MIME_TYPE_JSON);
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    ESP_LOGI(TAG, "Initializing HTTP server...");
    ESP_LOGI(TAG, "Available heap: %zu bytes", esp_get_free_heap_size());
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 4;
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Calling httpd_start...");
    if (httpd_start(&server_state.server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server!");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "HTTPD started successfully, handle=%p", (void*)server_state.server);
    
    httpd_uri_t uri;
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/";
    uri.method = HTTP_GET;
    uri.handler = web_ui_get_handler;
    uri.user_ctx = NULL;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI handler for /");
    } else {
        ESP_LOGI(TAG, "Registered handler: /");
    }
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/capture";
    uri.method = HTTP_GET;
    uri.handler = signal_capture_get_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/start";
    uri.method = HTTP_POST;
    uri.handler = signal_capture_start_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }

    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/scan/start";
    uri.method = HTTP_POST;
    uri.handler = signal_scan_start_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }

    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/scan/stop";
    uri.method = HTTP_POST;
    uri.handler = signal_scan_stop_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }

    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/scan/status";
    uri.method = HTTP_GET;
    uri.handler = signal_scan_status_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }

    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/transmit";
    uri.method = HTTP_POST;
    uri.handler = signal_transmit_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/stop";
    uri.method = HTTP_POST;
    uri.handler = signal_capture_stop_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/capture";
    uri.method = HTTP_POST;
    uri.handler = signal_capture_post_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/capture";
    uri.method = HTTP_DELETE;
    uri.handler = signal_capture_delete_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/list";
    uri.method = HTTP_GET;
    uri.handler = signal_capture_list_get_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/download/*";
    uri.method = HTTP_GET;
    uri.handler = signal_file_get_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/status";
    uri.method = HTTP_GET;
    uri.handler = api_status_get_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }

    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/frequency";
    uri.method = HTTP_GET;
    uri.handler = api_frequency_get_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }

    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/frequency";
    uri.method = HTTP_POST;
    uri.handler = api_frequency_set_handler;
    if (httpd_register_uri_handler(server_state.server, &uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI: %s", uri.uri);
    }
    
    server_state.is_running = true;
    ESP_LOGI(TAG, "All URI handlers registered, web server ready");
    return ESP_OK;
}

void web_server_stop(void) {
    if (server_state.is_running) {
        httpd_stop(server_state.server);
        server_state.is_running = false;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

bool web_server_is_running(void) {
    return server_state.is_running;
}
