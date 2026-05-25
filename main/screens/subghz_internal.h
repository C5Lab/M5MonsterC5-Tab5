#pragma once

#include "subghz_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Header (back button + title), parented to the page. */
lv_obj_t *subghz_create_header(lv_obj_t *parent, const char *title,
                               lv_color_t title_color, lv_event_cb_t on_back);

/* Style a popup as a card with a coloured border + shadow.  */
void subghz_style_popup_card(lv_obj_t *popup, lv_coord_t radius, lv_color_t accent);

/* Common back handler used by Listen/Transmit/Manage/Jammer/Tesla -> back to menu. */
void subghz_on_back_to_menu(lv_event_t *e);

#ifdef __cplusplus
}
#endif
