#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// ADS1298 pin mappings
#define ADS_RESET_PIN 12   // P1.12
#define ADS_START_PIN 11   // P1.11
#define ADS_PWDN_PIN  13   // P1.13
#define ADS_DRDY_PIN  14   // P1.14
#define ADS_CS_PIN    22   // P0.22

// Onboard LED
#define LED1_PIN      6    // P1.06

#define GPIO0_NODE DT_NODELABEL(gpio0)
#define GPIO1_NODE DT_NODELABEL(gpio1)

static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));

static const struct spi_config spi_cfg = {
    .frequency = 1000000,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPHA,
    .slave = 0,
    .cs = NULL,  // Manual CS control
};

void main(void)
{
    const struct device *gpio0_dev = DEVICE_DT_GET(GPIO0_NODE);
    const struct device *gpio1_dev = DEVICE_DT_GET(GPIO1_NODE);

    if (!device_is_ready(spi_dev) || !device_is_ready(gpio0_dev) || !device_is_ready(gpio1_dev)) {
        LOG_ERR("SPI or GPIO devices not ready");
        return;
    }

    LOG_INF("GPIO and SPI devices are ready");

    // GPIO0: CS, DRDY
    gpio_pin_configure(gpio0_dev, ADS_CS_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure(gpio0_dev, ADS_DRDY_PIN, GPIO_INPUT);

    // GPIO1: RESET, START, PWDN, LED
    gpio_pin_configure(gpio1_dev, ADS_RESET_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure(gpio1_dev, ADS_START_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure(gpio1_dev, ADS_PWDN_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure(gpio1_dev, LED1_PIN, GPIO_OUTPUT_ACTIVE);

    // Turn on LED1 to confirm logic HIGH
    gpio_pin_set(gpio1_dev, LED1_PIN, 1);
    LOG_INF("LED1 ON (P1.06 HIGH)");

    // Step 1: Bring PWDN HIGH to power up ADS1298
    gpio_pin_set(gpio1_dev, ADS_PWDN_PIN, 1);
    k_usleep(100);  // Wait >18 tCLK (~40us), give 100 us safety

    // Step 2: Pulse RESET (LOW then HIGH)
    gpio_pin_set(gpio1_dev, ADS_RESET_PIN, 0);
    k_msleep(10);
    gpio_pin_set(gpio1_dev, ADS_RESET_PIN, 1);
    k_msleep(10);
    LOG_INF("ADS1298 Reset done");

    // Step 3: Set START HIGH for continuous conversion
    gpio_pin_set(gpio1_dev, ADS_START_PIN, 1);
    LOG_INF("ADS1298 Start enabled");

    // Step 4: Set CS HIGH initially
    gpio_pin_set(gpio0_dev, ADS_CS_PIN, 1);

    // Step 5: Monitor DRDY (P1.14)
    LOG_INF("Monitoring DRDY (P1.14)...");
    for (int i = 0; i < 50; i++) {
        int drdy = gpio_pin_get(gpio0_dev, ADS_DRDY_PIN);
        LOG_INF("DRDY state: %d", drdy);
        k_msleep(10);
    }

    LOG_INF("SPI ready, starting ADS1298 test");

    // Step 6: Send RREG command to read Device ID (register 0x00)
    static uint8_t tx_buf[] = { 0x20, 0x00 };
    static uint8_t rx_buf[2] = {0};

    const struct spi_buf tx_spi_buf = {
        .buf = tx_buf,
        .len = sizeof(tx_buf),
    };
    const struct spi_buf rx_spi_buf = {
        .buf = rx_buf,
        .len = sizeof(rx_buf),
    };
    const struct spi_buf_set tx_set = {
        .buffers = &tx_spi_buf,
        .count = 1,
    };
    const struct spi_buf_set rx_set = {
        .buffers = &rx_buf,
        .count = 1,
    };

    gpio_pin_set(gpio0_dev, ADS_CS_PIN, 0);  // CS LOW
    int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
    gpio_pin_set(gpio0_dev, ADS_CS_PIN, 1);  // CS HIGH

    if (ret == 0) {
        LOG_INF("ADS1298 ID: 0x%02X", rx_buf[1]);
    } else {
        LOG_ERR("SPI transfer failed: %d", ret);
    }
}
