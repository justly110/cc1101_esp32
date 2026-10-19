#include "cc1101_driver.h"
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>

static const char *TAG = "CC1101";
static spi_device_handle_t spi_handle = NULL;
static TaskHandle_t acquisition_task = NULL;
static SemaphoreHandle_t spi_mutex = NULL;
static bool acquisition_active = false;

/* Internal one-shot capture state */
static cc1101_signal_t capture_signal;
static uint8_t *capture_buffer = NULL;
static volatile bool capture_done = true;

/* ---- OOK pulse scan state ---- */
static volatile uint32_t s_edge_times[CC1101_MAX_PULSES + 1];
static volatile uint32_t s_edge_count = 0;
static volatile bool s_recording = false;
static volatile bool s_scan_running = false;
static volatile int s_peak_rssi = -128;
static portMUX_TYPE s_isr_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_scan_task = NULL;
static cc1101_capture_result_t s_result;
static volatile bool s_has_result = false;
static volatile cc1101_scan_state_t s_scan_state = CC1101_SCAN_IDLE;
static bool s_isr_service_installed = false;

/* Currently selected carrier frequency (Hz). Written only via cc1101_write_config()/cc1101_set_frequency(). */
static volatile uint32_t s_frequency_hz = CC1101_DEFAULT_FREQUENCY;

static void IRAM_ATTR cc1101_gdo0_isr(void* arg)
{
    uint32_t now = (uint32_t)esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_isr_mux);
    if (s_recording && s_edge_count <= CC1101_MAX_PULSES) {
        s_edge_times[s_edge_count++] = now;
    }
    portEXIT_CRITICAL_ISR(&s_isr_mux);
}

/* Forward declarations (defined later in this file) */
esp_err_t cc1101_read_register(uint8_t reg, uint8_t* value);
esp_err_t cc1101_write_register(uint8_t reg, uint8_t value);
void cc1101_reset(void);

esp_err_t cc1101_spi_transaction(uint8_t cmd, uint8_t* tx_data, uint32_t tx_len, 
                                   uint8_t* rx_data, uint32_t rx_len, uint32_t timeout_ms) {
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(spi_transaction_t));
    
    t.flags = 0;
    
    if (tx_data && tx_len > 0) {
        t.tx_buffer = tx_data;
        t.length = tx_len * 8;
    }
    
    if (rx_data && rx_len > 0) {
        t.rx_buffer = rx_data;
        t.rxlength = rx_len * 8;
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
    
    // Initialize GDO0 pin as edge-triggered input for OOK pulse capture
    if (CC1101_IRQ_GPIO >= 0) {
        gpio_config_t irq_config;
        irq_config.pin_bit_mask = (1ULL << CC1101_IRQ_GPIO);
        irq_config.mode = GPIO_MODE_INPUT;
        irq_config.pull_up_en = GPIO_PULLUP_ENABLE;
        irq_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        irq_config.intr_type = GPIO_INTR_DISABLE;  /* enabled only while a carrier is present */
        ret = gpio_config(&irq_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure GDO0 GPIO: %d", ret);
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
    bus_config.mosi_io_num = CC1101_MOSI_GPIO;
    bus_config.miso_io_num = CC1101_MISO_GPIO;
    bus_config.sclk_io_num = CC1101_SCLK_GPIO;
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
    device_config.cs_ena_pretrans = 10;
    device_config.cs_ena_posttrans = 10;
    device_config.clock_speed_hz = 4000000;   // 4 MHz (CC1101 max 6.5 MHz)
    device_config.mode = 0;                  // CPOL=0, CPHA=0
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
    
    // Install GPIO ISR service for GDO0 pulse capture
    if (!s_isr_service_installed) {
        esp_err_t isr_ret = gpio_install_isr_service(0);
        if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "gpio_install_isr_service: %s", esp_err_to_name(isr_ret));
        }
        gpio_isr_handler_add(CC1101_IRQ_GPIO, cc1101_gdo0_isr, NULL);
        s_isr_service_installed = true;
    }
    
    // Reset module
    cc1101_reset();
    
    // Wait a bit for reset to complete
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    // Startup self-check: verify the chip is really present on the SPI bus
    uint8_t partnum = 0, version = 0;
    esp_err_t chk = cc1101_read_register(CC1101_REG_PARTNUM, &partnum);
    if (chk == ESP_OK) {
        chk = cc1101_read_register(CC1101_REG_VERSION, &version);
    }
    if (chk == ESP_OK && partnum == 0x00 && (version == 0x04 || version == 0x14)) {
        ESP_LOGI(TAG, "CC1101 detected: PARTNUM=0x%02X VERSION=0x%02X", partnum, version);
    } else if (chk == ESP_OK) {
        ESP_LOGE(TAG, "CC1101 NOT detected: PARTNUM=0x%02X VERSION=0x%02X (check wiring)", partnum, version);
    } else {
        ESP_LOGE(TAG, "CC1101 NOT detected: SPI read failed (%s)", esp_err_to_name(chk));
    }
    
    // Diagnostic: read several registers to verify SPI integrity
    uint8_t r_iocfg2 = 0, r_freq2 = 0, r_mcsm0 = 0, r_pktctrl0 = 0;
    cc1101_read_register(0x00, &r_iocfg2);
    cc1101_read_register(0x0D, &r_freq2);
    cc1101_read_register(0x17, &r_mcsm0);
    cc1101_read_register(0x08, &r_pktctrl0);
    ESP_LOGD(TAG, "DIAG regs: IOCFG2=0x%02X FREQ2=0x%02X MCSM0=0x%02X PKTCTRL0=0x%02X",
             r_iocfg2, r_freq2, r_mcsm0, r_pktctrl0);
    
    ESP_LOGI(TAG, "CC1101 driver initialized successfully");
    return ESP_OK;
}

void cc1101_reset(void) {
    ESP_LOGI(TAG, "Resetting CC1101");
    
    if (spi_mutex == NULL || spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI not initialized, cannot reset CC1101");
        return;
    }
    
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    
    uint8_t cmd = CC1101_STROBE_SRES;
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
    
    if (spi_mutex == NULL || spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
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
    uint8_t tx[2];
    uint8_t rx[2];
    
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (spi_mutex == NULL || spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    
    /* 配置寄存器用 0x80|addr 读；状态寄存器(0x30-0x3F)需 burst 位 0xC0|addr */
    if (reg >= 0x30 && reg <= 0x3F) {
        tx[0] = CC1101_SPI_READ_BURST | (reg & 0x3F);
    } else {
        tx[0] = CC1101_SPI_READ_REGISTER | (reg & 0x3F);
    }
    tx[1] = 0x00;  /* dummy byte，寄存器值在第二个字节返回 */
    
    esp_err_t spi_ret = cc1101_spi_transaction(
        CC1101_SPI_READ_SINGLE, tx, 2, rx, 2, 100
    );
    xSemaphoreGive(spi_mutex);
    
    if (spi_ret == ESP_OK) {
        *value = rx[1];
    }
    
    return spi_ret;
}

/* Program FREQ2/FREQ1/FREQ0 for the currently selected frequency.
 * The chip must be in IDLE before these registers are written. */
static esp_err_t cc1101_program_frequency_registers(void)
{
    uint32_t f = s_frequency_hz;
    uint64_t freq_word = ((uint64_t)f << 16) / 26000000ULL;
    esp_err_t ret;

    ret = cc1101_write_register(CC1101_REG_FREQ2, (uint8_t)((freq_word >> 16) & 0xFF));
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FREQ1, (uint8_t)((freq_word >> 8) & 0xFF));
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FREQ0, (uint8_t)(freq_word & 0xFF));
    return ret;
}

esp_err_t cc1101_write_config(const cc1101_config_t* config) {
    esp_err_t ret;
    
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Frequency and most modem registers must be written in IDLE state. */
    uint8_t sidle_cmd = CC1101_STROBE_SIDLE;
    cc1101_spi_transaction(0, &sidle_cmd, 1, NULL, 0, 100);
    vTaskDelay(1 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Writing CC1101 configuration for %lu Hz", (unsigned long)config->frequency);
    
    /* --- Frequency: FREQ = f_carrier / f_xosc * 2^16, f_xosc = 26 MHz ---
     * 315.0 MHz -> 0x0C1D89, 433.92 MHz -> 0x10B071, 443.0 MHz -> 0x1109D8
     */
    s_frequency_hz = config->frequency;
    uint64_t freq_word = ((uint64_t)config->frequency << 16) / 26000000ULL;
    ret = cc1101_write_register(CC1101_REG_FREQ2, (uint8_t)((freq_word >> 16) & 0xFF));
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FREQ1, (uint8_t)((freq_word >> 8) & 0xFF));
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FREQ0, (uint8_t)(freq_word & 0xFF));
    if (ret != ESP_OK) return ret;
    
    /* --- Modem: OOK/ASK, no preamble/sync (raw capture), 4.8 kBaud ---
     * MDMCFG4: CHANBW_E=2, CHANBW_M=4 (BW ~101 kHz), DRATE_E=7
     * MDMCFG3: DRATE_M=0x83 -> 4.8 kBaud
     * MDMCFG2: MOD_FORMAT=ASK/OOK, SYNC_MODE=no preamble/sync
     */
    ret = cc1101_write_register(CC1101_REG_MDMCFG4, 0xA7);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_MDMCFG3, 0x83);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_MDMCFG2, 0x20);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_MDMCFG1, 0x22);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_MDMCFG0, 0xF8);
    if (ret != ESP_OK) return ret;
    
    /* --- Packet: asynchronous serial mode (raw demodulated data on GDO0) --- */
    ret = cc1101_write_register(CC1101_REG_PKTCTRL1, 0x04);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_PKTCTRL0, 0x0E);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FIFOTHR, 0x47);
    if (ret != ESP_OK) return ret;
    
    /* --- GDO --- */
    ret = cc1101_write_register(CC1101_REG_IOCFG2, 0x0D);  /* GDO2: FIFO threshold */
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_IOCFG0, 0x0D);  /* GDO0: serial data output */
    if (ret != ESP_OK) return ret;
    
    /* --- Frequency synthesizer / AGC / frontend --- */
    ret = cc1101_write_register(CC1101_REG_FSCTRL1, 0x06);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FSCTRL0, 0x00);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_AGCCTRL2, 0x07);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_AGCCTRL1, 0x40);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_AGCCTRL0, 0x91);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FREND1, 0xB6);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FREND0, 0x11);  /* OOK PA power index */
    if (ret != ESP_OK) return ret;
    
    /* --- State machine --- */
    ret = cc1101_write_register(CC1101_REG_MCSM1, 0x30);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_MCSM0, 0x18);
    if (ret != ESP_OK) return ret;
    
    /* --- Calibration registers (recommended defaults) --- */
    ret = cc1101_write_register(CC1101_REG_FSCAL3, 0xA9);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FSCAL2, 0x0A);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FSCAL1, 0x20);
    if (ret != ESP_OK) return ret;
    ret = cc1101_write_register(CC1101_REG_FSCAL0, 0x0D);
    if (ret != ESP_OK) return ret;
    
    /* --- PA table: index 0 ~+10 dBm (used for TX later) --- */
    ret = cc1101_write_register(CC1101_REG_PATABLE, 0x60);
    if (ret != ESP_OK) return ret;
    
    /* Flush FIFOs and calibrate */
    uint8_t sfrx = CC1101_STROBE_SFRX;
    cc1101_spi_transaction(0, &sfrx, 1, NULL, 0, 100);
    uint8_t sftx = CC1101_STROBE_SFTX;
    cc1101_spi_transaction(0, &sftx, 1, NULL, 0, 100);
    uint8_t scal = CC1101_STROBE_SCAL;
    cc1101_spi_transaction(0, &scal, 1, NULL, 0, 100);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "CC1101 configuration written successfully (FREQ=0x%06llX)",
             (unsigned long long)freq_word);
    return ESP_OK;
}

esp_err_t cc1101_set_frequency(uint32_t frequency_hz)
{
    if (!cc1101_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* CC1101 only supports these three continuous bands. */
    bool valid = (frequency_hz >= CC1101_BAND1_MIN && frequency_hz <= CC1101_BAND1_MAX) ||
                 (frequency_hz >= CC1101_BAND2_MIN && frequency_hz <= CC1101_BAND2_MAX) ||
                 (frequency_hz >= CC1101_BAND3_MIN && frequency_hz <= CC1101_BAND3_MAX);
    if (!valid) {
        ESP_LOGW(TAG, "Unsupported frequency: %lu Hz (valid: 300-348 / 387-464 / 779-928 MHz)",
                 (unsigned long)frequency_hz);
        return ESP_ERR_INVALID_ARG;
    }

    cc1101_config_t cfg = {
        .frequency = frequency_hz,
        .channel = CC1101_CHANNEL_1,
        .modulation = CC1101_MODULATION_GFSK,
        .sample_rate = CC1101_SIGNAL_SAMPLE_RATE,
        .continuous = false,
    };

    esp_err_t ret = cc1101_write_config(&cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Working frequency set to %lu Hz (%.3f MHz)",
                 (unsigned long)frequency_hz, frequency_hz / 1000000.0);
    }
    return ret;
}

uint32_t cc1101_get_frequency(void)
{
    return s_frequency_hz;
}

esp_err_t cc1101_read_fifo(uint8_t* buffer, uint32_t* size) {
    if (buffer == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (spi_mutex == NULL || spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t bytes_to_read = *size;
    if (bytes_to_read == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bytes_to_read > 64) {           /* CC1101 FIFO 深度为 64 字节 */
        bytes_to_read = 64;
    }
    
    /* 读 FIFO: 先发 0xFF 命令字节，随后 N 个 dummy 字节，寄存器数据在 rx[1..N] */
    uint8_t tx[65];
    uint8_t rx[65];
    tx[0] = CC1101_SPI_READ_BURST | 0x3F;   /* 0xFF: burst read FIFO */
    memset(&tx[1], 0, bytes_to_read);
    
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    
    esp_err_t spi_ret = cc1101_spi_transaction(
        0, tx, bytes_to_read + 1, rx, bytes_to_read + 1, 100
    );
    
    xSemaphoreGive(spi_mutex);
    
    if (spi_ret == ESP_OK) {
        memcpy(buffer, &rx[1], bytes_to_read);
        *size = bytes_to_read;
    }
    
    return spi_ret;
}

void cc1101_acquisition_task(void* arg) {
    cc1101_signal_t* signal = (cc1101_signal_t*)arg;
    
    if (signal == NULL) {
        ESP_LOGE(TAG, "Signal structure is NULL");
        capture_done = true;
        vTaskDelete(NULL);
    }
    
    int64_t start_time = esp_timer_get_time();
    uint32_t samples_collected = 0;
    uint32_t fifo_count = 0;
    int rssi_max = -128;
    int rssi_min = 127;
    
    ESP_LOGI(TAG, "Starting signal acquisition, buffer size: %lu bytes", (unsigned long)signal->size);
    
    while (acquisition_active && samples_collected < signal->size) {
        /* Timeout guard: stop after 3 seconds even if nothing was received */
        if ((esp_timer_get_time() - start_time) > 3000000LL) {
            ESP_LOGW(TAG, "Acquisition timeout reached");
            break;
        }
        
        /* Track RSSI to tell "no carrier" apart from "bad demodulation" */
        uint8_t rssi_raw = 0;
        if (cc1101_read_register(CC1101_REG_RSSI, &rssi_raw) == ESP_OK) {
            int rssi_dbm = (rssi_raw >= 128)
                         ? (((int)rssi_raw - 256) / 2 - 74)
                         : ((int)rssi_raw / 2 - 74);
            if (rssi_dbm > rssi_max) rssi_max = rssi_dbm;
            if (rssi_dbm < rssi_min) rssi_min = rssi_dbm;
        }
        
        uint8_t fifo_count_reg = 0;
        esp_err_t ret = cc1101_read_register(CC1101_REG_RXBYTES, &fifo_count_reg);
        if (ret == ESP_OK) {
            /* RXBYTES bit7 is overflow flag; mask it out */
            fifo_count = (uint32_t)(fifo_count_reg & 0x7F);
        }
        
        if (fifo_count > 0) {
            ret = cc1101_read_fifo(signal->buffer + samples_collected, &fifo_count);
            if (ret == ESP_OK) {
                samples_collected += fifo_count;
                ESP_LOGD(TAG, "Collected %lu samples, total: %lu",
                         (unsigned long)fifo_count, (unsigned long)samples_collected);
            } else {
                ESP_LOGE(TAG, "Failed to read FIFO: %d", ret);
            }
        }
        
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
    
    signal->samples_count = samples_collected;
    signal->active = false;
    acquisition_active = false;
    capture_done = true;
    acquisition_task = NULL;
    
    int64_t end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Signal acquisition completed: %lu samples in %lld us",
             (unsigned long)samples_collected, (long long)(end_time - start_time));
    ESP_LOGI(TAG, "RSSI during capture: min=%d dBm, max=%d dBm", rssi_min, rssi_max);
    if (rssi_max < -75) {
        ESP_LOGW(TAG, "No strong carrier detected - check selected frequency (%.3f MHz) and antenna",
                 s_frequency_hz / 1000000.0);
    } else if (rssi_max - rssi_min > 15) {
        ESP_LOGI(TAG, "Carrier activity detected - signal is present, but demodulation may need tuning");
    }
    
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
    
    if (spi_handle == NULL || spi_mutex == NULL) {
        ESP_LOGE(TAG, "SPI not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    signal->samples_count = 0;
    signal->active = true;
    
    /* Set the flag BEFORE creating the task to avoid the task seeing false and exiting */
    acquisition_active = true;
    
    BaseType_t ret = xTaskCreate(
        cc1101_acquisition_task,
        "cc1101_acquisition",
        4096,
        signal,
        tskIDLE_PRIORITY + 2,
        &acquisition_task
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create acquisition task");
        acquisition_active = false;
        signal->active = false;
        acquisition_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    /* Flush RX FIFO and enter RX mode */
    uint8_t sfrx = CC1101_STROBE_SFRX;
    cc1101_spi_transaction(0, &sfrx, 1, NULL, 0, 100);
    uint8_t srx = CC1101_STROBE_SRX;
    cc1101_spi_transaction(0, &srx, 1, NULL, 0, 100);
    
    ESP_LOGI(TAG, "Signal acquisition started");
    return ESP_OK;
}

void cc1101_stop_acquisition(void) {
    if (!acquisition_active) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping signal acquisition");
    acquisition_active = false;
    
    /* Let the task exit by itself; wait briefly */
    for (int i = 0; i < 20 && acquisition_task != NULL; i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    if (acquisition_task != NULL) {
        vTaskDelete(acquisition_task);
        acquisition_task = NULL;
    }
    
    capture_done = true;
    ESP_LOGI(TAG, "Signal acquisition stopped");
}

/* ---------------- One-shot capture API ---------------- */

esp_err_t cc1101_capture_start(void) {
    if (spi_handle == NULL || spi_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (acquisition_active) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (capture_buffer == NULL) {
        capture_buffer = (uint8_t*)malloc(CC1101_SIGNAL_BUF_SIZE);
        if (capture_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate capture buffer");
            return ESP_ERR_NO_MEM;
        }
    }
    
    memset(&capture_signal, 0, sizeof(capture_signal));
    capture_signal.buffer = capture_buffer;
    capture_signal.size = CC1101_SIGNAL_BUF_SIZE;
    capture_signal.frequency = s_frequency_hz;
    capture_signal.timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
    
    capture_done = false;
    
    esp_err_t ret = cc1101_start_acquisition(&capture_signal);
    if (ret != ESP_OK) {
        capture_done = true;
    }
    return ret;
}

void cc1101_capture_stop(void) {
    cc1101_stop_acquisition();
    capture_done = true;
}

bool cc1101_capture_is_done(void) {
    return capture_done && !acquisition_active;
}

uint8_t* cc1101_capture_get_data(uint32_t* len) {
    if (len) {
        *len = capture_signal.samples_count;
    }
    return capture_buffer;
}

bool cc1101_is_initialized(void) {
    return (spi_handle != NULL && spi_mutex != NULL);
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
    
    uint8_t partnum = 0;
    uint8_t version = 0;
    esp_err_t ret = cc1101_read_register(CC1101_REG_PARTNUM, &partnum);
    if (ret != ESP_OK) {
        snprintf(version_str, max_len, "read error");
        return ret;
    }
    ret = cc1101_read_register(CC1101_REG_VERSION, &version);
    if (ret != ESP_OK) {
        snprintf(version_str, max_len, "read error");
        return ret;
    }
    
    snprintf(version_str, max_len, "CC1101 PARTNUM=0x%02X VERSION=0x%02X",
             partnum, version);
    
    return ESP_OK;
}

esp_err_t cc1101_check_connection(void) {
    uint8_t partnum = 0;
    uint8_t version = 0;
    
    /* PARTNUM 固定为 0x00，VERSION 为 0x04 或 0x14 */
    esp_err_t ret = cc1101_read_register(CC1101_REG_PARTNUM, &partnum);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CC1101 check: read PARTNUM failed (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    ret = cc1101_read_register(CC1101_REG_VERSION, &version);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CC1101 check: read VERSION failed (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "CC1101 check: PARTNUM=0x%02X VERSION=0x%02X", partnum, version);
    
    if (partnum == 0x00 && (version == 0x04 || version == 0x14)) {
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "CC1101 not detected (PARTNUM=0x%02X, VERSION=0x%02X)", partnum, version);
    return ESP_ERR_NOT_FOUND;
}

/* ================= OOK pulse scanning ================= */

static int cc1101_read_rssi_dbm(void)
{
    uint8_t raw = 0;
    if (cc1101_read_register(CC1101_REG_RSSI, &raw) != ESP_OK) {
        return -128;
    }
    return (raw >= 128) ? (((int)raw - 256) / 2 - 74) : ((int)raw / 2 - 74);
}

static void cc1101_classify_pulses(cc1101_capture_result_t* r)
{
    if (r->count < 8) {
        r->format = CC1101_FMT_UNKNOWN;
        snprintf(r->format_name, sizeof(r->format_name), "Unknown");
        return;
    }

    uint32_t min_w = 0xFFFFFFFF;
    for (uint32_t i = 0; i < r->count; i++) {
        if (r->pulses[i] > 50 && r->pulses[i] < min_w) {
            min_w = r->pulses[i];
        }
    }
    if (min_w == 0xFFFFFFFF) {
        r->format = CC1101_FMT_UNKNOWN;
        snprintf(r->format_name, sizeof(r->format_name), "Unknown");
        return;
    }

    uint32_t n_short = 0, n_long = 0, n_sync = 0;
    for (uint32_t i = 0; i < r->count; i++) {
        uint32_t w = r->pulses[i];
        if (w > min_w * 10) n_sync++;
        else if (w > min_w * 2) n_long++;
        else n_short++;
    }

    if (n_sync >= 1 && r->count >= 40) {
        r->format = CC1101_FMT_EV1527;
        snprintf(r->format_name, sizeof(r->format_name), "EV1527 (24bit)");
    } else if (n_sync >= 1 && r->count >= 24) {
        r->format = CC1101_FMT_PT2262;
        snprintf(r->format_name, sizeof(r->format_name), "PT2262 (12bit)");
    } else if (r->count >= 16) {
        r->format = CC1101_FMT_OOK_PWM;
        snprintf(r->format_name, sizeof(r->format_name), "OOK-PWM");
    } else {
        r->format = CC1101_FMT_UNKNOWN;
        snprintf(r->format_name, sizeof(r->format_name), "Unknown");
    }
}

static void cc1101_scan_task(void* arg)
{
    int64_t last_edge = esp_timer_get_time();
    uint32_t last_count = 0;
    int64_t last_rssi_poll = 0;

    ESP_LOGI(TAG, "Scan task started (RSSI threshold %d dBm)", CC1101_RSSI_THRESHOLD);

    while (s_scan_running) {
        int64_t now = esp_timer_get_time();

        if (now - last_rssi_poll > 20000) {
            int rssi = cc1101_read_rssi_dbm();
            last_rssi_poll = now;
            if (rssi > s_peak_rssi) s_peak_rssi = rssi;

            if (!s_recording && rssi > CC1101_RSSI_THRESHOLD) {
                portENTER_CRITICAL(&s_isr_mux);
                s_edge_count = 0;
                s_recording = true;
                portEXIT_CRITICAL(&s_isr_mux);
                gpio_intr_enable(CC1101_IRQ_GPIO);
                last_count = 0;
                last_edge = now;
                ESP_LOGI(TAG, "Carrier detected (%d dBm), recording", rssi);
            }
        }

        uint32_t cnt;
        portENTER_CRITICAL(&s_isr_mux);
        cnt = s_edge_count;
        portEXIT_CRITICAL(&s_isr_mux);

        if (cnt != last_count) {
            last_count = cnt;
            last_edge = now;
        }

        if (s_recording && cnt >= 24 && (now - last_edge) > 150000) {
            uint32_t times[CC1101_MAX_PULSES + 1];
            uint32_t n;

            portENTER_CRITICAL(&s_isr_mux);
            s_recording = false;
            n = s_edge_count;
            if (n > CC1101_MAX_PULSES + 1) n = CC1101_MAX_PULSES + 1;
            memcpy(times, (const void*)s_edge_times, n * sizeof(uint32_t));
            portEXIT_CRITICAL(&s_isr_mux);

            uint32_t pc = (n > 1) ? (n - 1) : 0;
            if (pc > CC1101_MAX_PULSES) pc = CC1101_MAX_PULSES;

            memset(&s_result, 0, sizeof(s_result));
            for (uint32_t i = 0; i < pc; i++) {
                uint32_t w = times[i + 1] - times[i];
                if (w == 0) w = 1;
                if (w > 65535) w = 65535;
                s_result.pulses[i] = (uint16_t)w;
            }
            s_result.count = pc;
            s_result.rssi = s_peak_rssi;
            s_result.duration_us = (n > 1) ? (times[n - 1] - times[0]) : 0;
            s_result.frequency = s_frequency_hz;

            cc1101_classify_pulses(&s_result);

            ESP_LOGI(TAG, "Detected: %s, %lu pulses, %lu us, RSSI %d dBm",
                     s_result.format_name, (unsigned long)pc,
                     (unsigned long)s_result.duration_us, s_result.rssi);

            gpio_intr_disable(CC1101_IRQ_GPIO);
            s_has_result = true;
            s_scan_state = CC1101_SCAN_DETECTED;
            s_scan_running = false;
            break;
        }

        if (s_recording && (now - last_edge) > 400000) {
            portENTER_CRITICAL(&s_isr_mux);
            s_recording = false;
            s_edge_count = 0;
            portEXIT_CRITICAL(&s_isr_mux);
            gpio_intr_disable(CC1101_IRQ_GPIO);
            last_count = 0;
            ESP_LOGD(TAG, "Burst discarded (noise)");
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    s_scan_task = NULL;
    if (s_scan_state != CC1101_SCAN_DETECTED) {
        s_scan_state = CC1101_SCAN_IDLE;
    }
    ESP_LOGI(TAG, "Scan task stopped");
    vTaskDelete(NULL);
}

esp_err_t cc1101_scan_start(void)
{
    if (!cc1101_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scan_running) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Re-apply the selected frequency every time a scan starts, so changing
     * the frequency from the web UI always takes effect. FREQ registers must
     * be written in IDLE state. */
    uint8_t sidle = CC1101_STROBE_SIDLE;
    cc1101_spi_transaction(0, &sidle, 1, NULL, 0, 100);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    cc1101_program_frequency_registers();
    uint8_t scal = CC1101_STROBE_SCAL;
    cc1101_spi_transaction(0, &scal, 1, NULL, 0, 100);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    cc1101_write_register(CC1101_REG_PKTCTRL0, 0x0E);
    cc1101_write_register(CC1101_REG_IOCFG0, 0x0D);
    cc1101_write_register(CC1101_REG_MDMCFG2, 0x20);

    /* Arm edge detection but keep interrupts disabled until a carrier appears */
    gpio_set_intr_type(CC1101_IRQ_GPIO, GPIO_INTR_ANYEDGE);
    gpio_intr_disable(CC1101_IRQ_GPIO);

    uint8_t sfrx = CC1101_STROBE_SFRX;
    cc1101_spi_transaction(0, &sfrx, 1, NULL, 0, 100);
    uint8_t srx = CC1101_STROBE_SRX;
    cc1101_spi_transaction(0, &srx, 1, NULL, 0, 100);

    portENTER_CRITICAL(&s_isr_mux);
    s_edge_count = 0;
    s_recording = false;
    portEXIT_CRITICAL(&s_isr_mux);

    s_peak_rssi = -128;
    s_has_result = false;
    s_scan_state = CC1101_SCAN_RUNNING;
    s_scan_running = true;

    BaseType_t ok = xTaskCreate(cc1101_scan_task, "cc1101_scan", 4096, NULL,
                                tskIDLE_PRIORITY + 3, &s_scan_task);
    if (ok != pdPASS) {
        s_scan_running = false;
        s_scan_state = CC1101_SCAN_IDLE;
        s_scan_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Scan started");
    return ESP_OK;
}

void cc1101_scan_stop(void)
{
    s_scan_running = false;
    if (s_scan_state == CC1101_SCAN_RUNNING) {
        s_scan_state = CC1101_SCAN_IDLE;
    }

    for (int i = 0; i < 50 && s_scan_task != NULL; i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (s_scan_task != NULL) {
        vTaskDelete(s_scan_task);
        s_scan_task = NULL;
    }

    portENTER_CRITICAL(&s_isr_mux);
    s_recording = false;
    s_edge_count = 0;
    portEXIT_CRITICAL(&s_isr_mux);
    gpio_intr_disable(CC1101_IRQ_GPIO);

    if (cc1101_is_initialized()) {
        uint8_t sidle = CC1101_STROBE_SIDLE;
        cc1101_spi_transaction(0, &sidle, 1, NULL, 0, 100);
    }

    ESP_LOGI(TAG, "Scan stopped");
}

cc1101_scan_state_t cc1101_scan_state(void)
{
    return s_scan_state;
}

esp_err_t cc1101_scan_get_result(cc1101_capture_result_t* out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_has_result) {
        return ESP_ERR_NOT_FOUND;
    }
    *out = s_result;
    return ESP_OK;
}

esp_err_t cc1101_transmit_pulses(const uint16_t* pulses, uint32_t count)
{
    if (pulses == NULL || count == 0 || count > CC1101_MAX_PULSES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cc1101_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scan_running) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Make sure the transmitter uses the frequency selected in the web UI. */
    uint8_t sidle = CC1101_STROBE_SIDLE;
    cc1101_spi_transaction(0, &sidle, 1, NULL, 0, 100);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    cc1101_program_frequency_registers();
    uint8_t scal = CC1101_STROBE_SCAL;
    cc1101_spi_transaction(0, &scal, 1, NULL, 0, 100);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    cc1101_write_register(CC1101_REG_PKTCTRL0, 0x0E);
    cc1101_write_register(CC1101_REG_MDMCFG2, 0x20);
    cc1101_write_register(CC1101_REG_IOCFG0, 0x2E);

    uint8_t sftx = CC1101_STROBE_SFTX;
    cc1101_spi_transaction(0, &sftx, 1, NULL, 0, 100);

    gpio_set_direction(CC1101_IRQ_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CC1101_IRQ_GPIO, 0);

    uint8_t stx = CC1101_STROBE_STX;
    cc1101_spi_transaction(0, &stx, 1, NULL, 0, 100);
    esp_rom_delay_us(1000);

    uint8_t level = 1;
    for (uint32_t i = 0; i < count; i++) {
        gpio_set_level(CC1101_IRQ_GPIO, level);
        esp_rom_delay_us(pulses[i]);
        level = level ? 0 : 1;
    }
    gpio_set_level(CC1101_IRQ_GPIO, 0);

    sidle = CC1101_STROBE_SIDLE;
    cc1101_spi_transaction(0, &sidle, 1, NULL, 0, 100);

    gpio_set_direction(CC1101_IRQ_GPIO, GPIO_MODE_INPUT);
    cc1101_write_register(CC1101_REG_IOCFG0, 0x0D);

    ESP_LOGI(TAG, "Transmitted %lu pulses", (unsigned long)count);
    return ESP_OK;
}
