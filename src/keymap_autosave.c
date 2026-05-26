#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(keymap_autosave, CONFIG_ZMK_LOG_LEVEL);

static struct k_work_delayable autosave_work;
static bool first_run = true;

static void autosave_handler(struct k_work *work)
{
    if (first_run) {
        first_run = false;
        int ret = settings_load_subtree("keymap");
        if (ret < 0) {
            LOG_ERR("Failed to load keymap from settings: %d", ret);
        } else {
            LOG_INF("Keymap loaded from settings on boot");
        }
    }

    if (zmk_keymap_check_unsaved_changes()) {
        int ret = zmk_keymap_save_changes();
        if (ret < 0) {
            LOG_ERR("Failed to save keymap changes: %d", ret);
        } else {
            LOG_INF("Keymap changes saved");
        }
    }
    k_work_schedule(&autosave_work, K_SECONDS(2));
}

static int keymap_autosave_init(void)
{
    k_work_init_delayable(&autosave_work, autosave_handler);
    k_work_schedule(&autosave_work, K_SECONDS(5));
    LOG_INF("Keymap auto-save initialized");
    return 0;
}

SYS_INIT(keymap_autosave_init, APPLICATION, 80);
