#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include "encoder_custom.h"

static const struct device *gpio_dev;
static struct gpio_callback enc_cb;

static void enc_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
}

static int encoder_init(void)
{
    gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (!device_is_ready(gpio_dev)) {
        return -ENODEV;
    }

    gpio_pin_configure(gpio_dev, ENC_A_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, ENC_B_PIN, GPIO_INPUT | GPIO_PULL_UP);

    gpio_init_callback(&enc_cb, enc_isr, BIT(ENC_A_PIN) | BIT(ENC_B_PIN));
    gpio_add_callback(gpio_dev, &enc_cb);

    gpio_pin_interrupt_configure(gpio_dev, ENC_A_PIN, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio_dev, ENC_B_PIN, GPIO_INT_EDGE_BOTH);

    return 0;
}

SYS_INIT(encoder_init, APPLICATION, 90);
