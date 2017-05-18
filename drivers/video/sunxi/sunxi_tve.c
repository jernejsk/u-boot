/*
 * Allwinner TVE driver
 *
 * (C) Copyright 2017 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <display.h>
#include <dm.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/lcdc.h>
#include <asm/arch/tve.h>

static int sunxi_tve_get_plug_in_status(void)
{
	struct sunxi_tve_reg * const tve =
		(struct sunxi_tve_reg *)SUNXI_TVE0_BASE;
	u32 status;

	status = readl(&tve->auto_detect_status) &
		SUNXI_TVE_AUTO_DETECT_STATUS_MASK(0);

	return status == SUNXI_TVE_AUTO_DETECT_STATUS_CONNECTED;
}

static int sunxi_tve_wait_for_hpd(void)
{
	struct sunxi_tve_reg * const tve =
		(struct sunxi_tve_reg *)SUNXI_TVE0_BASE;
	ulong start;

	/* enable auto detection */
	writel(SUNXI_TVE_DAC_CFG0_DETECTION, &tve->dac_cfg0);
	writel(SUNXI_TVE_AUTO_DETECT_CFG0, &tve->auto_detect_cfg0);
	writel(SUNXI_TVE_AUTO_DETECT_CFG1, &tve->auto_detect_cfg1);
	writel(9 << SUNXI_TVE_AUTO_DETECT_DEBOUNCE_SHIFT(0),
	       &tve->auto_detect_debounce);
	writel(SUNXI_TVE_AUTO_DETECT_EN_DET_EN(0), &tve->auto_detect_en);

	start = get_timer(0);
	do {
		if (sunxi_tve_get_plug_in_status())
			return 0;
		udelay(100);
	} while (get_timer(start) < 300);

	return -1;
}

static void sunxi_tve_lcdc_init(const struct display_timing *edid, int bpp)
{
	struct sunxi_ccm_reg * const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
	struct sunxi_lcdc_reg * const lcdc =
		(struct sunxi_lcdc_reg *)SUNXI_LCD1_BASE;

	/* Reset off */
	setbits_le32(&ccm->ahb_reset1_cfg, 1 << AHB_RESET_OFFSET_LCD1);

	/* Clock on */
	setbits_le32(&ccm->ahb_gate1, 1 << AHB_GATE_OFFSET_LCD1);

	lcdc_init(lcdc);
	lcdc_tcon1_mode_set(lcdc, edid, false, true);
	lcdc_enable(lcdc, bpp);
}

static int sunxi_tve_read_timing(struct udevice *dev,
				 struct display_timing *timing)
{
	/* PAL resolution */
	timing->pixelclock.typ = 27000000;

	timing->hactive.typ = 720;
	timing->hfront_porch.typ = 5;
	timing->hback_porch.typ = 137;
	timing->hsync_len.typ = 2;

	timing->vactive.typ = 576;
	timing->vfront_porch.typ = 27;
	timing->vback_porch.typ = 20;
	timing->vsync_len.typ = 2;

	timing->flags = DISPLAY_FLAGS_INTERLACED;

	return 0;
}

static int sunxi_tve_enable(struct udevice *dev, int panel_bpp,
			    const struct display_timing *edid)
{
	struct sunxi_tve_reg * const tve =
		(struct sunxi_tve_reg *)SUNXI_TVE0_BASE;

	sunxi_tve_lcdc_init(edid, panel_bpp);

	tvencoder_mode_set(tve, tve_mode_composite_pal);
	tvencoder_enable(tve);

	return 0;
}

static int sunxi_tve_probe(struct udevice *dev)
{
	struct sunxi_ccm_reg * const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
	struct sunxi_tve_reg * const tve =
		(struct sunxi_tve_reg *)SUNXI_TVE0_BASE;
	int ret;

	/* make sure that clock is active */
	clock_set_pll10(432000000);

	/* Reset off */
	setbits_le32(&ccm->ahb_reset1_cfg, 1 << AHB_RESET_OFFSET_TVE);

	/* Clock on */
	setbits_le32(&ccm->ahb_gate1, 1 << AHB_GATE_OFFSET_TVE);
	writel(CCM_TVE_CTRL_GATE | CCM_TVE_CTRL_M(2), &ccm->tve_clk_cfg);

#ifdef CONFIG_MACH_SUN50I_H5
	writel(SUNXI_TVE_CALIBRATION_H5, &tve->calibration);
	writel(SUNXI_TVE_UNKNOWN3_H5, &tve->unknown3);
#else
	writel(SUNXI_TVE_CALIBRATION_H3, &tve->calibration);
#endif

	ret = sunxi_tve_wait_for_hpd();
	if (ret < 0) {
		debug("tve can not get hpd signal\n");
		return -1;
	}

	return 0;
}

static const struct dm_display_ops sunxi_tve_ops = {
	.read_timing = sunxi_tve_read_timing,
	.enable = sunxi_tve_enable,
};

U_BOOT_DRIVER(sunxi_tve) = {
	.name	= "sunxi_tve",
	.id	= UCLASS_DISPLAY,
	.ops	= &sunxi_tve_ops,
	.probe	= sunxi_tve_probe,
};

#ifdef CONFIG_MACH_SUNXI_H3_H5
U_BOOT_DEVICE(sunxi_tve) = {
	.name = "sunxi_tve"
};
#endif
