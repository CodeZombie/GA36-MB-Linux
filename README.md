# GA36-MB v1.2 Debian Trixie
This repo contains build files, code, and instructions for installing Debian 13 (Trixie) and the Linux Kernel version 6.12 on the Allwinner A23/A33-based GA36-MB v1.2 handheld emulator.

This distribution is very unoptimized for the hardware. This is a proof of concept. Do not use this as a base for anything you intend to daily-drive.

**INCREDIBLY IMPORTANT NOTE**: I have constructed both the display driver and .dts file via reverse-engineering. Reverse-engineering is inherently error-prone, and misconfiguration in the .dts file CAN result in overvolting components on your device, permanently destroying them. If you can't tolerate that risk, do NOT attempt to run this. USE AT YOUR OWN RISK. I AM NOT RESPONSIBLE FOR ANY DAMAGED HARDWARE.

## How do I get this to run?
If you've read the INCREDIBLY IMPORTANT NOTE, and are willing to continue, here's how it works:

The GA36-MB v1.2 (and likely v1.0 and v1.1) use the ancient Sunxi U-boot bootloader. This bootloader looks for an Android Boot Image (which contains a linux kernel) at a specific offset on disk. If you can overwrite the file at that offset with your own, you can boot your own linux kernel.

The first ~100 or so mb of the SD card contain important data the bootloader needs, and the bootloader itself, so we'll avoid that space. To be extra cautious, we'll bump that up to 128mb, and only use the space after that.

0. Install Docker and the extras needed to build the armv7 rootfs stage. On Debian/Ubuntu:

   `
   sudo apt install docker.io docker-buildx qemu-user-static binfmt-support
   `

   Then ensure your user can talk to the daemon (`sudo usermod -aG docker $USER` and re-login, or prefix docker commands with `sudo`).
1. Make an image of the stock SD card. Put it somewhere very safe. We'll refer to this as `original_sdcard.img`.
2. Put a new microSD card (at least 1gb, doesn't need to be empty, but all data on the disk will be lost) into your computer. Use `lsblk` to identify it's location. This will be referred to as `/dev/sdX` or `Debian SD Card`. Anywhere you see `/dev/sdX` should be replaced with this actual device path.
3. Let's construct an MBR on the disk: `sudo parted /dev/sdX mklabel msdos`
4. Create an ext4 partition starting at 128mb: `sudo parted /dev/sdX mkpart primary ext4 128MiB 100%`
5. And we'll format and label it: `sudo mkfs.ext4 -L "linux" /dev/sdX1`
6. Now let's copy the first 128mb of `original_sdcard.img` onto `Debian SD Card`, skipping the first 512bytes (which is our MBR we just created): `sudo dd if=original_sdcard.img of=/dev/sdX bs=512 skip=1 seek=1 count=262143 conv=notrunc status=progress`
7. Now run the dockerfile to build the kernel and rootfs: `docker buildx build --output type=local,dest=./build_output .`
8. `cd` into the `build_output` directory and write the android boot image to your debian sd card: `sudo dd if=android_boot.img bs=512 seek=172032 conv=notrunc status=progress of=/dev/sdX`
9. Now let's make a mount point for the debian rootfs: `sudo mkdir /mnt/ga36mb_linux`
10. And mount it... `sudo mount /dev/sdX1 /mnt/ga36mb_linux`
11. And extract the rootfs to that location: `sudo tar -xf debian-a33-rootfs.tar.gz -C /mnt/ga36mb_linux/`
12. Now run `sync` to force your OS to actually write all the data to the microSD card. It'll likely take a while.
13. Then unmount it with `sudo umount /mnt/ga36mb_linux`
14. Now unplug the microSD card and pop it into your device. If everything went well, it'll boot into debian linux. Default login is root / root.

## What's working so far?
- A .dts file has been created which boots the device (and keeps it booted), powers some of the important subsystems, and gets UART and the screen working. This .dts is not complete and needs a lot more work. **NOTE**: If you're unfamiliar with linux device tree, please be very careful. Careless edits could permanently destroy components on your device.
- The display driver's init sequence has been reverse-engineered and re-implemented for Linux 6.12.
- With a 2gb linux partition I was able to get it to launch Retroarch. Though I couldn't do much because the buttons don't work yet :)
- GPU/Hardware Acceleration
- USB host + USB ethernet (CDC / RTL8152 / AX88179). Plug in a powered USB-C network adapter and the system gets an IP, DNS, and time via DHCP — `apt` and SSH work out of the box. Check with `ip addr` and `resolvectl status`.

## What still needs to be done?
- Buttons (except the power button - pressing that immediately triggers a `shutdown`)
- Joysticks
- USB OTG 5V out (unverified — use a powered hub/adapter for now)
- USB Serial (if possible)
- USB Mass Storage Device (if possible)
- Second MicroSD card slot
- Audio/Headset detection
- LED management
- Power/Battery management
- Voltage ranges in the .dts haven't been validated.
- The `regulator-always-on` nodes in the dts should be checked. We probably don't want all of them to be always-on, but it keeps the kernel from killing power to important subsystems that have already been initialized by the bootloader.
- Probably everything else

Unedited forensic notes for this project can be viewed [here](https://gist.github.com/CodeZombie/9a89c669faec17c3d11eddb1eede2370)
