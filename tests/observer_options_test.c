#include <assert.h>
#include <stdio.h>
#include "../main/observer_options.h"
int main(void) {
    observer_view_prefs_t p, copy;
    observer_prefs_defaults(&p);
    assert(observer_prefs_valid(&p));
    assert(p.ap_order[5] == OBS_FIELD_BSSID);
    assert(!p.ap_visible[OBS_FIELD_AGE]);
    assert(p.client_visible[OBS_CLIENT_FIELD_AGE]);
    copy = p;
    assert(!observer_prefs_move(p.ap_order, OBS_FIELD_COUNT, 0, -1));
    assert(observer_prefs_equal(&p, &copy));
    assert(!observer_prefs_move(p.ap_order, OBS_FIELD_COUNT, OBS_FIELD_COUNT - 1, 1));
    assert(!observer_prefs_move(p.ap_order, OBS_FIELD_COUNT, OBS_FIELD_COUNT, -1));
    assert(!observer_prefs_move(p.ap_order, OBS_FIELD_COUNT, 0, 0));
    assert(observer_prefs_move(p.ap_order, OBS_FIELD_COUNT, 0, 1));
    assert(p.ap_order[0] == OBS_FIELD_PMF);
    assert(p.ap_visible[OBS_FIELD_SECURITY]);
    assert(!observer_prefs_equal(&p, &copy));
    assert(observer_prefs_decode(&p, sizeof(p), &copy));
    assert(observer_prefs_equal(&p, &copy));
    assert(observer_prefs_move(p.ap_order, OBS_FIELD_COUNT, 1, -1));
    observer_prefs_defaults(&copy);
    assert(observer_prefs_equal(&p, &copy));
    p.ap_order[0] = p.ap_order[1];
    assert(!observer_prefs_valid(&p));
    assert(!observer_prefs_decode(&p, sizeof(p), &copy));
    assert(observer_prefs_valid(&copy));
    p = copy; p.version++;
    assert(!observer_prefs_decode(&p, sizeof(p), &copy));
    p = copy; p.client_visible[0] = 2;
    assert(!observer_prefs_valid(&p));
    assert(!observer_prefs_decode(&p, sizeof(p)-1, &copy));
    p = copy; p.ap_order[0] = 255;
    assert(!observer_prefs_valid(&p));
    puts("observer options tests passed");
}
