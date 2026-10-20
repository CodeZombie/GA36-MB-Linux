# LED bring-up notes (GA36-MB v1.2)

Status: **working**. Green and red status LEDs verified on hardware.

## Result

| LED   | Pin  | DT phandle           | Polarity    | Sysfs                          |
|-------|------|----------------------|-------------|--------------------------------|
| Green | PL4  | `&r_pio 0 4`         | active high | `/sys/class/leds/green:status` |
| Red   | PG13 | `&pio 6 13`          | active high | `/sys/class/leds/red:status`   |

`echo 1 > .../brightness` turns the LED on, `echo 0` turns it off. Green is on by default at boot, red is off.

Changes in this repo:
- `sun8i-a33-ga36mb-v12.dts`: `gpio-leds` node (`led_green`, `led_red`), plus `#include <dt-bindings/leds/common.h>`.
- `Dockerfile`: `CONFIG_NEW_LEDS`, `CONFIG_LEDS_CLASS`, `CONFIG_LEDS_GPIO`.

Only `android_boot.img` needs rewriting after a rebuild (README step 8). The rootfs is unchanged.

## How the pins were found

1. **The stock image has no `.dts`.** It boots Allwinner's 3.4 vendor kernel, which reads a binary `sys_config` blob (`script.bin`) instead. That blob is embedded in the bootloader area of the SD image, found by searching for known keys (`boot_clock`, `leds_used`) and walking back to the `product` section header. Decompiled, it matches the repo's `ga36mb_v1.2.fex`.
2. **The fex does not describe the LEDs.** `[leds_para]` has `leds_used = 0`. Its `red_led`/`green_led` entries point at PMU pins `power3`/`power1`, which are generic vendor defaults. `power3` is also `motor_shake`. Do not use them.
3. **The fex does list the real pins** in `[gpio_para]` as plain outputs: `gpio_pin_4 = PL04`, `gpio_pin_5 = PG13` (also `PH03`, `PH02`, `PH01`, whose roles are unknown).
4. **The stock userland shows the use.** The image layout (MBR with logical partitions) is: u-boot env, boot image at sector 172032, `EMUELEC` FAT16 holding a `SYSTEM` squashfs, and an ext4 storage partition. In `SYSTEM`, `/usr/bin/udt_battery.sh` runs every 10 s:
   - battery capacity <= 20%: `PG13=1`, `PL4=0` (red)
   - otherwise: `PL4=1`, `PG13=0` (green)

   via `/sys/class/gpio_sw/<pin>/data`. It also powers off at low voltage.
5. **Colors were inferred** from that logic (low battery = red), then confirmed on the device.

## Not done

- Low-battery behavior (green off, red on at <= 20%). Options: a small systemd service porting `udt_battery.sh` using `/sys/class/leds`, or an LED trigger tied to the PMU battery (needs checking that the AXP driver exposes one).
- Charging indication. The fex has `pmu_chgled_func = 0`, `pmu_chgled_type = 0`, so the PMU's own charge LED output is probably unused.

## Reusing this method for other peripherals

- Search the decompiled fex for the peripheral's section (`[audio0]`, `[mmc1_para]`, `[gpio_para]`, ...).
- Grep the stock `SYSTEM` squashfs (`/usr/bin`, `/usr/lib/systemd/system`) for `/sys/class/gpio_sw/` and `/sys/devices/platform/` to see how vendor drivers are used. Pin names there (e.g. `PL4`) map directly to mainline `&pio` (ports A..H) or `&r_pio` (port L).
- Extraction helpers: the fex decoder was a throwaway script. Squashfs can be extracted with `7z x SYSTEM`, no `unsquashfs` needed.
