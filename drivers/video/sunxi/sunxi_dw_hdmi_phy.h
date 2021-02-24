/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2021 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#ifndef __SUNXI_DW_HDMI_PHY_H
#define __SUNXI_DW_HDMI_PHY_H

#include <edid.h>

/**
 * sunxi_dw_hdmi_phy_set() - configure sunxi DW HDMI PHY
 *
 * Configure Sunxi DW HDMI PHY as found in A64, H3, H5 and other SoCs according
 * to speficied mode.
 *
 * @phy: PHY port
 * @edid: timing to configure
 * @clk_div: Clock divider to set
 */
void sunxi_dw_hdmi_phy_set(struct phy *phy, const struct display_timing *edid,
			   int clk_div);

#endif
