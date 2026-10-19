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

/* SPI 引脚定义（ESP32-S3） */
#define CC1101_SCLK_GPIO GPIO_NUM_12
#define CC1101_MOSI_GPIO GPIO_NUM_11
#define CC1101_MISO_GPIO GPIO_NUM_13

/* CC1101 SPI Commands
 * 命令字节格式: R/W# | B | A5..A0
 *   写寄存器 = 0x00 | addr
 *   读寄存器 = 0x80 | addr
 *   写 burst = 0x40 | addr
 *   读 burst = 0xC0 | addr
 *   Strobe  = 0x30..0x3D
 */
#define CC1101_SPI_WRITE_REGISTER   0x00
#define CC1101_SPI_READ_REGISTER    0x80
#define CC1101_SPI_WRITE_BURST      0x40
#define CC1101_SPI_READ_BURST       0xC0
#define CC1101_SPI_READ_SINGLE      0x80
#define CC1101_SPI_WRITE_SINGLE     0x00
#define CC1101_SPI_NONPAYLOAD       0x00
#define CC1101_SPI_READ_STATUS      0xC0

/* Strobe commands */
#define CC1101_STROBE_SRES          0x30
#define CC1101_STROBE_SFSTXON       0x31
#define CC1101_STROBE_SXOFF         0x32
#define CC1101_STROBE_SCAL          0x33
#define CC1101_STROBE_SRX           0x34
#define CC1101_STROBE_STX           0x35
#define CC1101_STROBE_SIDLE         0x36
#define CC1101_STROBE_SWOR          0x38
#define CC1101_STROBE_SPWD          0x39
#define CC1101_STROBE_SFRX          0x3A
#define CC1101_STROBE_SFTX          0x3B
#define CC1101_STROBE_SNOP          0x3D

#define CC1101_SPI_FACTORY_RESET    CC1101_STROBE_SRES

/* CC1101 Configuration Registers (0x00 - 0x2D) */
#define CC1101_REG_IOCFG2           0x00
#define CC1101_REG_IOCFG1           0x01
#define CC1101_REG_IOCFG0           0x02
#define CC1101_REG_FIFOTHR          0x03
#define CC1101_REG_SYNC1            0x04
#define CC1101_REG_SYNC0            0x05
#define CC1101_REG_PKTLEN           0x06
#define CC1101_REG_PKTCTRL1         0x07
#define CC1101_REG_PKTCTRL0         0x08
#define CC1101_REG_ADDR             0x09
#define CC1101_REG_CHANNR           0x0A
#define CC1101_REG_FSCTRL1          0x0B
#define CC1101_REG_FSCTRL0          0x0C
#define CC1101_REG_FREQ2            0x0D
#define CC1101_REG_FREQ1            0x0E
#define CC1101_REG_FREQ0            0x0F
#define CC1101_REG_MDMCFG4          0x10
#define CC1101_REG_MDMCFG3          0x11
#define CC1101_REG_MDMCFG2          0x12
#define CC1101_REG_MDMCFG1          0x13
#define CC1101_REG_MDMCFG0          0x14
#define CC1101_REG_DEVIATN          0x15
#define CC1101_REG_MCSM1            0x16
#define CC1101_REG_MCSM0            0x17
#define CC1101_REG_FOCCFG           0x18
#define CC1101_REG_BSCFG            0x19
#define CC1101_REG_AGCCTRL2         0x1A
#define CC1101_REG_AGCCTRL1         0x1B
#define CC1101_REG_AGCCTRL0         0x1C
#define CC1101_REG_WOREVT1          0x1D
#define CC1101_REG_WOREVT0          0x1E
#define CC1101_REG_WORCTRL          0x1F
#define CC1101_REG_FREND1           0x20
#define CC1101_REG_FREND0           0x21
#define CC1101_REG_FSCAL3           0x22
#define CC1101_REG_FSCAL2           0x23
#define CC1101_REG_FSCAL1           0x24
#define CC1101_REG_FSCAL0           0x25
#define CC1101_REG_RCCTRL1          0x26
#define CC1101_REG_RCCTRL0          0x27
#define CC1101_REG_FSTEST           0x28
#define CC1101_REG_PTEST            0x29
#define CC1101_REG_AGCTEST          0x2A
#define CC1101_REG_TEST2            0x2B
#define CC1101_REG_TEST1            0x2C
#define CC1101_REG_TEST0            0x2D
#define CC1101_REG_PATABLE          0x3E

/* Status registers (0x30 - 0x3B), read with burst bit (0xC0 | addr) */
#define CC1101_REG_PARTNUM          0x30
#define CC1101_REG_VERSION          0x31
#define CC1101_REG_FREQEST          0x32
#define CC1101_REG_LQI              0x33
#define CC1101_REG_RSSI             0x34
#define CC1101_REG_MARCSTATE        0x35
#define CC1101_REG_WORTIME1         0x36
#define CC1101_REG_WORTIME0         0x37
#define CC1101_REG_PKTSTATUS        0x38
#define CC1101_REG_VCO_VC_DAC       0x39
#define CC1101_REG_TXBYTES          0x3A
#define CC1101_REG_RXBYTES          0x3B

/* 兼容旧代码的别名 */
#define CC1101_REG_CHANNEL          CC1101_REG_CHANNR
#define CC1101_REG_FIFOCNT          CC1101_REG_RXBYTES
#define CC1101_REG_TXFIFOCNT        CC1101_REG_TXBYTES
#define CC1101_REG_RXFIFOCNT        CC1101_REG_RXBYTES
#define CC1101_REG_PKTCTRL2         CC1101_REG_PKTCTRL1
#define CC1101_REG_FREND0_SETTING    0xB6
#define CC1101_REG_CHANNEL_SETTING   0x07
#define CC1101_REG_AGCCTRL1_SETTING  0x00
#define CC1101_REG_AGCCTRL0_SETTING  0x07
#define CC1101_REG_MCSM0_SETTING     0xF8
#define CC1101_REG_MCSM1_SETTING     0x00
#define CC1101_REG_FSMCTRL_SETTING   0x0D

/* CC1101 Configuration (frequency is selectable at runtime) */
#define CC1101_FREQUENCY_BASE       443000000.0
#define CC1101_CHANNEL_SPACE        500000.0
#define CC1101_CHANNEL_1            0

/* 默认工作频率：315MHz，方便直接用常见 315 遥控器测试。
 * 可通过网页切换为 433.92 / 443 / 868 / 915 等。 */
#define CC1101_DEFAULT_FREQUENCY    315000000U

/* CC1101 支持的三个频段（芯片硬件限制） */
#define CC1101_BAND1_MIN            300000000U   /* 300 - 348 MHz */
#define CC1101_BAND1_MAX            348000000U
#define CC1101_BAND2_MIN            387000000U   /* 387 - 464 MHz */
#define CC1101_BAND2_MAX            464000000U
#define CC1101_BAND3_MIN            779000000U   /* 779 - 928 MHz */
#define CC1101_BAND3_MAX            928000000U

/* Signal acquisition constants */
#define CC1101_SIGNAL_BUF_SIZE      8192
#define CC1101_SIGNAL_SAMPLE_RATE    38400  // Samples per second
#define CC1101_MODULATION_GFSK      2

/* Raw OOK pulse capture */
#define CC1101_MAX_PULSES           512
#define CC1101_RSSI_THRESHOLD       (-75)   /* dBm: start recording above this */

/* Detected signal formats */
typedef enum {
    CC1101_FMT_UNKNOWN = 0,
    CC1101_FMT_OOK_PWM = 1,
    CC1101_FMT_EV1527  = 2,
    CC1101_FMT_PT2262  = 3,
} cc1101_format_t;

/* Result of one successful scan */
typedef struct {
    uint16_t pulses[CC1101_MAX_PULSES];  /* pulse widths in microseconds, alternating level */
    uint32_t count;                      /* number of pulse widths */
    int      rssi;                       /* peak RSSI in dBm */
    uint8_t  format;                     /* cc1101_format_t */
    char     format_name[32];
    uint32_t duration_us;                /* total captured duration */
    uint32_t frequency;                  /* receive frequency in Hz */
} cc1101_capture_result_t;

/* Scan state */
typedef enum {
    CC1101_SCAN_IDLE = 0,
    CC1101_SCAN_RUNNING,
    CC1101_SCAN_DETECTED,
} cc1101_scan_state_t;

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
 * @brief Set the working frequency (OOK/ASK capture+TX settings are kept).
 *        Valid CC1101 bands: 300-348, 387-464, 779-928 MHz.
 * @param frequency_hz Carrier frequency in Hz, e.g. 315000000 / 433920000
 */
esp_err_t cc1101_set_frequency(uint32_t frequency_hz);

/**
 * @brief Get the currently configured working frequency in Hz.
 */
uint32_t cc1101_get_frequency(void);

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

/**
 * @brief Start a one-shot capture using the driver's internal buffer.
 *        Capture stops automatically when the buffer is full or after timeout.
 */
esp_err_t cc1101_capture_start(void);

/**
 * @brief Stop an ongoing capture.
 */
void cc1101_capture_stop(void);

/**
 * @brief Returns true when no capture is running.
 */
bool cc1101_capture_is_done(void);

/**
 * @brief Get captured data pointer and length. Valid until next capture.
 */
uint8_t* cc1101_capture_get_data(uint32_t* len);

/**
 * @brief Check whether the driver (SPI) has been initialized.
 */
bool cc1101_is_initialized(void);

/**
 * @brief Start continuous scanning. Returns immediately; scan runs in background.
 *        Stops automatically when a valid signal is detected (state becomes DETECTED).
 */
esp_err_t cc1101_scan_start(void);

/**
 * @brief Stop scanning (idle).
 */
void cc1101_scan_stop(void);

/**
 * @brief Current scan state.
 */
cc1101_scan_state_t cc1101_scan_state(void);

/**
 * @brief Copy the last detection result. Returns ESP_OK only in DETECTED state.
 */
esp_err_t cc1101_scan_get_result(cc1101_capture_result_t* out);

/**
 * @brief Transmit a previously captured pulse sequence.
 */
esp_err_t cc1101_transmit_pulses(const uint16_t* pulses, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif // CC1101_DRIVER_H