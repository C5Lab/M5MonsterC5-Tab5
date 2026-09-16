"""Compile and exercise the production area transform (host cc, no hardware).

Run in WSL: python3 tests/run_rotation_geometry_tests.py
Does not validate PPA pixel output, timing, touch hardware or widget layout.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c').read_text()
start = source.index('void lvgl_port_rotate_area(')
end = source.index('\n}', start) + 2
production = source[start:end]
fixture = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef enum { LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90,
 LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270 } lv_display_rotation_t;
typedef struct { int x1,y1,x2,y2; } lv_area_t;
typedef struct { int rotation; } lv_display_t;
static int lv_area_get_width(lv_area_t *a) { return a->x2-a->x1+1; }
static int lv_area_get_height(lv_area_t *a) { return a->y2-a->y1+1; }
static int lv_display_get_rotation(lv_display_t *d) { return d->rotation; }
static int lv_display_get_horizontal_resolution(lv_display_t *d) { return d->rotation%2 ? 1280:720; }
static int lv_display_get_vertical_resolution(lv_display_t *d) { return d->rotation%2 ? 720:1280; }
/* PRODUCTION */
int main(void) {
 unsigned count=0;
 for (int r=0;r<4;r++) {
  lv_display_t d={r}; int w=lv_display_get_horizontal_resolution(&d),h=lv_display_get_vertical_resolution(&d);
  /* Every pixel, including all four edges: inverse must recover its logical position. */
  for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
   lv_area_t a={x,y,x,y}; lvgl_port_rotate_area(&d,&a);
   assert(a.x1==a.x2 && a.y1==a.y2);
   assert(a.x1>=0 && a.x1<720 && a.y1>=0 && a.y1<1280);
   int xx=a.x1,yy=a.y1;
   switch(r) {
    case 1: xx=1279-a.y1; yy=a.x1; break;
    case 2: xx=719-a.x1; yy=1279-a.y1; break;
    case 3: xx=a.y1; yy=719-a.x1; break;
   }
   assert(xx==x && yy==y); count++;
  }
  /* Non-square blocks, full screen, and blocks touching the lower/right edge. */
  lv_area_t cases[]={{0,0,w-1,h-1},{7,11,109,37},{w-103,h-27,w-1,h-1},{0,0,0,h-1}};
  for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
   lv_area_t a=cases[i], b=a; lvgl_port_rotate_area(&d,&b);
   int aw=lv_area_get_width(&a),ah=lv_area_get_height(&a);
   assert(lv_area_get_width(&b)==(r%2?ah:aw));
   assert(lv_area_get_height(&b)==(r%2?aw:ah));
   assert(b.x1>=0 && b.y1>=0 && b.x2<720 && b.y2<1280);
   for(int cy=0;cy<2;cy++) for(int cx=0;cx<2;cx++) {
    int x=cx?a.x2:a.x1,y=cy?a.y2:a.y1;
    lv_area_t p={x,y,x,y}; lvgl_port_rotate_area(&d,&p);
    assert((p.x1==b.x1 || p.x1==b.x2) && (p.y1==b.y1 || p.y1==b.y2));
   }
  }
 }
 printf("PASS: %u pixel round trips and 16 rectangle cases, rotations 0/90/180/270\n",count);
}
'''
with tempfile.TemporaryDirectory(prefix='tab5-rotation-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(fixture.replace('/* PRODUCTION */', production))
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-fsanitize=address,undefined', str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
