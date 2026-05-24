#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys_clock.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include "encoder_custom.h"

enum { ENC_O = 0 };
enum { TYPE_SCROLL };

static const struct device *gpio1;

struct enc_state {
    uint8_t last_state;
    volatile int8_t position;
    struct k_work work;
};

static struct enc_state enc = {0};

static const int8_t enc_steps[] = {
    0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0,
};

static uint32_t last_cycles;

static void pin_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uint32_t now = k_cycle_get_32();
    if (k_cyc_to_us_floor32(now - last_cycles) < 500) return;
    last_cycles = now;

    uint8_t a = gpio_pin_get(gpio1, ENC_O_A);
    uint8_t b = gpio_pin_get(gpio1, ENC_O_B);
    uint8_t state = (a << 1) | b;
    uint8_t idx = (enc.last_state << 2) | state;
    enc.last_state = state;

    int8_t step = enc_steps[idx & 0x0F];
    if (step) {
        enc.position += step;
        k_work_submit(&enc.work);
    }
}

static struct gpio_callback gpio_cb;

static void send_scroll(struct k_work *work)
{
    while (1) {
        int key = irq_lock();
        int8_t pos = enc.position;
        if (pos >= 2) {
            enc.position -= 2;
            irq_unlock(key);
        } else if (pos <= -2) {
            enc.position += 2;
            irq_unlock(key);
        } else {
            irq_unlock(key);
            break;
        }
        zmk_hid_mouse_scroll_set(0, pos > 0 ? 1 : -1);
        zmk_endpoints_send_mouse_report();
        k_sleep(K_MSEC(5));
        zmk_hid_mouse_clear();
        zmk_endpoints_send_mouse_report();
    }
}

static int encoder_init(void)
{
    gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (!device_is_ready(gpio1))
        return -ENODEV;

    gpio_pin_configure(gpio1, ENC_O_A, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio1, ENC_O_B, GPIO_INPUT | GPIO_PULL_UP);

    uint8_t a = gpio_pin_get(gpio1, ENC_O_A);
    uint8_t b = gpio_pin_get(gpio1, ENC_O_B);
    enc.last_state = (a << 1) | b;

    k_work_init(&enc.work, send_scroll);

    gpio_init_callback(&gpio_cb, pin_isr, BIT(ENC_O_A) | BIT(ENC_O_B));
    gpio_add_callback(gpio1, &gpio_cb);
    gpio_pin_interrupt_configure(gpio1, ENC_O_A, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio1, ENC_O_B, GPIO_INT_EDGE_BOTH);

    return 0;
}

SYS_INIT(encoder_init, APPLICATION, 90);
