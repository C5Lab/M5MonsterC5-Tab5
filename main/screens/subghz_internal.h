#pragma once

#include "subghz_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Header (back button + title), parented to the page. */
lv_obj_t *subghz_create_header(lv_obj_t *parent, const char *title,
                               lv_color_t title_color, lv_event_cb_t on_back);

/* Append a small action button (icon-only) to a header built by
 * subghz_create_header. Returns the button. Convenience for settings-gear
 * icons on Listen / Hunter / Scanner / Weather. */
lv_obj_t *subghz_add_header_action(lv_obj_t *header, const char *symbol,
                                   lv_event_cb_t cb, void *user_data);

/* Style a popup as a card with a coloured border + shadow.  */
void subghz_style_popup_card(lv_obj_t *popup, lv_coord_t radius, lv_color_t accent);

/* Common back handler used by Listen/Transmit/Manage/Jammer/Tesla -> back to menu. */
void subghz_on_back_to_menu(lv_event_t *e);

/* Touch-friendly modal text input popup (Save/Cancel + on-screen keyboard).
 * Replacement for CoreS3's CardKB-driven ui_show_text_input_popup. Used by
 * SD Signals manage screen for rename. */
typedef void (*subghz_text_input_confirm_cb_t)(const char *text, void *user_data);
typedef void (*subghz_text_input_cancel_cb_t)(void *user_data);

void subghz_show_text_input_popup(const char *title,
                                  const char *initial,
                                  uint32_t max_len,
                                  lv_color_t accent,
                                  subghz_text_input_confirm_cb_t on_confirm,
                                  subghz_text_input_cancel_cb_t on_cancel,
                                  void *user_data);

#ifdef __cplusplus
}
#endif
