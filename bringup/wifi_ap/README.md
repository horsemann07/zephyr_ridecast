# Phase 2C — STM32H7B3I-DK onboard Wi-Fi SoftAP with LVGL

**Platform:** Windows PowerShell, Zephyr 4.5, STM32H7B3I-DK, onboard Inventek ISM43340-series module. **No TI CC3351 is needed.**

## Important: driver enablement required first

The standard Zephyr 4.5 `stm32h7b3i_dk.dts` does not declare the onboard Inventek Wi-Fi device, though SPI2 is enabled. The generic Zephyr `eswifi` driver includes `NET_REQUEST_WIFI_AP_ENABLE`, but its documented SPI implementation was developed for ISM43362 rather than the specific ISM43340 firmware on this board. Therefore **this application is not yet a verified working SoftAP build for this hardware**. It will show **No onboard Wi-Fi driver** until a board-specific Zephyr devicetree overlay and a compatible driver/firmware are validated. Do **not** copy a guessed reset, chip-select or wake GPIO assignment into the devicetree.

The app already contains a simple LVGL UI, AP request and TCP echo logic for later use. Do not interpret the fixed SSID/IP text on screen as proof that an AP is running; only the **Status** field represents actual progression.

## Files

```text
bringup/wifi_ap/
├── README.md
├── CMakeLists.txt
├── prj.conf
└── src/main.c
```

## Initial diagnostics — Windows PowerShell (one command per line)

From `zephyr_workspace/`, examine the board's exact support:

```powershell
Select-String -Path .\zephyr\boards\st\stm32h7b3i_dk\stm32h7b3i_dk.dts -Pattern 'spi2|eswifi|wifi'
Select-String -Path .\zephyr\drivers\wifi\eswifi\eswifi_core.c -Pattern 'NET_REQUEST_WIFI_AP_ENABLE|eswifi_mgmt_ap_enable|AD\\r'
```

Before the AP test, confirm the board's Wi-Fi model, firmware version, module reset/wake/CS GPIOs and SPI control signals against the correct ST board schematic and its Wi-Fi BSP (`STM32CubeH7/Projects/STM32H7B3I-DK/Demonstrations/ClockAndWeather`). The module uses onboard SPI; do not wire the external CC3351 yet.

## Build and flash (once driver configuration is ready)

From your project root:

```powershell
.\zephyr_workspace\.venv\Scripts\Activate.ps1
cd zephyr_workspace
west build -p always -b stm32h7b3i_dk -s ..\bringup\wifi_ap -d ..\build
west flash -d ..\build
```

A build may succeed without onboard Wi-Fi support; in that case the LCD should explicitly show `No onboard Wi-Fi driver`. **That is a diagnostic outcome, not a SoftAP PASS.**

After the correct Wi-Fi overlay/driver have been integrated and AP activation succeeds, the intended test values are:

| Item | Demo value |
|---|---|
| SSID | `RideCast-DK` |
| WPA2 password | `RideCast1234` (demo only) |
| AP IPv4 | `192.168.4.1` (must be verified on device) |
| TCP server | Port `4242` |

## Android testing

1. Install **Serial WiFi Terminal** by Kai Morich from Google Play: https://play.google.com/store/apps/details?id=de.kai_morich.serial_wifi_terminal
2. Join `RideCast-DK` in Android Wi-Fi settings using the demo password. If Android reports no Internet, choose to stay connected.
3. In Serial WiFi Terminal, choose a **raw TCP socket** connection to `192.168.4.1` on port `4242` (only after verifying the displayed AP has started at that IP).
4. Enter `Hello STM32` and send. The server is designed to echo the bytes, update RX/TX counts and show the latest received text on the LCD.
5. Disconnect, reconnect and verify TCP session state changes and reaccept.

## Pass criteria

- The onboard Inventek driver is initialized and the access point actually appears in Android's Wi-Fi list.
- Android obtains an IP address and can join with WPA2 credentials.
- An Android raw TCP client connects to STM32 at the verified AP address and port.
- Data is echoed and the RX/TX counts increment accurately.
- The LVGL status distinguishes AP enabled, station connection (when reported by the driver), and TCP connection. Not all drivers report phone association events; TCP connection is the dependable proof of a working data session.
- Unplugging or disconnecting the phone does not freeze the LCD.

## References

- STM32H7B3I-DK: https://www.st.com/en/evaluation-tools/stm32h7b3i-dk.html
- Zephyr Inventek es-WIFI: https://docs.zephyrproject.org/latest/boards/shields/inventek_eswifi/doc/index.html
- Zephyr Wi-Fi AP sample: https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/samples/net/wifi/apsta_mode/src/main.c
- Inventek driver: https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/drivers/wifi/eswifi/eswifi_core.c
- Android terminal: https://play.google.com/store/apps/details?id=de.kai_morich.serial_wifi_terminal
