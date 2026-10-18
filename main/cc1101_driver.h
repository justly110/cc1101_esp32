#ifndef CC1101_DRIVER_H
#define CC1101_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include <driver/gpio.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC1101_SPI_HOST SPI2_HOST
#define CC1101_IRQ_GPIO GPIO_NUM_9
#define CC1101_CS_GPIO GPIO_NUM_10

/* CC1101 SPI Commands */
#define CC1101_SPI_READ_BURST       0xFF
#define CC1101_SPI_READ_SINGLE      0x80
#define CC1101_SPI_WRITE_BURST      0xFE
#define CC1101_SPI_WRITE_SINGLE     0x40
#define CC1101_SPI_NONPAYLOAD       0x00
#define CC1101_SPI_FACTORY_RESET    0x0E
#define CC1101_SPI_READ_STATUS      0x08
#define CC1101_SPI_WRITE_REGISTER   0x0B
#define CC1101_SPI_READ_REGISTER    0x0C

/* CC1101 Registers */
#define CC1101_REG_IOCFG2           0x00
#define CC1101_REG_IOCFG0           0x02
#define CC1101_REG_FIFOTHR          0x03
#define CC1101_REG_PKTCTRL0         0x05
#define CC1101_REG_PATABLE          0x09
#define CC1101_REG_CHANNEL          0x0A
#define CC1101_REG_FREND0           0x10
#define CC1101_REG_FREND1           0x11
#define CC1101_REG_MCSM0            0x12
#define CC1101_REG_FSMCTRL          0x13
#define CC1101_REG_FSCAL1           0x1A
#define CC1101_REG_FSCAL2           0x1B
#define CC1101_REG_FSCAL3           0x1C
#define CC1101_REG_AGCCTRL0         0x1D
#define CC1101_REG_AGCCTRL1         0x1E
#define CC1101_REG_AGCCTRL2         0x1F
#define CC1101_REG_FREND2           0x21
#define CC1101_REG_FREND3           0x22
#define CC1101_REG_FREND4           0x23
#define CC1101_REG_FSCAL_CTRL       0x24
#define CC1101_REG_FSTEST           0x2A
#define CC1101_REG_TEST2            0x2D
#define CC1101_REG_TEST1            0x2E
#define CC1101_REG_TEST0            0x2F
#define CC1101_REG_TXFIFOTHRESH      0x37
#define CC1101_REG_TXFIFOCNT        0x38
#define CC1101_REG_RXFIFOCNT        0x3A
#define CC1101_REG_VCO_VC_DAC       0x4D
#define CC1101_REG_FIFOPAGE         0x4F
#define CC1101_REG_FIFOPIND         0x50
#define CC1101_REG_FIFOCNT          0x51
#define CC1101_REG_TXPER            0x55
#define CC1101_REG_TXPER1           0x56
#define CC1101_REG_TXPER2           0x57
#define CC1101_REG_PKTCTRL1         0x72
#define CC1101_REG_PKTCTRL2         0x73
#define CC1101_REG_FREND0_SETTING    0xB6
#define CC1101_REG_CHANNEL_SETTING   0x07
#define CC1101_REG_AGCCTRL1_SETTING  0x00
#define CC1101_REG_AGCCTRL0_SETTING  0x07
#define CC1101_REG_MCSM0_SETTING     0xF8
#define CC1101_REG_MCSM1_SETTING     0x00
#define CC1101_REG_FSMCTRL_SETTING   0x0D

/* CC1101 Configuration for 443MHz */
#define CC1101_FREQUENCY_BASE       443000000.0
#define CC1101_CHANNEL_SPACE        500000.0
#define CC1101_CHANNEL_1            0

/* Signal acquisition constants */
#define CC1101_SIGNAL_BUF_SIZE      8192
#define CC1101_SIGNAL_SAMPLE_RATE    38400  // Samples per second
#define CC1101_MODULATION_GFSK      2

/* Status codes */
#define CC1101_OK                   0
#define CC1101_ERROR_SPI            -1
#define CC1101_ERROR_TIMEOUT        -2
#define CC1101_ERROR_FIFO_FULL      -3
#define CC1101_ERROR_INVALID_PARAM  -4

typedef struct {
    uint32_t frequency;           // Frequency in Hz
    uint16_t channel;             // Channel number
    uint8_t modulation;           // Modulation type
    uint16_t sample_rate;         // Sample rate
    bool continuous;              // Continuous mode
} cc1101_config_t;

typedef struct {
    uint8_t* buffer;              // Signal buffer
    uint32_t size;                // Buffer size
    uint32_t samples_count;       // Number of samples collected
    bool active;                  // Acquisition active flag
    uint32_t frequency;           // Signal frequency
    uint32_t timestamp;           // Signal timestamp
} cc1101_signal_t;

/**
 * @brief Initialize CC1101 SPI interface
 */
esp_err_t cc1101_init(void);

/**
 * @brief Reset CC1101 module
 */
void cc1101_reset(void);

/**
 * @brief Write configuration to CC1101 registers
 */
esp_err_t cc1101_write_config(const cc1101_config_t* config);

/**
 * @brief Read data from CC1101 FIFO
 */
esp_err_t cc1101_read_fifo(uint8_t* buffer, uint32_t* size);

/**
 * @brief Start signal acquisition
 */
esp_err_t cc1101_start_acquisition(cc1101_signal_t* signal);

/**
 * @brief Stop signal acquisition
 */
void cc1101_stop_acquisition(void);

/**
 * @brief Get current signal status
 */
esp_err_t cc1101_get_signal_status(cc1101_signal_t* signal);

/**
 * @brief Send signal via CC1101
 */
esp_err_t cc1101_send_signal(const uint8_t* data, uint32_t size);

/**
 * @brief Get version information
 */
esp_err_t cc1101_get_version(char* version_str, uint32_t max_len);

/**
 * @brief Check if CC1101 is connected by reading PARTNUM and VERSION registers
 * @return ESP_OK if connected, ESP_ERR_NOT_FOUND if not found, other errors on SPI failure
 */
esp_err_t cc1101_check_connection(void);

#ifdef __cplusplus
}
#endif

#endif // CC1101_DRIVER_H