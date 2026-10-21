#include "app_keyboard_rotation.h"
#include "app_keyboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned applies;
static bool save_ok;

static bool apply(void)
{
    applies++;
    return save_ok;
}

static lv_obj_t *find_button(lv_obj_t *root, const char *text)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *child = lv_obj_get_child(root, i);
        if (lv_obj_check_type(child, &lv_label_class) &&
            strcmp(lv_label_get_text(child), text) == 0)
            return lv_obj_get_parent(child);
        lv_obj_t *found = find_button(child, text);
        if (found) return found;
    }
    return NULL;
}

static void click(const char *text)
{
    lv_obj_t *button = find_button(lv_layer_top(), text);
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
}

int main(void)
{
    lv_init();
    lv_display_create(720, 1280);
    app_keyboard_rotation_init(apply, NULL);
    assert(!app_keyboard_rotation_update(false, true, true));
    /* Boot/hotplug while asleep, locked or showing the splash is deferred. */
    app_keyboard_set_connected(true);
    assert(!app_keyboard_rotation_update(true, false, true));
    assert(!find_button(lv_layer_top(), "Yes"));
    assert(app_keyboard_rotation_update(true, true, true));
    assert(find_button(lv_layer_top(), "Yes"));
    assert(!app_keyboard_rotation_update(true, true, true));
    click("No");
    assert(!find_button(lv_layer_top(), "Yes"));
    assert(applies == 0);
    assert(!app_keyboard_rotation_update(true, true, true));

    /* Reconnect offers again; Esc is equivalent to No. */
    app_keyboard_rotation_update(false, true, true);
    assert(app_keyboard_rotation_update(true, true, true));
    lv_obj_update_layout(lv_layer_top());
    const tab5_key_t escape = {TAB5_KEY_ESCAPE, 0};
    assert(app_keyboard_input(&escape));
    assert(!find_button(lv_layer_top(), "Yes"));
    assert(applies == 0);
    assert(!app_keyboard_rotation_update(true, true, true));

    /* At 90 degrees there is no question, including on startup. */
    app_keyboard_rotation_update(false, true, false);
    assert(!app_keyboard_rotation_update(true, true, false));
    assert(!find_button(lv_layer_top(), "Yes"));

    /* Disconnect dismisses an open popup without applying anything. */
    app_keyboard_rotation_update(false, true, true);
    assert(app_keyboard_rotation_update(true, true, true));
    app_keyboard_rotation_update(false, true, true);
    assert(!find_button(lv_layer_top(), "Yes"));
    assert(applies == 0);

    /* Save failure keeps the dialog open; retry can succeed. */
    assert(app_keyboard_rotation_update(true, true, true));
    click("Yes");
    assert(applies == 1);
    assert(find_button(lv_layer_top(), "Yes"));
    save_ok = true;
    click("Yes");
    assert(applies == 2);
    assert(!find_button(lv_layer_top(), "Yes"));
    assert(!app_keyboard_rotation_update(true, true, true));

    /* An already displayed prompt stays inaccessible while the screen locks. */
    app_keyboard_rotation_update(false, true, true);
    assert(app_keyboard_rotation_update(true, true, true));
    assert(!app_keyboard_rotation_update(true, false, true));
    assert(!lv_obj_is_visible(find_button(lv_layer_top(), "Yes")));
    assert(app_keyboard_rotation_update(true, true, true));
    lv_obj_update_layout(lv_layer_top());
    assert(lv_obj_is_visible(find_button(lv_layer_top(), "Yes")));
    click("No");

    /* Existing keyboard activation defaults to No, then arrows can pick Yes. */
    app_keyboard_rotation_update(false, true, true);
    assert(app_keyboard_rotation_update(true, true, true));
    const tab5_key_t enter = {TAB5_KEY_ENTER, 0};
    assert(app_keyboard_input(&enter));
    assert(!find_button(lv_layer_top(), "Yes"));
    assert(applies == 2);
    app_keyboard_rotation_update(false, true, true);
    assert(app_keyboard_rotation_update(true, true, true));
    const tab5_key_t right = {TAB5_KEY_RIGHT, 0};
    app_keyboard_input(&right); /* Establish selection on No. */
    app_keyboard_input(&right); /* Move to Yes. */
    assert(app_keyboard_input(&enter));
    assert(applies == 3);
    assert(!find_button(lv_layer_top(), "Yes"));
    lv_deinit();
    puts("PASS: keyboard rotation prompt lifecycle and confirmation");
    return 0;
}
