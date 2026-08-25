/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Per data bit eye centring for sunxi DRAM PHYs.
 *
 * (C) Copyright 2026 Jernej Skrabec <jernej.skrabec@gmail.com>
 *
 */

#ifndef _DRAM_EYE_SCAN_H
#define _DRAM_EYE_SCAN_H

#include <linux/types.h>

/*
 * Implemented by the DRAM controller driver, which knows its own PHY.  Delays
 * are staged between begin() and commit(); nothing reaches the delay chain
 * until commit() hands the staged values over.
 */
u8 mctl_phy_bit_delay_max(bool tx);
u8 mctl_phy_bit_delay_get(bool tx, unsigned int lane, unsigned int bit);
void mctl_phy_bit_delay_begin(bool tx);
void mctl_phy_bit_delay_set(bool tx, unsigned int lane, unsigned int bit,
			    u8 delay);
void mctl_phy_bit_delay_commit(bool tx);

/*
 * Centre every data bit of the first @lanes byte lanes, in both directions.
 * Returns false when a bit ends up with an eye narrower than @min_width.
 */
bool mctl_phy_eye_scan(unsigned int lanes, u32 min_width);

#endif
