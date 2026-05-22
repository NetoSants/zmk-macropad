#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include "encoder_custom.h"

static const struct device *gpio_dev;
static struct gpio_callback enc_cb;

static int8_t enc_table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
static uint8_t enc_state = 0;

static void encoder_work_handler(struct k_work *work);
K_WORK_DEFINE(encoder_work, encoder_work_handler);

static void encoder_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_work_submit(&encoder_work);
}

static void encoder_work_handler(struct k_work *work)
{
	uint8_t a = gpio_pin_get(gpio_dev, ENC_A_PIN);
	uint8_t b = gpio_pin_get(gpio_dev, ENC_B_PIN);
	enc_state = ((enc_state << 2) | (a << 1) | b) & 0x0F;

	int8_t dir = enc_table[enc_state];
	if (dir != 0) {
		uint32_t usage = (dir > 0)
			? (HID_USAGE_KEY << 16) | HID_USAGE_KEY_KEYBOARD_UPARROW
			: (HID_USAGE_KEY << 16) | HID_USAGE_KEY_KEYBOARD_DOWNARROW;

		zmk_hid_press(usage);
		zmk_endpoints_send_report(HID_USAGE_KEY);
		k_sleep(K_MSEC(10));
		zmk_hid_release(usage);
		zmk_endpoints_send_report(HID_USAGE_KEY);
	}
}

static int encoder_init(void)
{
	gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
	if (!device_is_ready(gpio_dev)) {
		printk("encoder: gpio1 not ready\n");
		return -ENODEV;
	}

	gpio_pin_configure(gpio_dev, ENC_A_PIN, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_configure(gpio_dev, ENC_B_PIN, GPIO_INPUT | GPIO_PULL_UP);

	gpio_init_callback(&enc_cb, encoder_isr, BIT(ENC_A_PIN) | BIT(ENC_B_PIN));
	gpio_add_callback(gpio_dev, &enc_cb);

	gpio_pin_interrupt_configure(gpio_dev, ENC_A_PIN, GPIO_INT_EDGE_BOTH);
	gpio_pin_interrupt_configure(gpio_dev, ENC_B_PIN, GPIO_INT_EDGE_BOTH);

	return 0;
}

SYS_INIT(encoder_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
