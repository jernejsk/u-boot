// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner DE2 display driver
 *
 * (C) Copyright 2017 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <common.h>
#include <clk.h>
#include <display.h>
#include <dm.h>
#include <edid.h>
#include <efi_loader.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <log.h>
#include <part.h>
#include <reset.h>
#include <video.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/display2.h>
#include <linux/bitops.h>
#include "simplefb_common.h"

DECLARE_GLOBAL_DATA_PTR;

enum {
	/* Maximum LCD size we support */
	LCD_MAX_WIDTH		= 3840,
	LCD_MAX_HEIGHT		= 2160,
	LCD_MAX_LOG2_BPP	= VIDEO_BPP32,
};

struct sunxi_de2_data {
	int id;
	const char *disp_drv_name;
};

static void sunxi_de2_mode_set(ulong de_mux_base,
			       const struct display_timing *mode,
			       int bpp, ulong address, bool is_composite)
{
	struct de_glb * const de_glb_regs =
		(struct de_glb *)(de_mux_base +
				  SUNXI_DE2_MUX_GLB_REGS);
	struct de_bld * const de_bld_regs =
		(struct de_bld *)(de_mux_base +
				  SUNXI_DE2_MUX_BLD_REGS);
	struct de_ui * const de_ui_regs =
		(struct de_ui *)(de_mux_base +
				 SUNXI_DE2_MUX_CHAN_REGS +
				 SUNXI_DE2_MUX_CHAN_SZ * 1);
	struct de_csc * const de_csc_regs =
		(struct de_csc *)(de_mux_base +
				  SUNXI_DE2_MUX_DCSC_REGS);
	u32 size = SUNXI_DE2_WH(mode->hactive.typ, mode->vactive.typ);
	int channel;
	u32 format;

	writel(SUNXI_DE2_MUX_GLB_CTL_EN, &de_glb_regs->ctl);
	writel(0, &de_glb_regs->status);
	writel(1, &de_glb_regs->dbuff);
	writel(size, &de_glb_regs->size);

	for (channel = 0; channel < 4; channel++) {
		void *ch = (void *)(de_mux_base + SUNXI_DE2_MUX_CHAN_REGS +
				    SUNXI_DE2_MUX_CHAN_SZ * channel);
		memset(ch, 0, (channel == 0) ?
			sizeof(struct de_vi) : sizeof(struct de_ui));
	}
	memset(de_bld_regs, 0, sizeof(struct de_bld));

	writel(0x00000101, &de_bld_regs->fcolor_ctl);

	writel(1, &de_bld_regs->route);

	writel(0, &de_bld_regs->premultiply);
	writel(0xff000000, &de_bld_regs->bkcolor);

	writel(0x03010301, &de_bld_regs->bld_mode[0]);

	writel(size, &de_bld_regs->output_size);
	writel(mode->flags & DISPLAY_FLAGS_INTERLACED ? 2 : 0,
	       &de_bld_regs->out_ctl);
	writel(0, &de_bld_regs->ck_ctl);

	writel(0xff000000, &de_bld_regs->attr[0].fcolor);
	writel(size, &de_bld_regs->attr[0].insize);

	/* Disable all other units */
	writel(0, de_mux_base + SUNXI_DE2_MUX_VSU_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_GSU1_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_GSU2_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_GSU3_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_FCE_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_BWS_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_LTI_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_PEAK_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_ASE_REGS);
	writel(0, de_mux_base + SUNXI_DE2_MUX_FCC_REGS);

	if (is_composite) {
		/* set CSC coefficients */
		writel(0x107, &de_csc_regs->coef11);
		writel(0x204, &de_csc_regs->coef12);
		writel(0x64, &de_csc_regs->coef13);
		writel(0x4200, &de_csc_regs->coef14);
		writel(0x1f68, &de_csc_regs->coef21);
		writel(0x1ed6, &de_csc_regs->coef22);
		writel(0x1c2, &de_csc_regs->coef23);
		writel(0x20200, &de_csc_regs->coef24);
		writel(0x1c2, &de_csc_regs->coef31);
		writel(0x1e87, &de_csc_regs->coef32);
		writel(0x1fb7, &de_csc_regs->coef33);
		writel(0x20200, &de_csc_regs->coef34);

		/* enable CSC unit */
		writel(1, &de_csc_regs->csc_ctl);
	} else {
		writel(0, &de_csc_regs->csc_ctl);
	}

	switch (bpp) {
	case 16:
		format = SUNXI_DE2_UI_CFG_ATTR_FMT(SUNXI_DE2_FORMAT_RGB_565);
		break;
	case 32:
	default:
		format = SUNXI_DE2_UI_CFG_ATTR_FMT(SUNXI_DE2_FORMAT_XRGB_8888);
		break;
	}

	writel(SUNXI_DE2_UI_CFG_ATTR_EN | format, &de_ui_regs->cfg[0].attr);
	writel(size, &de_ui_regs->cfg[0].size);
	writel(0, &de_ui_regs->cfg[0].coord);
	writel((bpp / 8) * mode->hactive.typ, &de_ui_regs->cfg[0].pitch);
	writel(address, &de_ui_regs->cfg[0].top_laddr);
	writel(size, &de_ui_regs->ovl_size);

	/* apply settings */
	writel(1, &de_glb_regs->dbuff);
}

static int sunxi_de2_init(struct udevice *dev, ulong fbbase,
			  enum video_log2_bpp l2bpp,
			  struct udevice *disp, int mux, bool is_composite)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct display_timing timing;
	struct display_plat *disp_uc_plat;
	struct reset_ctl_bulk resets;
	struct clk_bulk clocks;
	int ret;

	disp_uc_plat = dev_get_uclass_plat(disp);
	debug("Using device '%s', disp_uc_priv=%p\n", disp->name, disp_uc_plat);
	if (display_in_use(disp)) {
		debug("   - device in use\n");
		return -EBUSY;
	}

	disp_uc_plat->source_id = mux;

	ret = display_read_timing(disp, &timing);
	if (ret) {
		debug("%s: Failed to read timings\n", __func__);
		return ret;
	}

	ret = reset_get_bulk(dev, &resets);
	if (ret)
		return ret;

	ret = clk_get_bulk(dev, &clocks);
	if (ret)
		return ret;

	ret = clk_enable_bulk(&clocks);
	if (ret)
		return ret;

	ret = reset_deassert_bulk(&resets);
	if (ret)
		return ret;

	sunxi_de2_mode_set((ulong)dev_read_addr(dev), &timing,
			   1 << l2bpp, fbbase, is_composite);

	ret = display_enable(disp, 1 << l2bpp, &timing);
	if (ret) {
		debug("%s: Failed to enable display\n", __func__);
		return ret;
	}

	uc_priv->xsize = timing.hactive.typ;
	uc_priv->ysize = timing.vactive.typ;
	uc_priv->bpix = l2bpp;
	debug("fb=%lx, size=%d %d\n", fbbase, uc_priv->xsize, uc_priv->ysize);

#ifdef CONFIG_EFI_LOADER
	efi_add_memory_map(fbbase,
			   timing.hactive.typ * timing.vactive.typ *
			   (1 << l2bpp) / 8,
			   EFI_RESERVED_MEMORY_TYPE);
#endif

	return 0;
}

static int sunxi_de2_probe(struct udevice *dev)
{
	const struct sunxi_de2_data *data =
		(const struct sunxi_de2_data *)dev_get_driver_data(dev);
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct udevice *disp;
	struct uclass *uc;
	int ret;

	/* Before relocation we don't need to do anything */
	if (!(gd->flags & GD_FLG_RELOC))
		return 0;

	uclass_id_foreach_dev(UCLASS_DISPLAY, disp, uc) {
		if (strcmp(disp->driver->name, data->disp_drv_name))
			continue;

		/*
		 * This could be just simple device_probe() but it's not meant
		 * to be called from drivers (it resides in device-internal.h
		 * header).
		 */
		ret = uclass_get_device_by_seq(UCLASS_DISPLAY,
					       dev_seq(disp), &disp);
		if (ret)
			break;

		ret = sunxi_de2_init(dev, plat->base, VIDEO_BPP32, disp,
				     data->id, false);
		if (ret)
			break;

		video_set_flush_dcache(dev, 1);

		debug("%s successfully connected to %s\n",
		      dev->name, data->disp_drv_name);

		return 0;
	}

	debug("%s: %s not found (ret=%d)\n", __func__,
	      data->disp_drv_name, ret);

	return ret;
}

static int sunxi_de2_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->size = LCD_MAX_WIDTH * LCD_MAX_HEIGHT *
		(1 << LCD_MAX_LOG2_BPP) / 8;

	return 0;
}

const struct sunxi_de2_data h3_mixer_0 = {
	.id = 0,
	.disp_drv_name = "sunxi_dw_hdmi",
};

const struct sunxi_de2_data a64_mixer_0 = {
	.id = 0,
	.disp_drv_name = "sunxi_lcd",
};

const struct sunxi_de2_data a64_mixer_1 = {
	.id = 1,
	.disp_drv_name = "sunxi_dw_hdmi",
};

static const struct udevice_id sunxi_de2_ids[] = {
	{
		.compatible = "allwinner,sun8i-h3-de2-mixer-0",
		.data = (ulong)&h3_mixer_0,
	},
	{
		.compatible = "allwinner,sun50i-a64-de2-mixer-0",
		.data = (ulong)&a64_mixer_0,
	},
	{
		.compatible = "allwinner,sun50i-a64-de2-mixer-1",
		.data = (ulong)&a64_mixer_1,
	},
	{ }
};

static const struct video_ops sunxi_de2_ops = {
};

U_BOOT_DRIVER(sunxi_de2) = {
	.name	= "sunxi_de2",
	.id	= UCLASS_VIDEO,
	.of_match = sunxi_de2_ids,
	.ops	= &sunxi_de2_ops,
	.bind	= sunxi_de2_bind,
	.probe	= sunxi_de2_probe,
	.flags	= DM_FLAG_PRE_RELOC,
};

/*
 * Simplefb support.
 */
#if defined(CONFIG_OF_BOARD_SETUP) && defined(CONFIG_VIDEO_DT_SIMPLEFB)
int sunxi_simplefb_setup(void *blob)
{
	struct udevice *de2, *hdmi, *lcd;
	struct video_priv *de2_priv;
	struct video_uc_plat *de2_plat;
	int mux;
	int offset, ret;
	u64 start, size;
	const char *pipeline = NULL;

	debug("Setting up simplefb\n");

	if (IS_ENABLED(CONFIG_MACH_SUNXI_H3_H5))
		mux = 0;
	else
		mux = 1;

	/* Skip simplefb setting if DE2 / HDMI is not present */
	ret = uclass_get_device_by_driver(UCLASS_VIDEO,
					  DM_DRIVER_GET(sunxi_de2), &de2);
	if (ret) {
		debug("DE2 not present\n");
		return 0;
	} else if (!device_active(de2)) {
		debug("DE2 present but not probed\n");
		return 0;
	}

	ret = uclass_get_device_by_driver(UCLASS_DISPLAY,
					  DM_DRIVER_GET(sunxi_dw_hdmi), &hdmi);
	if (ret) {
		debug("HDMI not present\n");
	} else if (device_active(hdmi)) {
		if (mux == 0)
			pipeline = "mixer0-lcd0-hdmi";
		else
			pipeline = "mixer1-lcd1-hdmi";
	} else {
		debug("HDMI present but not probed\n");
	}

	ret = uclass_get_device_by_driver(UCLASS_DISPLAY,
					  DM_DRIVER_GET(sunxi_lcd), &lcd);
	if (ret)
		debug("LCD not present\n");
	else if (device_active(lcd))
		pipeline = "mixer0-lcd0";
	else
		debug("LCD present but not probed\n");

	if (!pipeline) {
		debug("No active display present\n");
		return 0;
	}

	de2_priv = dev_get_uclass_priv(de2);
	de2_plat = dev_get_uclass_plat(de2);

	offset = sunxi_simplefb_fdt_match(blob, pipeline);
	if (offset < 0) {
		eprintf("Cannot setup simplefb: node not found\n");
		return 0; /* Keep older kernels working */
	}

	start = gd->bd->bi_dram[0].start;
	size = de2_plat->base - start;
	ret = fdt_fixup_memory_banks(blob, &start, &size, 1);
	if (ret) {
		eprintf("Cannot setup simplefb: Error reserving memory\n");
		return ret;
	}

	ret = fdt_setup_simplefb_node(blob, offset, de2_plat->base,
			de2_priv->xsize, de2_priv->ysize,
			VNBYTES(de2_priv->bpix) * de2_priv->xsize,
			"x8r8g8b8");
	if (ret)
		eprintf("Cannot setup simplefb: Error setting properties\n");

	return ret;
}
#endif /* CONFIG_OF_BOARD_SETUP && CONFIG_VIDEO_DT_SIMPLEFB */
