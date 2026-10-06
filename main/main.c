#include <stdbool.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SPI_MOSI_PIN  7
#define SPI_MISO_PIN  1
#define SPI_SCK_PIN   14
#define USB_CS_PIN    9

/*
 * MAX3421E command byte:
 * rrrrr0wa
 *
 * bit 1: write = 1, read = 0
 * bit 0: ACKSTAT
 */
#define MAX3421E_WRITE_BIT       0x02

/* MAX3421E registers, already shifted into command-byte position. */
#define MAX_REG_USBIRQ           0x68
#define MAX_REG_USBCTL           0x78
#define MAX_REG_PINCTL           0x88
#define MAX_REG_REVISION         0x90
#define MAX_REG_MODE             0xD8

/* Register bits. */
#define MAX_USBIRQ_OSCOKIRQ      0x01
#define MAX_USBCTL_CHIPRES       0x20

#define MAX_PINCTL_FDUPSPI       0x10
#define MAX_PINCTL_INTLEVEL      0x08

#define MAX_MODE_HOST            0x01
#define MAX_MODE_DMPULLDN        0x40
#define MAX_MODE_DPPULLDN        0x80

#define MAX3421E_OSC_TIMEOUT_MS  100

static const char *TAG = "MAX3421E";
static spi_device_handle_t max3421e_spi;

/*
 * Write one MAX3421E register.
 *
 * CS is asserted/deasserted automatically by the ESP-IDF SPI driver.
 */
static esp_err_t max3421e_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx_data[2] = {
        (uint8_t)(reg | MAX3421E_WRITE_BIT),
        value
    };

    spi_transaction_t transaction = {
        .length = sizeof(tx_data) * 8,
        .tx_buffer = tx_data,
    };

    return spi_device_transmit(max3421e_spi, &transaction);
}

/*
 * Read one MAX3421E register.
 *
 * The MAX3421E returns its status byte during the command byte and
 * returns the requested register during the second SPI byte.
 */
static esp_err_t max3421e_read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t tx_data[2] = {
        (uint8_t)(reg & ~MAX3421E_WRITE_BIT),
        0x00
    };

    uint8_t rx_data[2] = {0};

    spi_transaction_t transaction = {
        .length = sizeof(tx_data) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    esp_err_t err = spi_device_transmit(max3421e_spi, &transaction);
    if (err == ESP_OK) {
        *value = rx_data[1];
    }

    return err;
}

static esp_err_t max3421e_spi_init(void)
{
    ESP_LOGI(TAG, "Initializing SPI bus and MAX3421E device");

    spi_bus_config_t bus_config = {
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .sclk_io_num = SPI_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 1000 * 1000,  /* Start conservatively at 1 MHz. */
        .mode = 0,                      /* MAX3421E supports SPI mode 0. */
        .spics_io_num = USB_CS_PIN,
        .queue_size = 1,
    };

    esp_err_t err = spi_bus_initialize(
        SPI2_HOST,
        &bus_config,
        SPI_DMA_CH_AUTO
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    err = spi_bus_add_device(
        SPI2_HOST,
        &device_config,
        &max3421e_spi
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Adding MAX3421E SPI device failed: %s", esp_err_to_name(err));
        spi_bus_free(SPI2_HOST);
        return err;
    }

    ESP_LOGI(TAG, "SPI bus/device initialized");
    return ESP_OK;
}

/*
 * Equivalent to the essential hardware portion of Usb.Init().
 *
 * This:
 *   1. Configures full-duplex SPI.
 *   2. Resets the MAX3421E.
 *   3. Waits for its oscillator/PLL.
 *   4. Places it in USB host mode.
 */
static esp_err_t max3421e_init(void)
{
    esp_err_t err;
    uint8_t value;

    err = max3421e_write_reg(
        MAX_REG_PINCTL,
        MAX_PINCTL_FDUPSPI | MAX_PINCTL_INTLEVEL
    );
    if (err != ESP_OK) {
        return err;
    }

    /* Stop the MAX3421E oscillator. */
    err = max3421e_write_reg(MAX_REG_USBCTL, MAX_USBCTL_CHIPRES);
    if (err != ESP_OK) {
        return err;
    }

    /* Release reset and restart the oscillator. */
    err = max3421e_write_reg(MAX_REG_USBCTL, 0x00);
    if (err != ESP_OK) {
        return err;
    }

    /* Wait for OSCOKIRQ, using a bounded timeout. */
    for (int elapsed_ms = 0;
         elapsed_ms < MAX3421E_OSC_TIMEOUT_MS;
         elapsed_ms++) {

        err = max3421e_read_reg(MAX_REG_USBIRQ, &value);
        if (err != ESP_OK) {
            return err;
        }

        if (value & MAX_USBIRQ_OSCOKIRQ) {
            ESP_LOGI(TAG, "MAX3421E oscillator ready");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (!(value & MAX_USBIRQ_OSCOKIRQ)) {
        ESP_LOGE(TAG, "MAX3421E oscillator not ready after %d ms",
                 MAX3421E_OSC_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }

    /* Enable USB host mode and the D+/D- pull-down resistors. */
    err = max3421e_write_reg(
        MAX_REG_MODE,
        MAX_MODE_HOST |
        MAX_MODE_DMPULLDN |
        MAX_MODE_DPPULLDN
    );

    return err;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-C5 MAX3421E SPI initialization test");

    esp_err_t result = max3421e_spi_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "MAX3421E initialization failed: %s", esp_err_to_name(result));
        return;
    }

    uint8_t revision = 0;
    result = max3421e_read_reg(MAX_REG_REVISION, &revision);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "MAX3421E revision register read failed: %s",
                 esp_err_to_name(result));
        ESP_LOGE(TAG, "MAX3421E initialization failed");
        return;
    }
    ESP_LOGI(TAG, "MAX3421E revision register: 0x%02X", revision);

    result = max3421e_init();

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "MAX3421E initialized successfully");
    } else {
        ESP_LOGE(
            TAG,
            "MAX3421E initialization failed: %s",
            esp_err_to_name(result)
        );
    }

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
