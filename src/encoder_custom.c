#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/keys.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(encoder_custom, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT custom_ec11_encoder

struct encoder_custom_config {
    struct gpio_dt_spec a_gpio;
    struct gpio_dt_spec b_gpio;
    uint32_t steps;
};

struct encoder_custom_data {
    const struct device *dev;
    struct gpio_callback gpio_cb;
    struct k_work_delayable work;
    int32_t counter;
    uint8_t prev_state;
};

static const int8_t quad_table[16] = {
     0,  1, -1,  0,
    -1,  0,  0,  1,
     1,  0,  0, -1,
     0, -1,  1,  0,
};

static void encoder_custom_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct encoder_custom_data *data = CONTAINER_OF(dwork, struct encoder_custom_data, work);
    const struct encoder_custom_config *cfg = data->dev->config;

    int32_t threshold = cfg->steps / 4;

    while (data->counter >= threshold) {
        zmk_hid_consumer_press(C_VOL_UP);
        zmk_endpoints_send_report(HID_USAGE_CONSUMER);
        zmk_hid_consumer_release(C_VOL_UP);
        zmk_endpoints_send_report(HID_USAGE_CONSUMER);
        data->counter -= threshold;
    }

    while (data->counter <= -threshold) {
        zmk_hid_consumer_press(C_VOL_DN);
        zmk_endpoints_send_report(HID_USAGE_CONSUMER);
        zmk_hid_consumer_release(C_VOL_DN);
        zmk_endpoints_send_report(HID_USAGE_CONSUMER);
        data->counter += threshold;
    }
}

static void encoder_custom_isr(const struct device *port, struct gpio_callback *cb,
                                gpio_port_pins_t pins) {
    struct encoder_custom_data *data = CONTAINER_OF(cb, struct encoder_custom_data, gpio_cb);
    const struct encoder_custom_config *cfg = data->dev->config;

    uint8_t a = gpio_pin_get_dt(&cfg->a_gpio);
    uint8_t b = gpio_pin_get_dt(&cfg->b_gpio);
    uint8_t state = (a << 1) | b;
    uint8_t index = (data->prev_state << 2) | state;
    int8_t dir = quad_table[index];

    data->prev_state = state;

    if (dir > 0) {
        data->counter++;
    } else if (dir < 0) {
        data->counter--;
    }

    int32_t threshold = cfg->steps / 4;

    if (data->counter >= threshold || data->counter <= -threshold) {
        k_work_reschedule(&data->work, K_NO_WAIT);
    }
}

static int encoder_custom_init(const struct device *dev) {
    struct encoder_custom_data *data = dev->data;
    const struct encoder_custom_config *cfg = dev->config;

    data->dev = dev;

    if (!device_is_ready(cfg->a_gpio.port) || !device_is_ready(cfg->b_gpio.port)) {
        LOG_ERR("GPIO port not ready");
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&cfg->a_gpio, GPIO_INPUT | cfg->a_gpio.dt_flags);
    if (err) {
        LOG_ERR("Failed to config A pin: %d", err);
        return err;
    }

    err = gpio_pin_configure_dt(&cfg->b_gpio, GPIO_INPUT | cfg->b_gpio.dt_flags);
    if (err) {
        LOG_ERR("Failed to config B pin: %d", err);
        return err;
    }

    data->prev_state = (gpio_pin_get_dt(&cfg->a_gpio) << 1) |
                        gpio_pin_get_dt(&cfg->b_gpio);
    data->counter = 0;

    k_work_init_delayable(&data->work, encoder_custom_work_handler);

    gpio_init_callback(&data->gpio_cb, encoder_custom_isr,
                       BIT(cfg->a_gpio.pin) | BIT(cfg->b_gpio.pin));

    err = gpio_add_callback(cfg->a_gpio.port, &data->gpio_cb);
    if (err) {
        LOG_ERR("Failed to add callback: %d", err);
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&cfg->a_gpio, GPIO_INT_EDGE_BOTH);
    if (err) {
        LOG_ERR("Failed to config A IRQ: %d", err);
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&cfg->b_gpio, GPIO_INT_EDGE_BOTH);
    if (err) {
        LOG_ERR("Failed to config B IRQ: %d", err);
        return err;
    }

    LOG_INF("Custom EC11 encoder ready");
    return 0;
}

#define ENCODER_CUSTOM_INIT(n) \
    static struct encoder_custom_config encoder_custom_config_##n = { \
        .a_gpio = GPIO_DT_SPEC_INST_GET(n, a_gpios), \
        .b_gpio = GPIO_DT_SPEC_INST_GET(n, b_gpios), \
        .steps = DT_INST_PROP(n, steps), \
    }; \
    static struct encoder_custom_data encoder_custom_data_##n; \
    DEVICE_DT_INST_DEFINE(n, encoder_custom_init, NULL, \
                          &encoder_custom_data_##n, &encoder_custom_config_##n, \
                          POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(ENCODER_CUSTOM_INIT)
