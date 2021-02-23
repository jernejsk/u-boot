// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (C) 2021 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <errno.h>
#include <asm/arch/ccu.h>
#include <asm/arch/clock.h>
#include <asm/io.h>
#include <dt-bindings/clock/sun8i-de2.h>
#include <dt-bindings/reset/sun8i-de2.h>
#include <linux/bitops.h>

static struct ccu_clk_gate de2_gates[] = {
	[CLK_MIXER0]		= GATE(0x00, BIT(0)),
	[CLK_MIXER1]		= GATE(0x00, BIT(1)),
	[CLK_WB]		= GATE(0x00, BIT(2)),

	[CLK_BUS_MIXER0]	= GATE(0x04, BIT(0)),
	[CLK_BUS_MIXER1]	= GATE(0x04, BIT(1)),
	[CLK_BUS_WB]		= GATE(0x04, BIT(2)),
};

static struct ccu_reset de2_resets[] = {
	[RST_MIXER0]		= RESET(0x08, BIT(0)),
	[RST_MIXER1]		= RESET(0x08, BIT(1)),
	[RST_WB]		= RESET(0x08, BIT(2)),
};

static const struct ccu_desc de2_ccu_desc = {
	.gates = de2_gates,
	.resets = de2_resets,
};

static int de2_clk_probe(struct udevice *dev)
{
	struct sunxi_ccm_reg * const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;

	/* clock driver doesn't know how to set rate or parent yet */
	clock_set_pll10(432000000);

	/* Set DE parent to pll10 */
	clrsetbits_le32(&ccm->de_clk_cfg, CCM_DE2_CTRL_PLL_MASK,
			CCM_DE2_CTRL_PLL10);

	return sunxi_clk_probe(dev);
}

static int de2_clk_bind(struct udevice *dev)
{
	return sunxi_reset_bind(dev, ARRAY_SIZE(de2_resets));
}

static const struct udevice_id de2_ccu_ids[] = {
	{ .compatible = "allwinner,sun8i-h3-de2-clk",
	  .data = (ulong)&de2_ccu_desc },
	{ .compatible = "allwinner,sun50i-a64-de2-clk",
	  .data = (ulong)&de2_ccu_desc },
	{ .compatible = "allwinner,sun50i-h5-de2-clk",
	  .data = (ulong)&de2_ccu_desc },
	{ }
};

U_BOOT_DRIVER(clk_sun8i_de2) = {
	.name		= "sun8i_de2_ccu",
	.id		= UCLASS_CLK,
	.of_match	= de2_ccu_ids,
	.priv_auto	= sizeof(struct ccu_priv),
	.ops		= &sunxi_clk_ops,
	.probe		= de2_clk_probe,
	.bind		= de2_clk_bind,
};
