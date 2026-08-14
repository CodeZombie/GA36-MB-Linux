# syntax=docker/dockerfile:1

# ==========================================
# Stage 1: The Kernel
# ==========================================
FROM debian:bookworm-slim AS kernel_builder

RUN apt-get update && apt-get install -y \
    build-essential gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
    bc bison flex libssl-dev make libc6-dev libncurses5-dev \
    git wget cpio kmod rsync device-tree-compiler mkbootimg \
    && rm -rf /var/lib/apt/lists/*

ENV ARCH=arm
ENV CROSS_COMPILE=arm-linux-gnueabihf-

WORKDIR /src
RUN git clone --depth=1 -b linux-6.12.y https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

WORKDIR /src/linux

# Copy our display driver driver
COPY jd9366-ga36mbv1-2.c drivers/gpu/drm/panel/jd9366-ga36mbv1-2.c

# Register the driver in the Kbuild Makefile
RUN echo 'obj-$(CONFIG_DRM_PANEL_JD9366) += jd9366-ga36mbv1-2.o' >> drivers/gpu/drm/panel/Makefile

# Add the configuration option to Kconfig so the build system recognizes it.
RUN sed -i '/^endmenu/i \
config DRM_PANEL_JD9366\n\
\ttristate "JD9366 panel"\n\
\tdepends on OF && DRM && BACKLIGHT_CLASS_DEVICE\n\
\thelp\n\
\t  Say Y here if you want to enable support for JD9366 panels.\n\
' drivers/gpu/drm/panel/Kconfig

# Inject the dts file
COPY sun8i-a33-ga36mb-v12.dts arch/arm/boot/dts/allwinner/
RUN echo 'dtb-$(CONFIG_MACH_SUN8I) += sun8i-a33-ga36mb-v12.dtb' >> arch/arm/boot/dts/allwinner/Makefile

RUN make sunxi_defconfig
RUN ./scripts/config --enable CONFIG_ARM_APPENDED_DTB && \
    ./scripts/config --enable CONFIG_ARM_ATAG_DTB_COMPAT && \
    ./scripts/config --set-str CONFIG_CMDLINE "root=/dev/mmcblk0p1 rootfstype=ext4 rootwait rw init=/sbin/init coherent_pool=4m boot_type=1 config_size=0 earlycon=uart,mmio32,0x01c28800 loglevel=8 initcall_debug=1" && \
    ./scripts/config --enable CONFIG_CMDLINE_FORCE && \
    ./scripts/config --enable CONFIG_EXT4_FS && \
    ./scripts/config --enable CONFIG_DEVTMPFS && \
    ./scripts/config --enable CONFIG_DEVTMPFS_MOUNT && \
    ./scripts/config --enable CONFIG_GPIO_SYSFS && \
    ./scripts/config --enable CONFIG_VFP && \
    ./scripts/config --enable CONFIG_VFPv4 && \
    ./scripts/config --enable CONFIG_NEON && \
    ./scripts/config --enable CONFIG_SMP && \
    ./scripts/config --enable CONFIG_ARCH_SUNXI && \
    ./scripts/config --enable CONFIG_MACH_SUN8I && \
    ./scripts/config --enable CONFIG_PREEMPT && \
    ./scripts/config --enable CONFIG_CPU_FREQ && \
    ./scripts/config --enable CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND && \
    ./scripts/config --enable CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE && \
    ./scripts/config --enable CONFIG_MFD_AXP20X_RSB && \
    ./scripts/config --enable CONFIG_REGULATOR_AXP20X && \
    ./scripts/config --enable CONFIG_DRM_SUN4I && \
    ./scripts/config --enable CONFIG_DRM_PANEL_SIMPLE && \
    ./scripts/config --enable CONFIG_BACKLIGHT_PWM && \
    ./scripts/config --enable CONFIG_PWM_SUN4I && \
    ./scripts/config --enable CONFIG_SND_SUN8I_CODEC && \
    ./scripts/config --enable CONFIG_DRM_SUN6I_MIPI_DSI && \
    ./scripts/config --enable CONFIG_COMMON_CLK_DEBUGFS && \
    ./scripts/config --enable CONFIG_DRM_PANEL_JD9366 && \
    ./scripts/config --enable CONFIG_DRM_MIPI_DSI && \
    ./scripts/config --enable CONFIG_DRM_FBDEV_EMULATION && \
    ./scripts/config --enable CONFIG_FRAMEBUFFER_CONSOLE && \
    ./scripts/config --enable CONFIG_FB && \
    ./scripts/config --enable CONFIG_DRM_LIMA && \
    ./scripts/config --enable CONFIG_USB && \
    ./scripts/config --enable CONFIG_USB_EHCI_HCD && \
    ./scripts/config --enable CONFIG_USB_EHCI_HCD_PLATFORM && \
    ./scripts/config --enable CONFIG_USB_OHCI_HCD && \
    ./scripts/config --enable CONFIG_USB_OHCI_HCD_PLATFORM && \
    ./scripts/config --enable CONFIG_USB_MUSB_HDRC && \
    ./scripts/config --enable CONFIG_USB_MUSB_SUNXI && \
    ./scripts/config --enable CONFIG_USB_MUSB_DUAL_ROLE && \
    ./scripts/config --enable CONFIG_PHY_SUN4I_USB && \
    ./scripts/config --enable CONFIG_USB_NET_DRIVERS && \
    ./scripts/config --enable CONFIG_USB_USBNET && \
    ./scripts/config --enable CONFIG_USB_NET_CDCETHER && \
    ./scripts/config --enable CONFIG_USB_NET_CDC_NCM && \
    ./scripts/config --enable CONFIG_USB_NET_CDC_SUBSET && \
    ./scripts/config --enable CONFIG_USB_RTL8152 && \
    ./scripts/config --enable CONFIG_USB_NET_AX88179_178A && \
    ./scripts/config --enable CONFIG_USB_ANNOUNCE_NEW_DEVICES && \
    ./scripts/config --enable CONFIG_USB_G_SERIAL && \
    ./scripts/config --enable CONFIG_INPUT_KEYBOARD && \
    ./scripts/config --enable CONFIG_KEYBOARD_GPIO && \
    ./scripts/config --enable CONFIG_KEYBOARD_GPIO_POLLED && \
    ./scripts/config --enable CONFIG_INPUT_JOYDEV && \
    ./scripts/config --enable CONFIG_SWAP && \
    ./scripts/config --enable CONFIG_ZSMALLOC && \
    ./scripts/config --enable CONFIG_ZRAM && \
    ./scripts/config --enable CONFIG_CRYPTO_LZ4 && \
    ./scripts/config --enable CONFIG_CRYPTO_ZSTD && \
    ./scripts/config --enable CONFIG_CRYPTO_LZO && \
    ./scripts/config --enable CONFIG_THERMAL && \
    ./scripts/config --enable CONFIG_CPU_THERMAL && \
    ./scripts/config --enable CONFIG_THERMAL_GOV_STEP_WISE && \
    ./scripts/config --enable CONFIG_IIO && \
    ./scripts/config --enable CONFIG_MFD_SUN4I_GPADC && \
    ./scripts/config --enable CONFIG_SUN4I_GPADC && \
    make olddefconfig

# TODO: Do we need `CONFIG_SUN8I_THERMAL`? The kernel seems to be able to read cpu temps without it.

# Build kernel and DTBs
RUN make -j$(nproc) zImage dtbs

# Append DTB to the kernel
RUN cat arch/arm/boot/zImage arch/arm/boot/dts/allwinner/sun8i-a33-ga36mb-v12.dtb > arch/arm/boot/zImage_with_dtb

# Build an android boot image with the kernel
# The bootloader requires that 0x40000000 offset.
# If we manage to rebuild the bootloader with modern U-Boot, we can probably get rid of that.
RUN touch empty_ramdisk
RUN mkbootimg \
    --kernel arch/arm/boot/zImage_with_dtb \
    --ramdisk empty_ramdisk \
    --base 0x40000000 \
    --board sun8i \
    --pagesize 2048 \
    -o /src/android_boot.img


# ==========================================================================================================================
# Stage 2: RootFS
#
# This is a major hack: We're just pulling an armv7 build of trixie from docker hub and packaging that up into a tar.gz.
# In the future, we need to use Buildroot to compile the entire rootfs with the appropriate SoC-specific flags (Neon, etc).
# This, however, is very convenient. If we put package names in the `apt-get install` line, they automatically get
# put in our output rootfs and we can run them on the device. It also doesn't need to compile anything, so it's _fast_.
# ==========================================================================================================================

FROM --platform=linux/arm/v7 debian:trixie-slim AS rootfs_builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    systemd systemd-sysv systemd-resolved systemd-timesyncd \
    udev dbus netbase iproute2 iputils-ping \
    kmod nano openssh-server busybox libdrm-tests glmark2-es2-drm libegl1 libgles2 libglx-mesa0 \
    && rm -rf /var/lib/apt/lists/*

RUN echo "root:root" | chpasswd
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config
RUN echo "/dev/mmcblk0p1 / ext4 defaults 0 1" > /etc/fstab
RUN rm -f /usr/sbin/policy-rc.d

# Networking: networkd DHCPs on wired/USB ethernet, resolved provides DNS,
# timesyncd sets the clock (apt refuses repo signatures if the date is wrong).
COPY rootfs/etc/systemd/network/20-wired.network /etc/systemd/network/20-wired.network
RUN systemctl enable systemd-networkd systemd-resolved systemd-timesyncd

# /etc/resolv.conf must point at systemd-resolved, but Docker bind-mounts that
# path during the build, so stage the symlink separately and add it to the tar.
RUN mkdir -p /staging/etc && ln -s /usr/lib/systemd/resolv.conf /staging/etc/resolv.conf

# -------------------------------------------------------------------
# Enable USB OTG Serial Root Shell
# -------------------------------------------------------------------
# 1. Auto-load the g_serial kernel module on boot
RUN echo "g_serial" > /etc/modules-load.d/usb-serial-gadget.conf

# 2. Create the systemd drop-in override for auto-login on ttyGS0
RUN mkdir -p /etc/systemd/system/serial-getty@ttyGS0.service.d && \
    echo "[Service]" > /etc/systemd/system/serial-getty@ttyGS0.service.d/override.conf && \
    echo "ExecStart=" >> /etc/systemd/system/serial-getty@ttyGS0.service.d/override.conf && \
    echo "ExecStart=-/sbin/agetty -o '-p -f -- \\u' --noclear --autologin root %I \$TERM" >> /etc/systemd/system/serial-getty@ttyGS0.service.d/override.conf

# 3. Manually enable the service by creating the systemd "wants" symlink
RUN mkdir -p /etc/systemd/system/getty.target.wants && \
    ln -sf /lib/systemd/system/serial-getty@.service /etc/systemd/system/getty.target.wants/serial-getty@ttyGS0.service
# -------


# Package the filesystem.
RUN tar --numeric-owner -cpzf /debian-a33-rootfs.tar.gz \
    --exclude=/debian-a33-rootfs.tar.gz \
    --exclude=/proc/* \
    --exclude=/sys/* \
    --exclude=/dev/* \
    --exclude=/run/* \
    --exclude=/.dockerenv \
    --exclude=/staging \
    --exclude=/etc/resolv.conf \
    / \
    -C /staging etc/resolv.conf

# ==========================================
# Stage 3: Export Artifacts
# ==========================================
FROM scratch AS artifacts

COPY --from=kernel_builder /src/android_boot.img /android_boot.img
COPY --from=kernel_builder /src/linux/.config /kernel_config.txt
COPY --from=rootfs_builder /debian-a33-rootfs.tar.gz /
