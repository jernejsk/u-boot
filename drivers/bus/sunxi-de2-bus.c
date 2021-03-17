// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner DE2 bus driver
 *
 * Copyright (C) 2021 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <common.h>
#include <dm.h>
#include <asm/io.h>

static int sunxi_de2_bus_probe(struct udevice *dev)
{
	u32 val;

	/* set SRAM for video use */
	val = readl(SUNXI_SRAMC_BASE + 0x04);
	val &= ~(0x01 << 24);
	writel(val, SUNXI_SRAMC_BASE + 0x04);

	return 0;
}

static const struct udevice_id sunxi_de2_bus_ids[] = {
	{ .compatible = "allwinner,sun50i-a64-de2" },
	{}
};

U_BOOT_DRIVER(sunxi_de2_bus) = {
	.name = "sunxi_de2_bus",
	.id = UCLASS_SIMPLE_BUS,
	.of_match = sunxi_de2_bus_ids,
	.probe	= sunxi_de2_bus_probe,
	.flags	= DM_FLAG_PRE_RELOC,
};
