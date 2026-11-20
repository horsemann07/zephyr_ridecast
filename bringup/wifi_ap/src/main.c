/*
 * RideCast Phase 2C — STM32H7B3I-DK onboard Wi-Fi SoftAP / TCP echo / LCD.
 *
 * Important: The standard Zephyr 4.5 board devicetree does not define the
 * onboard Inventek ISM43340. This code intentionally reports that limitation
 * instead of falsely displaying an active AP. Bring up the Inventek driver
 * and its hardware overlay before testing with a phone.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>

#define DEMO_SSID         "RideCast-DK"
#define DEMO_PASSWORD     "RideCast1234" /* Demo-only WPA2 password */
#define DEMO_IP           "192.168.4.1"  /* Configured only if driver starts */
#define DEMO_PORT         4242
#define DEMO_TCP_STACK    4096
#define DEMO_TCP_PRIORITY 8

static lv_obj_t* status_label;
static lv_obj_t* count_label;
static lv_obj_t* message_label;
static struct net_mgmt_event_callback wifi_callback;

static struct
{
    char status[64];
    char last_message[64];
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} demo_state = {
    .status       = "Starting...",
    .last_message = "No messages yet",
};

K_MUTEX_DEFINE(demo_lock);
K_SEM_DEFINE(ui_ready, 0, 1);

static void network_thread(void* arg1, void* arg2, void* arg3);
static void create_ui(void);
static void set_status(const char* status);
static void set_message(const char* message, size_t len);
static void add_traffic(size_t rx, size_t tx);
static void update_ui(void);
static void wifi_event_handler(struct net_mgmt_event_callback* cb,
                               uint64_t event,
                               struct net_if* iface);
static int start_soft_ap(struct net_if* iface);
static void tcp_echo_server(void);

static void wifi_diag_cb(struct net_if* iface, void* user_data);

K_THREAD_DEFINE(network_tid, DEMO_TCP_STACK, network_thread, NULL, NULL, NULL, DEMO_TCP_PRIORITY, 0, 0);

int main(void)
{
    const struct device* display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    int rc;

    printk("\nRideCast Phase 2C: onboard Wi-Fi AP demo\n");
    if(!device_is_ready(display))
    {
        printk("Display not ready\n");
        return 0;
    }

    create_ui();
    lv_timer_handler();
    rc = display_blanking_off(display);
    if(rc != 0 && rc != -ENOSYS)
    {
        printk("Cannot enable LCD: %d\n", rc);
        return 0;
    }
    k_sem_give(&ui_ready);

    /* Only the main thread calls LVGL APIs. */
    while(1)
    {
        update_ui();
        lv_timer_handler();
        k_sleep(K_MSEC(100));
    }
    return 0;
}

static void create_ui(void)
{
    lv_obj_t* screen = lv_screen_active();
    lv_obj_t* title  = lv_label_create(screen);
    lv_obj_t* wifi_info;
    lv_obj_t* hint           = lv_label_create(screen);
    lv_obj_t* settings_panel = lv_obj_create(screen);
    lv_obj_t* traffic_panel  = lv_obj_create(screen);

    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0E1B2B), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xEFF5FF), 0);

    /* Two compact LCD cards: wireless settings and TCP message counters. */
    lv_obj_set_pos(settings_panel, 10, 69);
    lv_obj_set_size(settings_panel, 460, 94);
    lv_obj_set_scrollable(settings_panel, false);
    lv_obj_set_style_radius(settings_panel, 10, 0);
    lv_obj_set_style_border_width(settings_panel, 0, 0);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(0x20354C), 0);
    lv_obj_set_style_pad_all(settings_panel, 8, 0);

    lv_obj_set_pos(traffic_panel, 10, 169);
    lv_obj_set_size(traffic_panel, 460, 75);
    lv_obj_set_scrollable(traffic_panel, false);
    lv_obj_set_style_radius(traffic_panel, 10, 0);
    lv_obj_set_style_border_width(traffic_panel, 0, 0);
    lv_obj_set_style_bg_color(traffic_panel, lv_color_hex(0x20354C), 0);
    lv_obj_set_style_pad_all(traffic_panel, 8, 0);

    lv_label_set_text(title, "RideCast | Wi-Fi AP Test");
    lv_obj_set_pos(title, 14, 12);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8CC7FF), 0);

    status_label = lv_label_create(screen);
    lv_obj_set_pos(status_label, 14, 42);
    lv_label_set_text(status_label, "Status: Starting...");

    wifi_info = lv_label_create(settings_panel);
    lv_label_set_text_fmt(wifi_info, "SSID: %s\nPassword: %s\nIP (planned): %s    TCP: %d",
                          DEMO_SSID, DEMO_PASSWORD, DEMO_IP, DEMO_PORT);
    lv_obj_set_pos(wifi_info, 5, 4);
    lv_obj_set_style_text_color(wifi_info, lv_color_hex(0xD6E7FA), 0);

    count_label = lv_label_create(traffic_panel);
    lv_obj_set_pos(count_label, 5, 0);
    lv_label_set_text(count_label, "RX: 0 bytes    TX: 0 bytes");

    message_label = lv_label_create(traffic_panel);
    lv_obj_set_pos(message_label, 5, 29);
    lv_obj_set_size(message_label, 430, 24);
    lv_label_set_text(message_label, "Last RX: -");

    lv_label_set_text(hint, "Android: Serial WiFi Terminal -> RAW TCP");
    lv_obj_set_pos(hint, 14, 251);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8CA7C6), 0);
}

static void set_status(const char* status)
{
    k_mutex_lock(&demo_lock, K_FOREVER);
    strncpy(demo_state.status, status, sizeof(demo_state.status) - 1);
    demo_state.status[sizeof(demo_state.status) - 1] = '\0';
    k_mutex_unlock(&demo_lock);
    printk("Wi-Fi: %s\n", status);
}

static void set_message(const char* message, size_t len)
{
    size_t count = MIN(len, sizeof(demo_state.last_message) - 1);

    k_mutex_lock(&demo_lock, K_FOREVER);
    memcpy(demo_state.last_message, message, count);
    demo_state.last_message[count] = '\0';
    k_mutex_unlock(&demo_lock);
}

static void add_traffic(size_t rx, size_t tx)
{
    k_mutex_lock(&demo_lock, K_FOREVER);
    demo_state.rx_bytes += rx;
    demo_state.tx_bytes += tx;
    k_mutex_unlock(&demo_lock);
}

static void update_ui(void)
{
    char status[64];
    char last_message[64];
    uint32_t rx, tx;

    k_mutex_lock(&demo_lock, K_FOREVER);
    strcpy(status, demo_state.status);
    strcpy(last_message, demo_state.last_message);
    rx = demo_state.rx_bytes;
    tx = demo_state.tx_bytes;
    k_mutex_unlock(&demo_lock);

    lv_label_set_text_fmt(status_label, "Status: %s", status);
    lv_label_set_text_fmt(count_label, "RX: %u bytes    TX: %u bytes",
                          (unsigned)rx, (unsigned)tx);
    lv_label_set_text_fmt(message_label, "Last RX: %s", last_message);
}

static void
wifi_event_handler(struct net_mgmt_event_callback* cb, uint64_t event, struct net_if* iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);
    if(event == NET_EVENT_WIFI_AP_STA_CONNECTED)
    {
        set_status("Phone joined AP");
    }
    else if(event == NET_EVENT_WIFI_AP_STA_DISCONNECTED)
    {
        set_status("Phone left AP");
    }
}

static int start_soft_ap(struct net_if* iface)
{
    struct net_in_addr ap_addr, netmask;
    static struct wifi_connect_req_params params = {
        .ssid        = (const uint8_t*)DEMO_SSID,
        .ssid_length = sizeof(DEMO_SSID) - 1,
        .psk         = (const uint8_t*)DEMO_PASSWORD,
        .psk_length  = sizeof(DEMO_PASSWORD) - 1,
        .security    = WIFI_SECURITY_TYPE_PSK,
        .band        = WIFI_FREQ_BAND_2_4_GHZ,
        .channel     = 6,
    };
    int rc;

    if(net_addr_pton(AF_INET, DEMO_IP, &ap_addr) != 0 ||
       net_addr_pton(AF_INET, "255.255.255.0", &netmask) != 0)
    {
        return -EINVAL;
    }
    if(net_if_ipv4_addr_add(iface, &ap_addr, NET_ADDR_MANUAL, 0) == NULL)
    {
        return -EADDRNOTAVAIL;
    }
    (void)net_if_ipv4_set_netmask_by_addr(iface, &ap_addr, &netmask);

    /* Inventek eS-WiFi firmware has its own IP/socket handling. Do not start
     * a second DHCP server without checking the specific module firmware. */
    rc = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &params, sizeof(params));
    return rc;
}

static void network_thread(void* arg1, void* arg2, void* arg3)
{
    struct net_if* wifi_iface;
    int rc;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    k_sem_take(&ui_ready, K_FOREVER);
    k_sleep(K_MSEC(1000));


    const struct device* wifi_dev = DEVICE_DT_GET(DT_NODELABEL(wifi0));

    printk("Wi-Fi device: %s\n", wifi_dev->name);
    printk("Wi-Fi device ready: %d\n", device_is_ready(wifi_dev));

    printk("Registered network interfaces:\n");
    net_if_foreach(wifi_diag_cb, NULL);


    const struct gpio_dt_spec wifi_reset =
    GPIO_DT_SPEC_GET(DT_NODELABEL(wifi0), resetn_gpios);

    const struct gpio_dt_spec wifi_wakeup =
    GPIO_DT_SPEC_GET(DT_NODELABEL(wifi0), wakeup_gpios);

    const struct gpio_dt_spec wifi_data_ready =
    GPIO_DT_SPEC_GET(DT_NODELABEL(wifi0), data_gpios);

    printk("Wi-Fi GPIO levels:\n");

    printk("  RESET PI1 = %d\n", gpio_pin_get_raw(wifi_reset.port, wifi_reset.pin));

    printk("  WAKE  PI2 = %d\n", gpio_pin_get_raw(wifi_wakeup.port, wifi_wakeup.pin));

    printk("  READY PI5 = %d\n",
           gpio_pin_get_raw(wifi_data_ready.port, wifi_data_ready.pin));


    wifi_iface = net_if_get_first_wifi();
    if(wifi_iface == NULL)
    {
        set_status("No onboard Wi-Fi driver");
        printk("Board Wi-Fi DT/driver not configured; see README.md\n");
        return;
    }

    net_mgmt_init_event_callback(&wifi_callback, wifi_event_handler,
                                 NET_EVENT_WIFI_AP_STA_CONNECTED |
                                 NET_EVENT_WIFI_AP_STA_DISCONNECTED);
    net_mgmt_add_event_callback(&wifi_callback);

    rc = start_soft_ap(wifi_iface);
    if(rc != 0)
    {
        set_status("AP start failed (see UART)");
        printk("AP start returned: %d\n", rc);
        return;
    }
    set_status("AP ready - join from phone");
    tcp_echo_server();
}

static void tcp_echo_server(void)
{
    struct sockaddr_in bind_addr = { 0 };
    int listener;

    listener = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(listener < 0)
    {
        set_status("TCP socket failed");
        return;
    }
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(DEMO_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(zsock_bind(listener, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0 ||
       zsock_listen(listener, 1) < 0)
    {
        set_status("TCP listen failed");
        zsock_close(listener);
        return;
    }

    for(;;)
    {
        int client;
        char buffer[128];
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);

        set_status("AP ready / TCP listening");
        client = zsock_accept(listener, (struct sockaddr*)&peer, &peer_len);
        if(client < 0)
        {
            k_sleep(K_MSEC(300));
            continue;
        }
        set_status("Phone TCP connected");
        for(;;)
        {
            ssize_t received = zsock_recv(client, buffer, sizeof(buffer) - 1, 0);
            if(received <= 0)
            {
                break;
            }
            buffer[received] = '\0';
            set_message(buffer, (size_t)received);
            add_traffic((size_t)received, 0);
            printk("TCP RX: %s\n", buffer);

            /* Echo exactly the received text back to the phone. */
            size_t sent_total = 0;
            while(sent_total < (size_t)received)
            {
                ssize_t sent = zsock_send(client, buffer + sent_total,
                                          (size_t)received - sent_total, 0);
                if(sent <= 0)
                {
                    break;
                }
                sent_total += (size_t)sent;
                add_traffic(0, (size_t)sent);
            }
            if(sent_total < (size_t)received)
            {
                break;
            }
        }
        zsock_close(client);
        set_status("TCP disconnected / AP active");
    }
}


static void wifi_diag_cb(struct net_if* iface, void* user_data)
{
    const struct device* dev = net_if_get_device(iface);

    ARG_UNUSED(user_data);

    printk("IF index=%d, device=%s, ready=%d, wifi=%d, offloaded=%d\n",
           net_if_get_by_iface(iface), dev ? dev->name : "NULL",
           device_is_ready(dev), net_if_is_wifi(iface), net_if_is_offloaded(iface));
}
