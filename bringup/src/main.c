
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define LED_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

int main(void)
{
    int ret;
    uint32_t counter = 0U;

    printk("\n");
    printk("============================\n");
    printk("  STM32H7B3 Bring-up Test\n");
    printk("  Zephyr RTOS\n");
    printk("============================\n");

    if(!gpio_is_ready_dt(&led))
    {
        printk("ERROR: LED GPIO not ready\n");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    if(ret < 0)
    {
        printk("ERROR: LED configuration failed\n");
        return 0;
    }

    printk("Initialization complete\n");

    while(1)
    {
        ret = gpio_pin_toggle_dt(&led);

        if(ret < 0)
        {
            printk("ERROR: LED toggle failed: %d\n", ret);
        }

        printk("STM32 alive: %u\n", counter++);

        k_sleep(K_SECONDS(1));
    }

    return 0;
}
