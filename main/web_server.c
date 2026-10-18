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
static const char* MIME_TYPE_TEXT_PLAIN = "text/plain"; /* unused */
static const char* MIME_TYPE_APPLICATION_OCTET_STREAM = "application/octet-stream";

/* Base64 encoding table */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

esp_err_t base64_encode(const uint8_t* data, uint32_t size, char* output, uint32_t* output_size) {
    if (!data || !output || !output_size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t i = 0, j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    
    *output_size = 0;
    
    while (i < size) {
        memset(char_array_3, 0xFF, sizeof(char_array_3));
        
        // Copy data to char_array_3
        for (int k = 0; k < 3; k++) {
            if (i < size) {
                char_array_3[k] = data[i++];
            }
        }
        
        // Encode char_array_3 to char_array_4
        char_array_4[0] = (char_array_3[0] & 0xFC) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) | ((char_array_3[1] & 0xF0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0F) << 2) | ((char_array_3[2] & 0xC0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3F;
        
        // Convert to ASCII
        for (int k = 0; k < 4; k++) {
            if (i - 3 + k - 1 < size) {
                output[j++] = base64_table[char_array_4[k]];
            } else {
                output[j++] = '=';
            }
        }
    }
    
    output[j] = '\0';
*output_size = j;

    return ESP_OK;
}

esp_err_t base64_decode(const char* input, uint8_t* output, uint32_t* output_size) {
    if (!input || !output || !output_size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t i = 0, j = 0;
    uint32_t input_len = strlen(input);
    uint8_t char_array_4[4];
    uint8_t char_array_3[3];
    uint32_t out_idx = 0;
    
    *output_size = 0;
    
    while (input[i] != '\0' && i < input_len) {
        // Find first valid index
        int map_index = -1;
        for (int k = 0; k < 64; k++) {
            if (input[i] == base64_table[k]) {
                map_index = k;
                break;
            }
        }
        
        if (map_index == -1) {
            if (input[i] == '=') {
                // Padding
            }
            i++;
            continue;
        }
        
        char_array_4[j++] = (uint8_t)map_index;
        
        if (j == 4 || char_array_4[0] == '=') {
            // Decode to char_array_3
            if (char_array_4[0] != '=') {
                char_array_3[0] = (char_array_4[0] << 2) | (char_array_4[1] >> 4);
                output[out_idx++] = char_array_3[0];
            }
            if (char_array_4[1] != '=' && char_array_4[1] != '=') {
                char_array_3[1] = (char_array_4[1] << 4) | (char_array_4[2] >> 2);
                output[out_idx++] = char_array_3[1];
            }
            if (char_array_4[2] != '=' && char_array_4[2] != '=') {
                char_array_3[2] = (char_array_4[2] << 6) | char_array_4[3];
                output[out_idx++] = char_array_3[2];
            }
            j = 0;
        }
        
        i++;
    }
    
    *output_size = out_idx;
    return ESP_OK;
}

static esp_err_t load_signal_from_file(uint32_t signal_id, signal_data_t* signal_data) {
    if (!signal_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Create filename
    char filename[64];
snprintf(filename, sizeof(filename), "%s/signal_%06lu.dat", SIGNAL_STORAGE_PATH, (unsigned long)signal_id);

    // Check if file exists
    FILE* f = fopen(filename, "rb");
    if (!f) {
        ESP_LOGD(TAG, "Signal file not found: %s", filename);
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
    signal_data->frequency = 443000000;  // 443MHz
    signal_data->samples_count = file_size / 4;  // Assuming 4 bytes per sample
    signal_data->data_size = base64_size;
    signal_data->data = base64_data;
    
    // Create filename string
    signal_data->filename = (char*)malloc(64);
    if (!signal_data->filename) {
        free(base64_data);
        ESP_LOGE(TAG, "Failed to allocate memory for filename");
        return ESP_ERR_NO_MEM;
    }
snprintf(signal_data->filename, 64, "signal_%06lu.dat", (unsigned long)signal_id);

    return ESP_OK;
}

static esp_err_t save_signal_to_file(const uint8_t* data, uint32_t size, const char* filename) {
    if (!data || size == 0 || !filename) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Ensure storage directory exists
    mkdir(SIGNAL_STORAGE_PATH, 0777);
    
    // Create full path
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
        ESP_LOGE(TAG, "Failed to write entire file: %zu != %zu", bytes_written, size);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Signal saved to: %s (%d bytes)", full_path, size);
    return ESP_OK;
}

static esp_err_t list_signals_json(char* output, uint32_t output_size) {
    if (!output) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Open directory
    DIR* dir = opendir(SIGNAL_STORAGE_PATH);
    if (!dir) {
        // Create directory if it doesn't exist
        mkdir(SIGNAL_STORAGE_PATH, 0777);
        dir = opendir(SIGNAL_STORAGE_PATH);
        if (!dir) {
            ESP_LOGD(TAG, "Failed to open signal directory: %s", SIGNAL_STORAGE_PATH);
            return ESP_ERR_NOT_FOUND;
        }
    }
    
    // Create JSON array
    cJSON* signals_array = cJSON_CreateArray();
    if (!signals_array) {
        ESP_LOGE(TAG, "Failed to create signals array");
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }
    
    struct dirent* entry;
    uint32_t signal_id = 1;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".dat") != NULL) {
            // Parse signal ID from filename
            char* endptr;
            uint32_t id = (uint32_t)strtoul(entry->d_name + 7, &endptr, 10);  // Skip "signal_"
            
            if (endptr && *endptr == '\0') {
                cJSON* signal_obj = cJSON_CreateObject();
                if (!signal_obj) {
                    ESP_LOGE(TAG, "Failed to create signal object");
                    continue;
                }
                
                cJSON_AddNumberToObject(signal_obj, "id", id);
                cJSON_AddStringToObject(signal_obj, "filename", entry->d_name);
                
                // Get file size
                char full_path[512];
                snprintf(full_path, sizeof(full_path), "%s/%s", SIGNAL_STORAGE_PATH, entry->d_name);
                struct stat st;
                if (stat(full_path, &st) == 0) {
                    cJSON_AddNumberToObject(signal_obj, "size", st.st_size);
                    cJSON_AddNumberToObject(signal_obj, "modified_time", st.st_mtime);
                }
                
                cJSON_AddItemToArray(signals_array, signal_obj);
                
                if (id > signal_id) {
                    signal_id = id;
                }
            }
        }
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

esp_err_t web_server_load_signal(uint32_t signal_id, signal_data_t* signal_data) {
    if (!signal_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize signal data structure
    memset(signal_data, 0, sizeof(signal_data_t));
    
    return load_signal_from_file(signal_id, signal_data);
}

esp_err_t web_server_delete_signal(uint32_t signal_id) {
    char filename[64];
    snprintf(filename, sizeof(filename), "%s/signal_%06lu.dat", SIGNAL_STORAGE_PATH, (unsigned long)signal_id);
    
    if (remove(filename) != 0) {
        ESP_LOGE(TAG, "Failed to delete signal file: %s", filename);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Deleted signal file: %s", filename);
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
    
    // Get signal type/name
    cJSON* type_json = cJSON_GetObjectItem(root, "type");
    const char* signal_type = "default";
    if (type_json && cJSON_IsString(type_json)) {
        signal_type = type_json->valuestring;
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
    snprintf(filename, sizeof(filename), "signal_%ld_%s.dat", (long)esp_timer_get_time(), signal_type);
    
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
    
    // Parse signal ID from query parameter
    int signal_id = -1;
    char query_str[256];
    if (httpd_req_get_url_query_str(req, query_str, sizeof(query_str)) == ESP_OK) {
        char* token = strtok(query_str, "&");
        while (token != NULL) {
            if (strncmp(token, "id=", 3) == 0) {
                signal_id = atoi(token + 3);
                break;
            }
            token = strtok(NULL, "&");
        }
    }
    
    if (signal_id < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid signal ID");
        return ESP_FAIL;
    }
    
    esp_err_t ret = web_server_delete_signal(signal_id);
    
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
    cJSON_AddNumberToObject(response, "signal_id", signal_id);
    
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
    
    // Parse signal ID from URI
    uint32_t signal_id = 0;
    
    // Extract signal ID from URI (simplified parsing)
    const char* uri = req->uri;
    if (sscanf(uri, "/api/signal/download/%lu", &signal_id) != 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid signal ID in URI");
        return ESP_FAIL;
    }
    
    // Load signal data
    signal_data_t signal_data;
    memset(&signal_data, 0, sizeof(signal_data_t));
    
    esp_err_t ret = web_server_load_signal(signal_id, &signal_data);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Signal not found");
        return ESP_FAIL;
    }
    
    // Create filename for download
    char filename[64];
    snprintf(filename, sizeof(filename), "signal_%06lu.dat", (unsigned long)signal_id);
    
    // Set response headers
    httpd_resp_set_type(req, MIME_TYPE_APPLICATION_OCTET_STREAM);
    httpd_resp_set_hdr(req, "Content-Disposition", filename);
    
    // Decode base64 data and send
    uint8_t* decoded_data = (uint8_t*)malloc(signal_data.data_size);
    if (!decoded_data) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate decode buffer");
        free(signal_data.data);
        return ESP_FAIL;
    }
    
    ret = base64_decode(signal_data.data, decoded_data, &signal_data.data_size);
    free(signal_data.data);
    
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to decode signal data");
        free(decoded_data);
        return ESP_FAIL;
    }
    
    httpd_resp_send(req, (const char*)decoded_data, signal_data.data_size);
    free(decoded_data);
    
    return ESP_OK;
}

esp_err_t web_ui_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Web UI GET request received");
    
    const char* html_content = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "    <title>CC1101 443MHz Remote Control Signal Manager</title>"
        "    <meta charset=\"utf-8\">"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "    <style>"
        "        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }"
        "        .container { max-width: 1200px; margin: 0 auto; background-color: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
        "        h1 { color: #333; text-align: center; }"
        "        .section { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }"
        "        .status { background-color: #e8f5e8; }"
        "        .controls { background-color: #f0f8ff; }"
        "        button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; margin: 5px; }"
        "        button:hover { background-color: #45a049; }"
        "        button.stop { background-color: #f44336; }"
        "        button.stop:hover { background-color: #da190b; }"
        "        input[type=\"file\"] { display: none; }"
        "        .file-label { background-color: #2196F3; color: white; padding: 10px 15px; border-radius: 4px; cursor: pointer; display: inline-block; }"
        "        .file-label:hover { background-color: #0b7dda; }"
        "        table { width: 100%; border-collapse: collapse; margin-top: 20px; }"
        "        th, td { border: 1px solid #ddd; padding: 12px; text-align: left; }"
        "        th { background-color: #f2f2f2; }"
        "        tr:nth-child(even) { background-color: #f9f9f9; }"
        "        .signal-data { font-family: monospace; white-space: pre; background-color: #f5f5f5; padding: 10px; border-radius: 4px; overflow-x: auto; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <div class=\"container\">"
        "        <h1>CC1101 443MHz Remote Control Signal Manager</h1>"
        "        <div class=\"section status\">"
        "            <h2>System Status</h2>"
        "            <p><strong>Status:</strong> <span id=\"status\">Loading...</span></p>"
        "            <p><strong>Available Interfaces:</strong> CC1101 443MHz RF Module</p>"
        "            <p><strong>Firmware Version:</strong> 1.0.0</p>"
        "        </div>"
        "        <div class=\"section controls\">"
        "            <h2>Signal Capture/Control</h2>"
        "            <div>"
        "                <button onclick=\"startCapture()\">Start Signal Capture</button>"
        "                <button class=\"stop\" onclick=\"stopCapture()\">Stop Signal Capture</button>"
        "                <label class=\"file-label\">Upload Signal File"
        "                    <input type=\"file\" id=\"signalFileInput\" onchange=\"uploadSignalFile()\">"
        "                </label>"
        "                <button onclick=\"transmitSignal()\">Transmit Stored Signal</button>"
        "            </div>"
        "            <p>Captured signals will be stored and can be retrieved for transmission.</p>"
        "        </div>"
        "        <div class=\"section\">"
        "            <h2>Stored Signals</h2>"
        "            <div id=\"signalList\">Loading signals...</div>"
        "        </div>"
        "        <div class=\"section\">"
        "            <h2>Captured Signal Data</h2>"
        "            <div id=\"signalData\">No signal data available</div>"
        "        </div>"
        "        <div class=\"section\">"
        "            <h2>API Status</h2>"
        "            <div id=\"apiStatus\">Loading API status...</div>"
        "        </div>"
        "    </div>"
        "    <script>"
        "        setInterval(function() {"
        "            refreshStatus();"
        "            refreshSignalList();"
        "            refreshAPIStatus();"
        "        }, 5000);"
        "        function refreshStatus() {"
        "            fetch('/api/status')"
        "                .then(response => response.json())"
        "                .then(data => {"
        "                    document.getElementById('status').textContent = data.status || 'Unknown';"
        "                })"
        "                .catch(error => {"
        "                    console.error('Error fetching status:', error);"
        "                    document.getElementById('status').textContent = 'Error';"
        "                });"
        "        }"
        "        function refreshSignalList() {"
        "            fetch('/api/signal/list')"
        "                .then(response => response.json())"
        "                .then(data => {"
        "                    let html = '<table><tr><th>ID</th><th>Filename</th><th>Size</th><th>Actions</th></tr>';"
        "                    data.forEach(signal => {"
        "                        html += `<tr><td>${signal.id}</td><td>${signal.filename}</td><td>${signal.size} bytes</td><td><button onclick=\"loadSignalData(${signal.id})\">Load</button> <button onclick=\"deleteSignal(${signal.id})\">Delete</button></td></tr>`;"
        "                    });"
        "                    html += '</table>';"
        "                    document.getElementById('signalList').innerHTML = html;"
        "                })"
        "                .catch(error => {"
        "                    console.error('Error fetching signal list:', error);"
        "                    document.getElementById('signalList').innerHTML = 'Error loading signals';"
        "                });"
        "        }"
        "        function refreshAPIStatus() {"
        "            fetch('/api/status')"
        "                .then(response => response.json())"
        "                .then(data => {"
        "                    let html = '<ul>';"
        "                    Object.keys(data).forEach(key => {"
        "                        html += `<li><strong>${key}:</strong> ${data[key]}</li>`;"
        "                    });"
        "                    html += '</ul>';"
        "                    document.getElementById('apiStatus').innerHTML = html;"
        "                })"
        "                .catch(error => {"
        "                    console.error('Error fetching API status:', error);"
        "                    document.getElementById('apiStatus').innerHTML = 'Error loading API status';"
        "                });"
        "        }"
        "        function startCapture() {"
        "            fetch('/api/signal/capture', { method: 'POST', body: JSON.stringify({ type: 'auto' }) })"
        "                .then(response => response.json())"
        "                .then(data => {"
        "                    alert('Signal capture started: ' + data.message);"
        "                    refreshSignalList();"
        "                })"
        "                .catch(error => {"
        "                    console.error('Error starting capture:', error);"
        "                    alert('Failed to start signal capture');"
        "                });"
        "        }"
        "        function stopCapture() {"
        "            fetch('/api/signal/capture?id=-1', { method: 'DELETE' })"
        "                .then(response => response.json())"
        "                .then(data => {"
        "                    alert('Signal capture stopped');"
        "                })"
        "                .catch(error => {"
        "                    console.error('Error stopping capture:', error);"
        "                    alert('Failed to stop signal capture');"
        "                });"
        "        }"
        "        function uploadSignalFile() {"
        "            const fileInput = document.getElementById('signalFileInput');"
        "            if (!fileInput.files || !fileInput.files[0]) {"
        "                return;"
        "            }"
        "            const file = fileInput.files[0];"
        "            const reader = new FileReader();"
        "            reader.onload = function(e) {"
        "                const arrayBuffer = e.target.result;"
        "                const uint8Array = new Uint8Array(arrayBuffer);"
        "                const base64Data = btoa(String.fromCharCode.apply(null, uint8Array));"
        "                fetch('/api/signal/capture', {"
        "                    method: 'POST',"
        "                    headers: { 'Content-Type': 'application/json' },"
        "                    body: JSON.stringify({"
        "                        type: 'upload',"
        "                        data: base64Data"
        "                    })"
        "                })"
        "                .then(response => response.json())"
        "                .then(data => {"
        "                    alert('Signal file uploaded: ' + data.message);"
        "                    refreshSignalList();"
        "                })"
        "                .catch(error => {"
        "                    console.error('Error uploading file:', error);"
        "                    alert('Failed to upload signal file');"
        "                });"
        "            };"
        "            reader.readAsArrayBuffer(file);"
        "        }"
        "        function loadSignalData(signalId) {"
        "            fetch(`/api/signal/download/${signalId}`)"
        "                .then(response => response.arrayBuffer())"
        "                .then(arrayBuffer => {"
        "                    const uint8Array = new Uint8Array(arrayBuffer);"
        "                    const dataStr = Array.from(uint8Array).map(byte => byte.toString(16).padStart(2, '0')).join(' ');"
        "                    document.getElementById('signalData').textContent = dataStr;"
        "                })"
        "                .catch(error => {"
        "                    console.error('Error loading signal data:', error);"
        "                    document.getElementById('signalData').textContent = 'Error loading signal data';"
        "                });"
        "        }"
        "        function deleteSignal(signalId) {"
        "            if (confirm(`Are you sure you want to delete signal ${signalId}?`)) {"
        "                fetch(`/api/signal/capture?id=${signalId}`, { method: 'DELETE' })"
        "                    .then(response => response.json())"
        "                    .then(data => {"
        "                        alert(data.message);"
        "                        refreshSignalList();"
        "                    })"
        "                    .catch(error => {"
        "                        console.error('Error deleting signal:', error);"
        "                        alert('Failed to delete signal');"
        "                    });"
        "            }"
        "        }"
        "        function transmitSignal() {"
        "            alert('Transmit functionality would be implemented here');"
        "        }"
        "        document.addEventListener('DOMContentLoaded', function() {"
        "            refreshStatus();"
        "            refreshSignalList();"
        "            refreshAPIStatus();"
        "        });"
        "    </script>"
        "</body>"
        "</html>";
    
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
    config.max_uri_handlers = 8;
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
    httpd_register_uri_handler(server_state.server, &uri);
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/capture";
    uri.method = HTTP_POST;
    uri.handler = signal_capture_post_handler;
    httpd_register_uri_handler(server_state.server, &uri);
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/capture";
    uri.method = HTTP_DELETE;
    uri.handler = signal_capture_delete_handler;
    httpd_register_uri_handler(server_state.server, &uri);
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/list";
    uri.method = HTTP_GET;
    uri.handler = signal_capture_list_get_handler;
    httpd_register_uri_handler(server_state.server, &uri);
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/signal/download/*";
    uri.method = HTTP_GET;
    uri.handler = signal_file_get_handler;
    httpd_register_uri_handler(server_state.server, &uri);
    
    memset(&uri, 0, sizeof(uri));
    uri.uri = "/api/status";
    uri.method = HTTP_GET;
    uri.handler = api_status_get_handler;
    httpd_register_uri_handler(server_state.server, &uri);
    
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
