// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner DW HDMI bridge
 *
 * (C) Copyright 2017 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <clk.h>
#include <common.h>
#include <display.h>
#include <dm.h>
#include <dw_hdmi.h>
#include <edid.h>
#include <generic-phy.h>
#include <reset.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/lcdc.h>
#include <linux/bitops.h>
#include "sunxi_dw_hdmi_phy.h"

struct sunxi_dw_hdmi_priv {
	struct dw_hdmi hdmi;
	struct phy phy;
};

static void sunxi_dw_hdmi_pll_set(uint clk_khz, int *phy_div)
{
	int value, n, m, div, diff;
	int best_n = 0, best_m = 0, best_div = 0, best_diff = 0x0FFFFFFF;

	/*
	 * Find the lowest divider resulting in a matching clock. If there
	 * is no match, pick the closest lower clock, as monitors tend to
	 * not sync to higher frequencies.
	 */
	for (div = 1; div <= 16; div++) {
		int target = clk_khz * div;

		if (target < 192000)
			continue;
		if (target > 912000)
			continue;

		for (m = 1; m <= 16; m++) {
			n = (m * target) / 24000;

			if (n >= 1 && n <= 128) {
				value = (24000 * n) / m / div;
				diff = clk_khz - value;
				if (diff < best_diff) {
					best_diff = diff;
					best_m = m;
					best_n = n;
					best_div = div;
				}
			}
		}
	}

	*phy_div = best_div;

	clock_set_pll3_factors(best_m, best_n);
	debug("dotclock: %dkHz = %dkHz: (24MHz * %d) / %d / %d\n",
	      clk_khz, (clock_get_pll3() / 1000) / best_div,
	      best_n, best_m, best_div);
}

static void sunxi_dw_hdmi_lcdc_init(int mux, const struct display_timing *edid,
				    int bpp)
{
	struct sunxi_ccm_reg * const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
	int div = DIV_ROUND_UP(clock_get_pll3(), edid->pixelclock.typ);
	struct sunxi_lcdc_reg *lcdc;

	if (mux == 0) {
		lcdc = (struct sunxi_lcdc_reg *)SUNXI_LCD0_BASE;

		/* Reset off */
		setbits_le32(&ccm->ahb_reset1_cfg, 1 << AHB_RESET_OFFSET_LCD0);

		/* Clock on */
		setbits_le32(&ccm->ahb_gate1, 1 << AHB_GATE_OFFSET_LCD0);
		writel(CCM_LCD0_CTRL_GATE | CCM_LCD0_CTRL_M(div),
		       &ccm->lcd0_clk_cfg);
	} else {
		lcdc = (struct sunxi_lcdc_reg *)SUNXI_LCD1_BASE;

		/* Reset off */
		setbits_le32(&ccm->ahb_reset1_cfg, 1 << AHB_RESET_OFFSET_LCD1);

		/* Clock on */
		setbits_le32(&ccm->ahb_gate1, 1 << AHB_GATE_OFFSET_LCD1);
		writel(CCM_LCD1_CTRL_GATE | CCM_LCD1_CTRL_M(div),
		       &ccm->lcd1_clk_cfg);
	}

	lcdc_init(lcdc);
	lcdc_tcon1_mode_set(lcdc, edid, false, false);
	lcdc_enable(lcdc, bpp);
}

static int sunxi_dw_hdmi_phy_cfg(struct dw_hdmi *hdmi,
				 const struct display_timing *edid)
{
	struct sunxi_dw_hdmi_priv *priv =
		container_of(hdmi, struct sunxi_dw_hdmi_priv, hdmi);
	int phy_div;

	sunxi_dw_hdmi_pll_set(edid->pixelclock.typ / 1000, &phy_div);
	sunxi_dw_hdmi_phy_set(&priv->phy, edid, phy_div);

	return 0;
}

static int sunxi_dw_hdmi_read_edid(struct udevice *dev, u8 *buf, int buf_size)
{
	struct sunxi_dw_hdmi_priv *priv = dev_get_priv(dev);

	return dw_hdmi_read_edid(&priv->hdmi, buf, buf_size);
}

static bool sunxi_dw_hdmi_mode_valid(struct udevice *dev,
				     const struct display_timing *timing)
{
	return timing->pixelclock.typ <= 297000000;
}

static int sunxi_dw_hdmi_enable(struct udevice *dev, int panel_bpp,
				const struct display_timing *edid)
{
	struct sunxi_dw_hdmi_priv *priv = dev_get_priv(dev);
	struct display_plat *uc_plat = dev_get_uclass_plat(dev);
	int ret;

	ret = dw_hdmi_enable(&priv->hdmi, edid);
	if (ret)
		return ret;

	sunxi_dw_hdmi_lcdc_init(uc_plat->source_id, edid, panel_bpp);

	/*
	 * This is last hdmi access before boot, so scramble addresses
	 * again or othwerwise BSP driver won't work. Dummy read is
	 * needed or otherwise last write doesn't get written correctly.
	 */
	(void)readb(priv->hdmi.ioaddr);
	generic_phy_exit(&priv->phy);

	return 0;
}

static int sunxi_dw_hdmi_of_to_plat(struct udevice *dev)
{
	struct sunxi_dw_hdmi_priv *priv = dev_get_priv(dev);
	struct dw_hdmi *hdmi = &priv->hdmi;

	hdmi->ioaddr = (ulong)dev_read_addr(dev);
	hdmi->i2c_clk_high = 0xd8;
	hdmi->i2c_clk_low = 0xfe;
	hdmi->reg_io_width = 1;
	hdmi->phy_set = sunxi_dw_hdmi_phy_cfg;

	return 0;
}

static int sunxi_dw_hdmi_probe(struct udevice *dev)
{
	struct sunxi_dw_hdmi_priv *priv = dev_get_priv(dev);
	struct sunxi_ccm_reg * const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
	struct reset_ctl_bulk resets;
	struct clk_bulk clocks;
	int ret;

	ret = generic_phy_get_by_name(dev, "phy", &priv->phy);
	if (ret)
		return ret;

	/* Set pll3 to 297 MHz */
	clock_set_pll3(297000000);

	/* Set hdmi parent to pll3 */
	clrsetbits_le32(&ccm->hdmi_clk_cfg, CCM_HDMI_CTRL_PLL_MASK,
			CCM_HDMI_CTRL_PLL3);

	ret = generic_phy_init(&priv->phy);
	if (ret)
		return ret;

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

	ret = generic_phy_power_on(&priv->phy);
	if (ret)
		return ret;

	ret = dw_hdmi_phy_wait_for_hpd(&priv->hdmi);
	if (ret < 0) {
		debug("hdmi can not get hpd signal\n");
		return -1;
	}

	dw_hdmi_init(&priv->hdmi);

	return 0;
}

static const struct dm_display_ops sunxi_dw_hdmi_ops = {
	.read_edid = sunxi_dw_hdmi_read_edid,
	.enable = sunxi_dw_hdmi_enable,
	.mode_valid = sunxi_dw_hdmi_mode_valid,
};

static const struct udevice_id sunxi_dw_hdmi_ids[] = {
	{ .compatible = "allwinner,sun8i-a83t-dw-hdmi" },
	{ }
};

U_BOOT_DRIVER(sunxi_dw_hdmi) = {
	.name	= "sunxi_dw_hdmi",
	.id	= UCLASS_DISPLAY,
	.of_match = sunxi_dw_hdmi_ids,
	.ops	= &sunxi_dw_hdmi_ops,
	.of_to_plat = sunxi_dw_hdmi_of_to_plat,
	.probe	= sunxi_dw_hdmi_probe,
	.priv_auto	= sizeof(struct sunxi_dw_hdmi_priv),
};
