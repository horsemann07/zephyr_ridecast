/*
 * RideCast phase-2 bring-up: show a static OpenStreetMap export on STM32 LCD.
 * This is an offline display test, not a live tile/network client.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>

/* Created with: python tools/prepare_map.py map.png */
#include "osm_map_rgb888.h"

#define MAP_DISPLAY_NODE DT_CHOSEN(zephyr_display)

static const struct device *const map_display = DEVICE_DT_GET(MAP_DISPLAY_NODE);

int main(void)
{
    struct display_capabilities caps;
    const struct display_buffer_descriptor desc = {
        .buf_size = sizeof(osm_map_rgb888),
        .width = OSM_MAP_WIDTH,
        .height = OSM_MAP_HEIGHT,
        .pitch = OSM_MAP_WIDTH,
    };
    int ret;

    printk("\nRideCast Phase 2 - OpenStreetMap static preview\n");

    if (!device_is_ready(map_display)) {
        printk("ERROR: LCD display device not ready\n");
        return 0;
    }

    display_get_capabilities(map_display, &caps);

    printk("LCD: %u x %u, pixel format 0x%x\n",
           caps.x_resolution, caps.y_resolution,
           (unsigned int)caps.current_pixel_format);

    if (caps.x_resolution != OSM_MAP_WIDTH ||
        caps.y_resolution != OSM_MAP_HEIGHT) {
        printk("ERROR: map size does not match LCD size\n");
        return 0;
    }

    /* Buffer stores B, G, R bytes for Zephyr's RGB_888 framebuffer. */
    if (caps.current_pixel_format != PIXEL_FORMAT_RGB_888) {
        printk("ERROR: expected RGB888. Check CONFIG_STM32_LTDC_RGB888\n");
        return 0;
    }

    ret = display_write(map_display, 0, 0, &desc, osm_map_rgb888);
    if (ret < 0) {
        printk("ERROR: display_write failed (%d)\n", ret);
        return 0;
    }

    ret = display_blanking_off(map_display);
    if (ret < 0) {
        printk("WARNING: display_blanking_off returned %d\n", ret);
    }

    printk("Map displayed. This is OFFLINE ONLY; no Wi-Fi/network tested.\n");

    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
