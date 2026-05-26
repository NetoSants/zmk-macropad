#include <zephyr/kernel.h>
#include <zmk/keymap.h>

static struct k_work_delayable autosave_work;

static void autosave_handler(struct k_work *work)
{
    if (zmk_keymap_check_unsaved_changes()) {
        zmk_keymap_save_changes();
    }
    k_work_schedule(&autosave_work, K_SECONDS(2));
}

static int keymap_autosave_init(void)
{
    k_work_init_delayable(&autosave_work, autosave_handler);
    k_work_schedule(&autosave_work, K_SECONDS(5));
    return 0;
}

SYS_INIT(keymap_autosave_init, APPLICATION, 80);
