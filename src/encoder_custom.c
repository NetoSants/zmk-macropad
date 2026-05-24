#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys_clock.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include "encoder_custom.h"

enum { ENC_O = 0, ENC_P, ENC_Q, NUM_ENCODERS };
enum { TYPE_SCROLL, TYPE_VOLUME, TYPE_HSCROLL };
enum { BLE_LED_PIN = 16 };

static const struct device *gpio0;
static const struct device *gpio1;

static struct gpio_callback cb_gpio0;
static struct gpio_callback cb_gpio1;

struct enc_state {
    uint8_t last_state;
    volatile int8_t position;
    struct k_work work;
    uint8_t type;
};

static struct enc_state encoders[NUM_ENCODERS];

static const int8_t enc_steps[] = {
    0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0,
};

static uint32_t last_cycles_gpio0;
static uint32_t last_cycles_gpio1;

static void process_encoder(int idx, const struct device *a_dev, int a_pin,
                            const struct device *b_dev, int b_pin)
{
    uint8_t a = gpio_pin_get(a_dev, a_pin);
    uint8_t b = gpio_pin_get(b_dev, b_pin);
    uint8_t state = (a << 1) | b;
    uint8_t idx_tbl = (encoders[idx].last_state << 2) | state;
    encoders[idx].last_state = state;

    int8_t step = enc_steps[idx_tbl & 0x0F];
    if (step) {
        encoders[idx].position += step;
        k_work_submit(&encoders[idx].work);
    }
}

static void isr_gpio0(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uint32_t now = k_cycle_get_32();
    if (k_cyc_to_us_floor32(now - last_cycles_gpio0) < 500) return;
    last_cycles_gpio0 = now;

    if (pins & (BIT(ENC_P_A) | BIT(ENC_P_B)))
        process_encoder(ENC_P, gpio0, ENC_P_A, gpio0, ENC_P_B);

    if (pins & BIT(ENC_Q_A))
        process_encoder(ENC_Q, gpio0, ENC_Q_A, gpio1, ENC_Q_B);
}

static void isr_gpio1(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uint32_t now = k_cycle_get_32();
    if (k_cyc_to_us_floor32(now - last_cycles_gpio1) < 500) return;
    last_cycles_gpio1 = now;

    if (pins & (BIT(ENC_O_A) | BIT(ENC_O_B)))
        process_encoder(ENC_O, gpio1, ENC_O_A, gpio1, ENC_O_B);

    if (pins & BIT(ENC_Q_B))
        process_encoder(ENC_Q, gpio0, ENC_Q_A, gpio1, ENC_Q_B);
}

static void send_scroll(struct k_work *work)
{
    struct enc_state *enc = CONTAINER_OF(work, struct enc_state, work);
    while (1) {
        int key = irq_lock();
        int8_t pos = enc->position;
        if (pos >= 2) {
            enc->position -= 2;
            irq_unlock(key);
        } else if (pos <= -2) {
            enc->position += 2;
            irq_unlock(key);
        } else {
            irq_unlock(key);
            break;
        }

        int dir = pos > 0 ? 1 : -1;
        if (enc->type == TYPE_VOLUME) {
            uint16_t usage = dir > 0 ? HID_USAGE_CONSUMER_VOLUME_INCREMENT
                                     : HID_USAGE_CONSUMER_VOLUME_DECREMENT;
            zmk_hid_consumer_press(usage);
            zmk_endpoints_send_report(HID_USAGE_CONSUMER);
            k_sleep(K_MSEC(5));
            zmk_hid_consumer_release(usage);
            zmk_endpoints_send_report(HID_USAGE_CONSUMER);
        } else if (enc->type == TYPE_HSCROLL) {
            zmk_hid_mouse_scroll_set(dir, 0);
            zmk_endpoints_send_mouse_report();
            k_sleep(K_MSEC(5));
            zmk_hid_mouse_clear();
            zmk_endpoints_send_mouse_report();
        } else {
            zmk_hid_mouse_scroll_set(0, dir);
            zmk_endpoints_send_mouse_report();
            k_sleep(K_MSEC(5));
            zmk_hid_mouse_clear();
            zmk_endpoints_send_mouse_report();
        }
    }
}

static struct k_work_delayable ble_switch_work;
static struct k_work_delayable led_work;
static const struct device *led_dev;

static void ble_switch_handler(struct k_work *work)
{
    zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE);
}

static void led_blink_handler(struct k_work *work)
{
    if (zmk_ble_active_profile_is_connected()) {
        gpio_pin_set(led_dev, BLE_LED_PIN, 1);
        return;
    }
    static int on;
    on = !on;
    gpio_pin_set(led_dev, BLE_LED_PIN, on);
    k_work_schedule(&led_work, K_MSEC(500));
}

static void init_encoder(int idx, const struct device *a_port, int a_pin,
                         const struct device *b_port, int b_pin, uint8_t type)
{
    gpio_pin_configure(a_port, a_pin, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(b_port, b_pin, GPIO_INPUT | GPIO_PULL_UP);

    uint8_t a = gpio_pin_get(a_port, a_pin);
    uint8_t b = gpio_pin_get(b_port, b_pin);
    encoders[idx].last_state = (a << 1) | b;
    encoders[idx].position = 0;
    encoders[idx].type = type;
    k_work_init(&encoders[idx].work, send_scroll);
}

static int encoder_init(void)
{
    gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (!device_is_ready(gpio0) || !device_is_ready(gpio1))
        return -ENODEV;

    init_encoder(ENC_O, gpio1, ENC_O_A, gpio1, ENC_O_B, TYPE_SCROLL);
    init_encoder(ENC_P, gpio0, ENC_P_A, gpio0, ENC_P_B, TYPE_VOLUME);
    init_encoder(ENC_Q, gpio0, ENC_Q_A, gpio1, ENC_Q_B, TYPE_HSCROLL);

    gpio_init_callback(&cb_gpio0, isr_gpio0,
        BIT(ENC_P_A) | BIT(ENC_P_B) | BIT(ENC_Q_A));
    gpio_add_callback(gpio0, &cb_gpio0);
    gpio_pin_interrupt_configure(gpio0, ENC_P_A, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio0, ENC_P_B, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio0, ENC_Q_A, GPIO_INT_EDGE_BOTH);

    gpio_init_callback(&cb_gpio1, isr_gpio1,
        BIT(ENC_O_A) | BIT(ENC_O_B) | BIT(ENC_Q_B));
    gpio_add_callback(gpio1, &cb_gpio1);
    gpio_pin_interrupt_configure(gpio1, ENC_O_A, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio1, ENC_O_B, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio1, ENC_Q_B, GPIO_INT_EDGE_BOTH);

    k_work_init_delayable(&ble_switch_work, ble_switch_handler);
    k_work_schedule(&ble_switch_work, K_SECONDS(3));

    led_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    gpio_pin_configure(led_dev, BLE_LED_PIN, GPIO_OUTPUT);
    gpio_pin_set(led_dev, BLE_LED_PIN, 1);
    k_work_init_delayable(&led_work, led_blink_handler);
    k_work_schedule(&led_work, K_MSEC(500));

    return 0;
}

SYS_INIT(encoder_init, APPLICATION, 90);
