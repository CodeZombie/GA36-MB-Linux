// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *          Henson Li <lidongsheng111@gmail.com>
 * 
 * Modified by Jeremy Clark <https://github.com/CodeZombie> for use on the GA36-MB v1.2
 * The original version of this file can be found here: https://github.com/cutiepi-io/cutiepi-drivers/blob/master/Display/drivers/gpu/drm/panel/panel-jd9366.c
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

/*** Manufacturer Command Set ***/
#define MCS_CMD_MODE_SW		0xFE /* CMD Mode Switch */
#define MCS_CMD1_UCS		0x00 /* User Command Set (UCS = CMD1) */
#define MCS_CMD2_P0		0x01 /* Manufacture Command Set Page0 (CMD2 P0) */
#define MCS_CMD2_P1		0x02 /* Manufacture Command Set Page1 (CMD2 P1) */
#define MCS_CMD2_P2		0x03 /* Manufacture Command Set Page2 (CMD2 P2) */
#define MCS_CMD2_P3		0x04 /* Manufacture Command Set Page3 (CMD2 P3) */

/* CMD2 P0 commands (Display Options and Power) */
#define MCS_STBCTR		0x12 /* TE1 Output Setting Zig-Zag Connection */
#define MCS_SGOPCTR		0x16 /* Source Bias Current */
#define MCS_SDCTR		0x1A /* Source Output Delay Time */
#define MCS_INVCTR		0x1B /* Inversion Type */
#define MCS_EXT_PWR_IC		0x24 /* External PWR IC Control */
#define MCS_SETAVDD		0x27 /* PFM Control for AVDD Output */
#define MCS_SETAVEE		0x29 /* PFM Control for AVEE Output */
#define MCS_BT2CTR		0x2B /* DDVDL Charge Pump Control */
#define MCS_BT3CTR		0x2F /* VGH Charge Pump Control */
#define MCS_BT4CTR		0x34 /* VGL Charge Pump Control */
#define MCS_VCMCTR		0x46 /* VCOM Output Level Control */
#define MCS_SETVGN		0x52 /* VG M/S N Control */
#define MCS_SETVGP		0x54 /* VG M/S P Control */
#define MCS_SW_CTRL		0x5F /* Interface Control for PFM and MIPI */

/* CMD2 P2 commands (GOA Timing Control) - no description in datasheet */
#define GOA_VSTV1		0x00
#define GOA_VSTV2		0x07
#define GOA_VCLK1		0x0E
#define GOA_VCLK2		0x17
#define GOA_VCLK_OPT1		0x20
#define GOA_BICLK1		0x2A
#define GOA_BICLK2		0x37
#define GOA_BICLK3		0x44
#define GOA_BICLK4		0x4F
#define GOA_BICLK_OPT1		0x5B
#define GOA_BICLK_OPT2		0x60
#define MCS_GOA_GPO1		0x6D
#define MCS_GOA_GPO2		0x71
#define MCS_GOA_EQ		0x74
#define MCS_GOA_CLK_GALLON	0x7C
#define MCS_GOA_FS_SEL0		0x7E
#define MCS_GOA_FS_SEL1		0x87
#define MCS_GOA_FS_SEL2		0x91
#define MCS_GOA_FS_SEL3		0x9B
#define MCS_GOA_BS_SEL0		0xAC
#define MCS_GOA_BS_SEL1		0xB5
#define MCS_GOA_BS_SEL2		0xBF
#define MCS_GOA_BS_SEL3		0xC9
#define MCS_GOA_BS_SEL4		0xD3

/* CMD2 P3 commands (Gamma) */
#define MCS_GAMMA_VP		0x60 /* Gamma VP1~VP16 */
#define MCS_GAMMA_VN		0x70 /* Gamma VN1~VN16 */

struct jd9366 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *supply;
	struct backlight_device *backlight;
	bool prepared;
	bool enabled;
};

static const struct drm_display_mode default_mode = {
    .clock = 30000,                 /* lcd_dclk_freq * 1000 */

    .hdisplay = 640,                /* lcd_x */
    .hsync_start = 640 + 280,       /* hdisplay + calculated front porch */
    .hsync_end = 640 + 280 + 40,    /* hsync_start + lcd_hspw */
    .htotal = 1040,                 /* lcd_ht */

    .vdisplay = 480,                /* lcd_y */
    .vsync_start = 480 + 26,        /* vdisplay + calculated front porch */
    .vsync_end = 480 + 26 + 6,      /* vsync_start + lcd_vspw */
    .vtotal = 518,                  /* lcd_vt */

    .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC, /* Standard MIPI DSI sync polarities */

    /* Physical dimensions for a standard 3.5-inch 4:3 panel */
    .width_mm = 71,
    .height_mm = 53,
};

static inline struct jd9366 *panel_to_jd9366(struct drm_panel *panel)
{
	return container_of(panel, struct jd9366, panel);
}

static int jd9366_dcs_write_buf(struct jd9366 *ctx, const void *data,
				  size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	err = mipi_dsi_dcs_write_buffer(dsi, data, len);
	if (err < 0)
		return err;

	return 0;
}

static int __maybe_unused jd9366_dcs_write_cmd(struct jd9366 *ctx, u8 cmd, u8 value)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	err = mipi_dsi_dcs_write(dsi, cmd, &value, 1);
	if (err < 0)
		return err;

	return 0;
}

#define dcs_write_seq(ctx, seq...)				\
({								\
	static const u8 d[] = { seq };				\
								\
	jd9366_dcs_write_buf(ctx, d, ARRAY_SIZE(d));		\
})

/*
 * This panel is not able to auto-increment all cmd addresses so for some of
 * them, we need to send them one by one...
 */
#define dcs_write_cmd_seq(ctx, cmd, seq...)			\
({								\
	static const u8 d[] = { seq };				\
	unsigned int i;						\
								\
	for (i = 0; i < ARRAY_SIZE(d) ; i++)			\
		jd9366_dcs_write_cmd(ctx, cmd + i, d[i]);	\
})

static void jd9366_init_sequence(struct jd9366 *ctx)
{
    // Reverse-engineered from `lcd.ko` in the original GA36-MB v1.2's firmware via decompilation/static analysis.

    //Page0 ?? Do we need this?
    dcs_write_seq(ctx, 0xE0, 0x00);

    // Password?
    dcs_write_seq(ctx, 0xFF, 0x30);
    dcs_write_seq(ctx, 0xFF, 0x52);
    dcs_write_seq(ctx, 0xFF, 0x01);
    // Page0
    dcs_write_seq(ctx, 0xE3, 0x00);
    // ???
    dcs_write_seq(ctx, 0x20, 0x90);
    dcs_write_seq(ctx, 0x25, 0x10);
    dcs_write_seq(ctx, 0x28, 0x6F);
    dcs_write_seq(ctx, 0x29, 0x01);
    dcs_write_seq(ctx, 0x2A, 0xDF);
    dcs_write_seq(ctx, 0x30, 0x58);
    dcs_write_seq(ctx, 0x37, 0x9C);
    dcs_write_seq(ctx, 0x38, 0xA7);
    dcs_write_seq(ctx, 0x39, 0x53);
    dcs_write_seq(ctx, 0x44, 0x00);
    dcs_write_seq(ctx, 0x49, 0x3C);
    dcs_write_seq(ctx, 0x59, 0xFE);
    dcs_write_seq(ctx, 0x5C, 0x00);
    dcs_write_seq(ctx, 0x60, 0x8F);
    dcs_write_seq(ctx, 0x80, 0x20);
    dcs_write_seq(ctx, 0x91, 0x77);
    dcs_write_seq(ctx, 0x92, 0x77);
    dcs_write_seq(ctx, 0xA0, 0x55);
    dcs_write_seq(ctx, 0xA1, 0x50);
    dcs_write_seq(ctx, 0xA3, 0x58);
    dcs_write_seq(ctx, 0xA4, 0x9C);
    dcs_write_seq(ctx, 0xA7, 0x02);
    dcs_write_seq(ctx, 0xA8, 0x01);
    dcs_write_seq(ctx, 0xA9, 0x21);
    dcs_write_seq(ctx, 0xAA, 0xFC);
    dcs_write_seq(ctx, 0xAB, 0x28);
    dcs_write_seq(ctx, 0xAC, 0x06);
    dcs_write_seq(ctx, 0xAD, 0x06);
    dcs_write_seq(ctx, 0xAE, 0x06);
    dcs_write_seq(ctx, 0xAF, 0x03);
    dcs_write_seq(ctx, 0xB0, 0x08);
    dcs_write_seq(ctx, 0xB1, 0x26);
    dcs_write_seq(ctx, 0xB2, 0x28);
    dcs_write_seq(ctx, 0xB3, 0x28);
    dcs_write_seq(ctx, 0xB4, 0x03);
    dcs_write_seq(ctx, 0xB5, 0x08);
    dcs_write_seq(ctx, 0xB6, 0x26);
    dcs_write_seq(ctx, 0xB7, 0x08);
    dcs_write_seq(ctx, 0xB8, 0x26);
    dcs_write_seq(ctx, 0xFF, 0x30);
    dcs_write_seq(ctx, 0xFF, 0x52);
    dcs_write_seq(ctx, 0xFF, 0x02);
    dcs_write_seq(ctx, 0xB0, 0x0B);
    dcs_write_seq(ctx, 0xB1, 0x16);
    dcs_write_seq(ctx, 0xB2, 0x17);
    dcs_write_seq(ctx, 0xB3, 0x2C);
    dcs_write_seq(ctx, 0xB4, 0x32);
    dcs_write_seq(ctx, 0xB5, 0x3B);
    dcs_write_seq(ctx, 0xB6, 0x29);
    dcs_write_seq(ctx, 0xB7, 0x40);
    dcs_write_seq(ctx, 0xB8, 0x0D);
    dcs_write_seq(ctx, 0xB9, 0x05);
    dcs_write_seq(ctx, 0xBA, 0x12);
    dcs_write_seq(ctx, 0xBB, 0x10);
    dcs_write_seq(ctx, 0xBC, 0x12);
    dcs_write_seq(ctx, 0xBD, 0x15);
    dcs_write_seq(ctx, 0xBE, 0x19);
    dcs_write_seq(ctx, 0xBF, 0x0E);
    dcs_write_seq(ctx, 0xC0, 0x16);
    dcs_write_seq(ctx, 0xC1, 0x0A);
    dcs_write_seq(ctx, 0xD0, 0x0C);
    dcs_write_seq(ctx, 0xD1, 0x17);
    dcs_write_seq(ctx, 0xD2, 0x14);
    dcs_write_seq(ctx, 0xD3, 0x2E);
    dcs_write_seq(ctx, 0xD4, 0x32);
    dcs_write_seq(ctx, 0xD5, 0x3C);
    dcs_write_seq(ctx, 0xD6, 0x22);
    dcs_write_seq(ctx, 0xD7, 0x3D);
    dcs_write_seq(ctx, 0xD8, 0x0D);
    dcs_write_seq(ctx, 0xD9, 0x07);
    dcs_write_seq(ctx, 0xDA, 0x13);
    dcs_write_seq(ctx, 0xDB, 0x13);
    dcs_write_seq(ctx, 0xDC, 0x11);
    dcs_write_seq(ctx, 0xDD, 0x15);
    dcs_write_seq(ctx, 0xDE, 0x19);
    dcs_write_seq(ctx, 0xDF, 0x10);
    dcs_write_seq(ctx, 0xE0, 0x17);
    dcs_write_seq(ctx, 0xE1, 0x0A);
    dcs_write_seq(ctx, 0xFF, 0x30);
    dcs_write_seq(ctx, 0xFF, 0x52);
    dcs_write_seq(ctx, 0xFF, 0x03);
    dcs_write_seq(ctx, 0x00, 0x00);
    dcs_write_seq(ctx, 0x01, 0x00);
    dcs_write_seq(ctx, 0x02, 0x00);
    dcs_write_seq(ctx, 0x03, 0x00);
    dcs_write_seq(ctx, 0x04, 0x61);
    dcs_write_seq(ctx, 0x05, 0x80);
    dcs_write_seq(ctx, 0x06, 0xC7);
    dcs_write_seq(ctx, 0x07, 0x01);
    dcs_write_seq(ctx, 0x08, 0x82);
    dcs_write_seq(ctx, 0x09, 0x83);
    dcs_write_seq(ctx, 0x30, 0x00);
    dcs_write_seq(ctx, 0x31, 0x00);
    dcs_write_seq(ctx, 0x32, 0x00);
    dcs_write_seq(ctx, 0x33, 0x00);
    dcs_write_seq(ctx, 0x34, 0x61);
    dcs_write_seq(ctx, 0x35, 0xC5);
    dcs_write_seq(ctx, 0x36, 0x80);
    dcs_write_seq(ctx, 0x37, 0x23);
    dcs_write_seq(ctx, 0x40, 0x82);
    dcs_write_seq(ctx, 0x41, 0x83);
    dcs_write_seq(ctx, 0x42, 0x80);
    dcs_write_seq(ctx, 0x43, 0x81);
    dcs_write_seq(ctx, 0x44, 0x11);
    dcs_write_seq(ctx, 0x45, 0xE2);
    dcs_write_seq(ctx, 0x46, 0xE1);
    dcs_write_seq(ctx, 0x47, 0x11);
    dcs_write_seq(ctx, 0x48, 0xE4);
    dcs_write_seq(ctx, 0x49, 0xE3);
    dcs_write_seq(ctx, 0x50, 0x02);
    dcs_write_seq(ctx, 0x51, 0x01);
    dcs_write_seq(ctx, 0x52, 0x04);
    dcs_write_seq(ctx, 0x53, 0x03);
    dcs_write_seq(ctx, 0x54, 0x11);
    dcs_write_seq(ctx, 0x55, 0xE6);
    dcs_write_seq(ctx, 0x56, 0xE5);
    dcs_write_seq(ctx, 0x57, 0x11);
    dcs_write_seq(ctx, 0x58, 0xE8);
    dcs_write_seq(ctx, 0x59, 0xE7);
    dcs_write_seq(ctx, 0x7E, 0x08);
    dcs_write_seq(ctx, 0x81, 0x0F);
    dcs_write_seq(ctx, 0x84, 0x0C);
    dcs_write_seq(ctx, 0x85, 0x0D);
    dcs_write_seq(ctx, 0x86, 0x07);
    dcs_write_seq(ctx, 0x87, 0x04);
    dcs_write_seq(ctx, 0x88, 0x05);
    dcs_write_seq(ctx, 0x89, 0x06);
    dcs_write_seq(ctx, 0x8A, 0x00);
    dcs_write_seq(ctx, 0x97, 0x0F);
    dcs_write_seq(ctx, 0x9A, 0x0C);
    dcs_write_seq(ctx, 0x9B, 0x0D);
    dcs_write_seq(ctx, 0x9C, 0x07);
    dcs_write_seq(ctx, 0x9D, 0x04);
    dcs_write_seq(ctx, 0x9E, 0x05);
    dcs_write_seq(ctx, 0x9F, 0x06);
    dcs_write_seq(ctx, 0xA0, 0x00);
    dcs_write_seq(ctx, 0xE0, 0x02);
    dcs_write_seq(ctx, 0xE1, 0x52);
    dcs_write_seq(ctx, 0xFF, 0x30);
    dcs_write_seq(ctx, 0xFF, 0x52);
    dcs_write_seq(ctx, 0xFF, 0x00);
    dcs_write_seq(ctx, 0x36, 0x02);
    dcs_write_seq(ctx, 0x11); // SLPOUT
    // sunxi_lcd_delay_ms(120);
    msleep(120);
    dcs_write_seq(ctx, 0x29); // DISPON
    // sunxi_lcd_delay_ms(20);
    msleep(20);

    // TE -- do we need this?????
	// dcs_write_seq(ctx, 0x35,0x00);
}

static int jd9366_disable(struct drm_panel *panel)
{
	struct jd9366 *ctx = panel_to_jd9366(panel);

	if (!ctx->enabled)
		return 0;

	backlight_disable(ctx->backlight);

	ctx->enabled = false;

	return 0;
}

static int jd9366_unprepare(struct drm_panel *panel)
{
	struct jd9366 *ctx = panel_to_jd9366(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret)
		return ret;

	msleep(120);

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
	}

	regulator_disable(ctx->supply);

	ctx->prepared = false;

	return 0;
}

static int jd9366_prepare(struct drm_panel *panel)
{
	struct jd9366 *ctx = panel_to_jd9366(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_enable(ctx->supply);
	if (ret < 0)
		return ret;

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(100);
	}

	jd9366_init_sequence(ctx);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret)
		return ret;

	msleep(125);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	msleep(20);

	ctx->prepared = true;

	return 0;
}

static int jd9366_enable(struct drm_panel *panel)
{
	struct jd9366 *ctx = panel_to_jd9366(panel);

	if (ctx->enabled)
		return 0;

	backlight_enable(ctx->backlight);

	ctx->enabled = true;

	return 0;
}

static int jd9366_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay,
			default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs jd9366_drm_funcs = {
	.disable = jd9366_disable,
	.unprepare = jd9366_unprepare,
	.prepare = jd9366_prepare,
	.enable = jd9366_enable,
	.get_modes = jd9366_get_modes,
};

static int jd9366_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jd9366 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "cannot get reset GPIO: %d\n", ret);
		return ret;
	}

	ctx->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(ctx->supply)) {
		ret = PTR_ERR(ctx->supply);
		dev_err(dev, "cannot get regulator: %d\n", ret);
		return ret;
	}

	ctx->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(ctx->backlight))
		return PTR_ERR(ctx->backlight);

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, &dsi->dev, &jd9366_drm_funcs,
				DRM_MODE_CONNECTOR_DPI);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &jd9366_drm_funcs;

	ctx->panel.prepare_prev_first = true;
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach() failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void jd9366_remove(struct mipi_dsi_device *dsi)
{
	struct jd9366 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_jd9366_of_match[] = {
	{ .compatible = "boe,jd9366" },
	{ }
};
MODULE_DEVICE_TABLE(of, boe_jd9366_of_match);

static struct mipi_dsi_driver boe_jd9366_driver = {
	.probe = jd9366_probe,
	.remove = jd9366_remove,
	.driver = {
		.name = "panel-boe-jd9366",
		.of_match_table = boe_jd9366_of_match,
	},
};
module_mipi_dsi_driver(boe_jd9366_driver);

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_AUTHOR("Henson Li <lidongsheng111@gmail.com>");
MODULE_DESCRIPTION("DRM Driver for BOE JD9366 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
