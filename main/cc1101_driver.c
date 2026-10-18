#include "cc1101_driver.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

static const char *TAG = "CC1101";
static spi_device_handle_t spi_handle = NULL;
static TaskHandle_t acquisition_task = NULL;
static SemaphoreHandle_t spi_mutex = NULL;
static bool acquisition_active = false;

esp_err_t cc1101_spi_transaction(uint8_t cmd, uint8_t* tx_data, uint32_t tx_len, 
                                   uint8_t* rx_data, uint32_t rx_len, uint32_t timeout_ms) {
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(spi_transaction_t));
    
    t.flags = 0;
    
    if (tx_data && tx_len > 0) {
        if (tx_len <= 4) {
            memcpy(t.tx_data, tx_data, tx_len);
            t.flags |= SPI_TRANS_USE_TXDATA;
            t.length = tx_len * 8;
        } else {
            t.tx_buffer = tx_data;
            t.length = tx_len * 8;
        }
    }
    
    if (rx_data && rx_len > 0) {
        if (rx_len <= 4) {
            t.rxlength = rx_len * 8;
            t.flags |= SPI_TRANS_USE_RXDATA;
        } else {
            t.rx_buffer = rx_data;
            t.rxlength = rx_len * 8;
        }
    }
    
    t.user = (void*)timeout_ms;
    
    ret = spi_device_polling_transmit(spi_handle, &t);
    return ret;
}

esp_err_t cc1101_gpio_init(void) {
    esp_err_t ret;
    
    // Initialize CS pin
    gpio_config_t cs_config;
    cs_config.pin_bit_mask = (1ULL << CC1101_CS_GPIO);
    cs_config.mode = GPIO_MODE_OUTPUT;
    cs_config.pull_up_en = GPIO_PULLUP_ENABLE;
    cs_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cs_config.intr_type = GPIO_INTR_DISABLE;
    ret = gpio_config(&cs_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CS GPIO: %d", ret);
        return ret;
    }
    
    gpio_set_level(CC1101_CS_GPIO, 1); // CS high (deselect)
    
    // Initialize IRQ pin (if available)
    if (CC1101_IRQ_GPIO >= 0) {
        gpio_config_t irq_config;
        irq_config.pin_bit_mask = (1ULL << CC1101_IRQ_GPIO);
        irq_config.mode = GPIO_MODE_INPUT;
        irq_config.pull_up_en = GPIO_PULLUP_ENABLE;
        irq_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        irq_config.intr_type = GPIO_INTR_DISABLE;
        ret = gpio_config(&irq_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure IRQ GPIO: %d", ret);
        }
    }
    
    return ESP_OK;
}

esp_err_t cc1101_spi_init(void) {
    esp_err_t ret;
    spi_bus_config_t bus_config;
    spi_device_interface_config_t device_config;
    
    // Initialize SPI bus
    memset(&bus_config, 0, sizeof(bus_config));
    bus_config.mosi_io_num = 11;  // ESP32-S3 default
    bus_config.miso_io_num = 12;  // ESP32-S3 default
    bus_config.sclk_io_num = 10;  // ESP32-S3 default
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = 4096;
    
    ret = spi_bus_initialize(CC1101_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %d", ret);
        return ret;
    }
    
    // Initialize SPI device
    memset(&device_config, 0, sizeof(device_config));
    device_config.cs_ena_pretrans = 5;
    device_config.clock_speed_hz = 10000000;  // 10 MHz
    device_config.mode = 0;
    device_config.spics_io_num = CC1101_CS_GPIO;
    device_config.queue_size = 1;
    device_config.flags = 0;
    
    ret = spi_bus_add_device(CC1101_SPI_HOST, &device_config, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %d", ret);
        spi_bus_free(CC1101_SPI_HOST);
        return ret;
    }
    
    // Create mutex for SPI access
    spi_mutex = xSemaphoreCreateMutex();
    if (spi_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create SPI mutex");
        spi_bus_remove_device(spi_handle);
        spi_bus_free(CC1101_SPI_HOST);
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t cc1101_init(void) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing CC1101 driver");
    
    // Initialize GPIOs
    ret = cc1101_gpio_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIOs: %d", ret);
        return ret;
    }
    
    // Initialize SPI
    ret = cc1101_spi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI: %d", ret);
        return ret;
    }
    
    // Reset module
    cc1101_reset();
    
    // Wait a bit for reset to complete
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "CC1101 driver initialized successfully");
    return ESP_OK;
}

void cc1101_reset(void) {
    ESP_LOGI(TAG, "Resetting CC1101");
    
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    
    uint8_t cmd = CC1101_SPI_FACTORY_RESET;
    esp_err_t spi_ret = cc1101_spi_transaction(
        CC1101_SPI_WRITE_SINGLE, &cmd, 1, NULL, 0, 100
    );
    
    xSemaphoreGive(spi_mutex);
    
    if (spi_ret == ESP_OK) {
        ESP_LOGI(TAG, "CC1101 reset completed");
    } else {
        ESP_LOGE(TAG, "CC1101 reset failed: %d", spi_ret);
    }
}

esp_err_t cc1101_write_register(uint8_t reg, uint8_t value) {
    uint8_t cmd[2];
    
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    
    cmd[0] = CC1101_SPI_WRITE_REGISTER | (reg & 0x3F);
    cmd[1] = value;
    
    esp_err_t spi_ret = cc1101_spi_transaction(
        CC1101_SPI_WRITE_SINGLE, cmd, 2, NULL, 0, 100
    );
    
    xSemaphoreGive(spi_mutex);
    
    return spi_ret;
}

esp_err_t cc1101_read_register(uint8_t reg, uint8_t* value) {
    uint8_t cmd[2];
    
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    
    cmd[0] = CC1101_SPI_READ_REGISTER | (reg & 0x3F);
    
    esp_err_t spi_ret = cc1101_spi_transaction(
        CC1101_SPI_READ_SINGLE, cmd, 1, value, 1, 100
    );
    xSemaphoreGive(spi_mutex);
    
    return spi_ret;
}

esp_err_t cc1101_write_config(const cc1101_config_t* config) {
    esp_err_t ret;
    
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Writing CC1101 configuration for %d Hz channel %d", 
             config->frequency, config->channel);
    
    // Set basic configuration
    ret = cc1101_write_register(CC1101_REG_IOCFG2, 0x0D);  // GDO2 output for FIFO thresholds
    if (ret != ESP_OK) return ret;
    
    ret = cc1101_write_register(CC1101_REG_IOCFG0, 0x2E);  // GDO0 output for data_ready
    if (ret != ESP_OK) return ret;
    
    ret = cc1101_write_register(CC1101_REG_FIFOTHR, 0x4F);  // 64 bytes in RX and 64 bytes in TX
    if (ret != ESP_OK) return ret;
    
    ret = cc1101_write_register(CC1101_REG_PKTCTRL0, 0x05);  // Fixed length, enable CRC
    if (ret != ESP_OK) return ret;
    
    ret = cc1101_write_register(CC1101_REG_PATABLE, 0x60);  // +10 dBm output power
    if (ret != ESP_OK) return ret;
    
    // Calculate channel settings
    uint32_t channel_space = (uint32_t)(CC1101_CHANNEL_SPACE * 1000000.0);
    uint32_t channel_offset = (uint32_t)(config->frequency - CC1101_FREQUENCY_BASE);
    uint8_t channel = (uint8_t)(channel_offset / channel_space);
    
    if (channel > 255) {
        ESP_LOGE(TAG, "Channel %d out of range (0-255)", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Set channel
    ret = cc1101_write_register(CC1101_REG_CHANNEL, channel);
    if (ret != ESP_OK) return ret;
    
    // Set frontend configuration
    ret = cc1101_write_register(CC1101_REG_FREND0, CC1101_REG_FREND0_SETTING);
    if (ret != ESP_OK) return ret;
    
    // Set AGC configuration
    ret = cc1101_write_register(CC1101_REG_AGCCTRL1, CC1101_REG_AGCCTRL1_SETTING);
    if (ret != ESP_OK) return ret;
    
    ret = cc1101_write_register(CC1101_REG_AGCCTRL0, CC1101_REG_AGCCTRL0_SETTING);
    if (ret != ESP_OK) return ret;
    
    // Set modem configuration
    ret = cc1101_write_register(CC1101_REG_MCSM0, CC1101_REG_MCSM0_SETTING);
    if (ret != ESP_OK) return ret;
    
    ret = cc1101_write_register(CC1101_REG_FSMCTRL, CC1101_REG_FSMCTRL_SETTING);
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "CC1101 configuration written successfully");
    return ESP_OK;
}

esp_err_t cc1101_read_fifo(uint8_t* buffer, uint32_t* size) {
    if (buffer == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t bytes_to_read = *size;
    if (bytes_to_read > CC1101_SIGNAL_BUF_SIZE) {
        bytes_to_read = CC1101_SIGNAL_BUF_SIZE;
    }
    
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    
    uint8_t cmd = CC1101_SPI_READ_BURST;
    esp_err_t spi_ret = cc1101_spi_transaction(
        CC1101_SPI_READ_BURST, &cmd, 1, buffer, bytes_to_read, 100
    );
    
    xSemaphoreGive(spi_mutex);
    
    if (spi_ret == ESP_OK) {
        *size = bytes_to_read;
    }
    
    return spi_ret;
}

void cc1101_acquisition_task(void* arg) {
    cc1101_signal_t* signal = (cc1101_signal_t*)arg;
    
    if (signal == NULL) {
        ESP_LOGE(TAG, "Signal structure is NULL");
        vTaskDelete(NULL);
    }
    
    uint32_t start_time = esp_timer_get_time();
    uint32_t samples_collected = 0;
    uint32_t fifo_count = 0;
    uint8_t read_cmd = CC1101_SPI_READ_SINGLE;
    
    ESP_LOGI(TAG, "Starting signal acquisition, buffer size: %d bytes", signal->size);
    
    while (acquisition_active && samples_collected < signal->size) {
        // Check FIFO count
        uint8_t fifo_count_reg;
        esp_err_t ret = cc1101_read_register(CC1101_REG_FIFOCNT, &fifo_count_reg);
        if (ret == ESP_OK) {
            fifo_count = (uint32_t)fifo_count_reg;
        }
        
        if (fifo_count >= 64) {  // FIFO has enough data
            ret = cc1101_read_fifo(signal->buffer + samples_collected, &fifo_count);
            if (ret == ESP_OK) {
                samples_collected += fifo_count;
                ESP_LOGI(TAG, "Collected %d samples, total: %d", fifo_count, samples_collected);
            } else {
                ESP_LOGE(TAG, "Failed to read FIFO: %d", ret);
            }
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);  // Small delay to prevent CPU hogging
    }
    
    // Mark acquisition as complete
    signal->samples_count = samples_collected;
    signal->active = false;
    acquisition_active = false;
    
    uint32_t end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Signal acquisition completed: %d samples in %lu us (%.2f samples/sec)", 
             samples_collected, end_time - start_time, 
             (float)samples_collected * 1000000.0 / (end_time - start_time));
    
    vTaskDelete(NULL);
}

esp_err_t cc1101_start_acquisition(cc1101_signal_t* signal) {
    if (signal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (signal->buffer == NULL) {
        ESP_LOGE(TAG, "Signal buffer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (acquisition_active) {
        ESP_LOGE(TAG, "Acquisition is already active");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Initialize signal structure
    signal->samples_count = 0;
    signal->active = true;
    
    // Start acquisition task
    BaseType_t ret = xTaskCreate(
        cc1101_acquisition_task,
        "cc1101_acquisition",
        4096,
        signal,
        tskIDLE_PRIORITY + 1,
        &acquisition_task
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create acquisition task");
        signal->active = false;
        return ESP_ERR_NO_MEM;
    }
    
    acquisition_active = true;
    
    // Reset FIFO counter
    cc1101_write_register(CC1101_REG_FIFOCNT, 0);
    
    ESP_LOGI(TAG, "Signal acquisition started");
    return ESP_OK;
}

void cc1101_stop_acquisition(void) {
    if (!acquisition_active) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping signal acquisition");
    
    acquisition_active = false;
    
    if (acquisition_task != NULL) {
        vTaskDelete(acquisition_task);
        acquisition_task = NULL;
    }
    
    ESP_LOGI(TAG, "Signal acquisition stopped");
}

esp_err_t cc1101_get_signal_status(cc1101_signal_t* signal) {
    if (signal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t fifo_count;
    esp_err_t ret = cc1101_read_register(CC1101_REG_FIFOCNT, &fifo_count);
    
    if (ret == ESP_OK) {
        signal->samples_count = (uint32_t)fifo_count;
    }
    
    signal->active = acquisition_active;
    
    return ret;
}

esp_err_t cc1101_send_signal(const uint8_t* data, uint32_t size) {
    if (data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Sending signal data: %d bytes", size);
    
    // TODO: Implement actual CC1101 transmission
    // For now, just log the data
    for (uint32_t i = 0; i < size && i < 32; i++) {
        ESP_LOGI(TAG, "Data[%d]: 0x%02X", i, data[i]);
    }
    
    return ESP_OK;
}

esp_err_t cc1101_get_version(char* version_str, uint32_t max_len) {
    if (version_str == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Read version from CC1101
    uint8_t version;
    esp_err_t ret = cc1101_read_register(0x2F, &version);  // PARTNUM register
    
    snprintf(version_str, max_len, "CC1101 v%d.%d.%d", 
             (version >> 4) & 0x0F, version & 0x0F, 0);
    
    return ret;
}

esp_err_t cc1101_check_connection(void) {
    // Read PARTNUM register (0x2F) - should be 0x00 for CC1101
    uint8_t partnum;
    esp_err_t ret = cc1101_read_register(0x2F, &partnum);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Read VERSION register (0x2E) - should be 0x04 for CC1101
    uint8_t version;
    ret = cc1101_read_register(0x2E, &version);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Verify CC1101 identity: PARTNUM=0x00, VERSION=0x04 (or 0x14 for some variants)
    if (partnum == 0x00 && (version == 0x04 || version == 0x14)) {
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "CC1101 not detected: PARTNUM=0x%02X, VERSION=0x%02X", partnum, version);
    return ESP_ERR_NOT_FOUND;
}