#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include "encoder_custom.h"

static const struct device *gpio_dev;
static struct gpio_callback enc_cb;
static struct k_work enc_work;
static struct k_work_delayable enc_release_work;

static volatile int8_t position;
static volatile uint8_t last_state;
static volatile int8_t last_dir;

static const int8_t enc_steps[] = {
    0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0,
};

static void enc_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uint8_t a = gpio_pin_get(gpio_dev, ENC_A_PIN);
    uint8_t b = gpio_pin_get(gpio_dev, ENC_B_PIN);
    uint8_t state = (a << 1) | b;
    uint8_t idx = (last_state << 2) | state;
    last_state = state;

    int8_t step = enc_steps[idx & 0x0F];
    if (step) {
        position += step;
        last_dir = step;
        k_work_submit(&enc_work);
    }
}

static void enc_release_handler(struct k_work_delayable *work)
{
    uint32_t usage = last_dir > 0
        ? (HID_USAGE_CONSUMER << 16) | HID_USAGE_CONSUMER_VOLUME_INCREMENT
        : (HID_USAGE_CONSUMER << 16) | HID_USAGE_CONSUMER_VOLUME_DECREMENT;
    zmk_hid_release(usage);
}

static void enc_work_handler(struct k_work *work)
{
    int8_t pos = position;
    position = 0;

    if (pos > 0) {
        uint32_t usage = (HID_USAGE_CONSUMER << 16) | HID_USAGE_CONSUMER_VOLUME_INCREMENT;
        zmk_hid_press(usage);
        k_work_reschedule(&enc_release_work, K_MSEC(RELEASE_MS));
    } else if (pos < 0) {
        uint32_t usage = (HID_USAGE_CONSUMER << 16) | HID_USAGE_CONSUMER_VOLUME_DECREMENT;
        zmk_hid_press(usage);
        k_work_reschedule(&enc_release_work, K_MSEC(RELEASE_MS));
    }
}

static int encoder_init(void)
{
    gpio_dev = device_get_binding(ENC_A_PORT);
    if (!gpio_dev) {
        return -ENODEV;
    }

    gpio_pin_configure(gpio_dev, ENC_A_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, ENC_B_PIN, GPIO_INPUT | GPIO_PULL_UP);

    last_state = (gpio_pin_get(gpio_dev, ENC_A_PIN) << 1) | gpio_pin_get(gpio_dev, ENC_B_PIN);

    k_work_init(&enc_work, enc_work_handler);
    k_work_init_delayable(&enc_release_work, enc_release_handler);

    gpio_pin_interrupt_configure(gpio_dev, ENC_A_PIN, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio_dev, ENC_B_PIN, GPIO_INT_EDGE_BOTH);

    gpio_init_callback(&enc_cb, enc_isr, BIT(ENC_A_PIN) | BIT(ENC_B_PIN));
    gpio_add_callback(gpio_dev, &enc_cb);

    return 0;
}

SYS_INIT(encoder_init, APPLICATION, 90);
