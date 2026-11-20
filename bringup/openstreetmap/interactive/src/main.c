/*
 * RideCast Phase 2B: offline map pan and zoom with Zephyr 4.5 + LVGL 9.
 * Uses the Phase 2A generated image (no network or CC3351).
 */
#include <errno.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>

#include "../../static/src/osm_map_rgb888.h"

#define MAP_VIEW_W 480
#define MAP_VIEW_H 272
#define MAP_SCALE_MIN 256
#define MAP_SCALE_MAX 512
#define MAP_SCALE_STEP 64
#define MAP_SCALE_START 384

static lv_obj_t *map_image;
static lv_obj_t *zoom_label;
static int32_t map_x;
static int32_t map_y;
static uint32_t map_scale = MAP_SCALE_START;
static lv_point_t previous_touch;

/* The phase-2A header contains B, G, R bytes: LVGL's RGB888 image layout. */
static const lv_image_dsc_t map_bitmap = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB888,
        .w = OSM_MAP_WIDTH,
        .h = OSM_MAP_HEIGHT,
        .stride = OSM_MAP_WIDTH * 3,
    },
    .data_size = sizeof(osm_map_rgb888),
    .data = osm_map_rgb888,
};

static int32_t clamp_value(int32_t value, int32_t low, int32_t high);
static void set_map_position(int32_t x, int32_t y);
static void set_map_scale(uint32_t new_scale);
static void map_event_cb(lv_event_t *event);
static void zoom_in_cb(lv_event_t *event);
static void zoom_out_cb(lv_event_t *event);
static lv_obj_t *create_zoom_button(lv_obj_t *parent, const char *text,
                                    int32_t top_offset, lv_event_cb_t callback);
static void create_ui(void);

int main(void)
{
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    int ret;

    printk("\nRideCast Phase 2B: offline map pan/zoom\n");

    if (!device_is_ready(display_dev)) {
        printk("ERROR: display is not ready\n");
        return 0;
    }

    create_ui();
    lv_timer_handler();

    ret = display_blanking_off(display_dev);
    if (ret < 0 && ret != -ENOSYS) {
        printk("ERROR: display blanking off failed (%d)\n", ret);
        return 0;
    }

    printk("Touch and drag map; use + and - buttons to zoom.\n");
    while (1) {
        lv_timer_handler();
        k_sleep(K_MSEC(10));
    }

    return 0;
}

static int32_t clamp_value(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static void set_map_position(int32_t x, int32_t y)
{
    const int32_t scaled_w = (OSM_MAP_WIDTH * (int32_t)map_scale) / 256;
    const int32_t scaled_h = (OSM_MAP_HEIGHT * (int32_t)map_scale) / 256;
    const int32_t min_x = MAP_VIEW_W - scaled_w;
    const int32_t min_y = MAP_VIEW_H - scaled_h;

    map_x = clamp_value(x, MIN(min_x, 0), 0);
    map_y = clamp_value(y, MIN(min_y, 0), 0);
    lv_obj_set_pos(map_image, map_x, map_y);
}

static void set_map_scale(uint32_t new_scale)
{
    if (new_scale < MAP_SCALE_MIN || new_scale > MAP_SCALE_MAX) {
        return;
    }

    /* Keep the same source-map point approximately under the LCD centre. */
    const int32_t source_x = (MAP_VIEW_W / 2 - map_x) * 256 / (int32_t)map_scale;
    const int32_t source_y = (MAP_VIEW_H / 2 - map_y) * 256 / (int32_t)map_scale;
    const int32_t new_x = MAP_VIEW_W / 2 - source_x * (int32_t)new_scale / 256;
    const int32_t new_y = MAP_VIEW_H / 2 - source_y * (int32_t)new_scale / 256;

    map_scale = new_scale;
    lv_image_set_scale(map_image, map_scale);
    set_map_position(new_x, new_y);
    lv_label_set_text_fmt(zoom_label, "%u%%", (unsigned int)(map_scale * 100 / 256));
    printk("Zoom: %u%%\n", (unsigned int)(map_scale * 100 / 256));
}

static void map_event_cb(lv_event_t *event)
{
    lv_indev_t *input = lv_indev_active();
    lv_point_t point;

    if (input == NULL) {
        return;
    }

    lv_indev_get_point(input, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        previous_touch = point;
    } else if (lv_event_get_code(event) == LV_EVENT_PRESSING) {
        set_map_position(map_x + point.x - previous_touch.x,
                         map_y + point.y - previous_touch.y);
        previous_touch = point;
    }
}

static void zoom_in_cb(lv_event_t *event)
{
    ARG_UNUSED(event);
    if (map_scale < MAP_SCALE_MAX) {
        set_map_scale(map_scale + MAP_SCALE_STEP);
    }
}

static void zoom_out_cb(lv_event_t *event)
{
    ARG_UNUSED(event);
    if (map_scale > MAP_SCALE_MIN) {
        set_map_scale(map_scale - MAP_SCALE_STEP);
    }
}

static lv_obj_t *create_zoom_button(lv_obj_t *parent, const char *text,
                                    int32_t top_offset, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);

    lv_obj_set_size(button, 44, 40);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -8, top_offset);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *viewport = lv_obj_create(screen);
    lv_obj_t *marker;
    lv_obj_t *caption;

    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1C1C1C), 0);

    lv_obj_set_size(viewport, MAP_VIEW_W, MAP_VIEW_H);
    lv_obj_set_pos(viewport, 0, 0);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);
    lv_obj_set_style_radius(viewport, 0, 0);
    lv_obj_set_scrollable(viewport, false);
    lv_obj_set_clickable(viewport, true);
    lv_obj_add_event_cb(viewport, map_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(viewport, map_event_cb, LV_EVENT_PRESSING, NULL);

    map_image = lv_image_create(viewport);
    lv_image_set_src(map_image, &map_bitmap);
    lv_image_set_pivot(map_image, 0, 0);
    lv_image_set_antialias(map_image, false);
    lv_obj_set_clickable(map_image, false);
    lv_obj_set_scrollable(map_image, false);
    lv_image_set_scale(map_image, map_scale);
    set_map_position((MAP_VIEW_W - OSM_MAP_WIDTH * (int32_t)map_scale / 256) / 2,
                     (MAP_VIEW_H - OSM_MAP_HEIGHT * (int32_t)map_scale / 256) / 2);

    create_zoom_button(screen, "+", 12, zoom_in_cb);
    create_zoom_button(screen, "-", 62, zoom_out_cb);

    zoom_label = lv_label_create(screen);
    lv_obj_align(zoom_label, LV_ALIGN_TOP_RIGHT, -11, 108);
    lv_obj_set_style_text_color(zoom_label, lv_color_white(), 0);
    lv_label_set_text_fmt(zoom_label, "%u%%", (unsigned int)(map_scale * 100 / 256));

    marker = lv_obj_create(screen);
    lv_obj_set_size(marker, 20, 20);
    lv_obj_center(marker);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(marker, 0, 0);
    lv_obj_set_style_bg_color(marker, lv_color_hex(0xE63442), 0);
    lv_obj_set_style_border_color(marker, lv_color_white(), 0);
    lv_obj_set_style_border_width(marker, 3, 0);
    lv_obj_set_clickable(marker, false);
    lv_obj_set_scrollable(marker, false);

    caption = lv_label_create(screen);
    lv_label_set_text(caption, "(c) OpenStreetMap contributors | Offline demo");
    lv_obj_align(caption, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_set_style_text_color(caption, lv_color_black(), 0);
    lv_obj_set_style_bg_color(caption, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(caption, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(caption, 3, 0);
}
