#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include "encoder_custom.h"

static const struct device *gpio_dev;
static struct gpio_callback enc_cb;
static struct k_work enc_work;
static struct k_work_delayable ble_switch_work;

static volatile int8_t position;
static volatile uint8_t last_state;

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
        k_work_submit(&enc_work);
    }
}

static void enc_work_handler(struct k_work *work)
{
    int8_t pos = position;
    position = 0;

    if (pos) {
        int8_t steps = pos / 2;
        if (!steps) {
            return;
        }
        uint32_t usage = (steps > 0)
            ? (HID_USAGE_KEY << 16) | HID_USAGE_KEY_KEYBOARD_UPARROW
            : (HID_USAGE_KEY << 16) | HID_USAGE_KEY_KEYBOARD_DOWNARROW;

        uint8_t n = abs(steps);
        for (uint8_t i = 0; i < n; i++) {
            zmk_hid_press(usage);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(10));
            zmk_hid_release(usage);
            zmk_endpoints_send_report(HID_USAGE_KEY);
        }
    }
}

static void ble_switch_handler(struct k_work *work)
{
    zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE);
}

static int encoder_init(void)
{
    gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (!device_is_ready(gpio_dev)) {
        return -ENODEV;
    }

    gpio_pin_configure(gpio_dev, ENC_A_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, ENC_B_PIN, GPIO_INPUT | GPIO_PULL_UP);

    last_state = (gpio_pin_get(gpio_dev, ENC_A_PIN) << 1) |
                  gpio_pin_get(gpio_dev, ENC_B_PIN);

    k_work_init(&enc_work, enc_work_handler);

    gpio_init_callback(&enc_cb, enc_isr, BIT(ENC_A_PIN) | BIT(ENC_B_PIN));
    gpio_add_callback(gpio_dev, &enc_cb);

    gpio_pin_interrupt_configure(gpio_dev, ENC_A_PIN, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio_dev, ENC_B_PIN, GPIO_INT_EDGE_BOTH);

    k_work_init_delayable(&ble_switch_work, ble_switch_handler);
    k_work_schedule(&ble_switch_work, K_SECONDS(3));

    return 0;
}

SYS_INIT(encoder_init, APPLICATION, 90);
