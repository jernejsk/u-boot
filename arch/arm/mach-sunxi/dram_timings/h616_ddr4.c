/*
 * sun50i H616 DDR4 timings, as programmed by Allwinner's boot0
 *
 * The chips are probably able to be driven by a faster clock, but boot0
 * uses a more conservative timing (as usual).
 *
 * (C) Copyright 2024 Jernej Skrabec <jernej.skrabec@gmail.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/arch/dram.h>
#include <asm/arch/cpu.h>

void mctl_set_timing_params(const struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	u16 txsr	= 4;
	u8 tfaw		= ns_to_t(35);
	u8 trrd		= max(ns_to_t(8), 2);
	u8 txp		= max(ns_to_t(6), 2);
	u8 tmrd_pda	= max(ns_to_t(10), 8);
	u8 trp		= ns_to_t(15);
	u8 trc		= ns_to_t(49);
	u8 wr2rd_s	= max(ns_to_t(3), 1) + 7;
	u8 tras		= ns_to_t(34);
	u16 trefi	= ns_to_t(7800) / 32;
	u16 trfc	= ns_to_t(350);
	u8 txs		= ns_to_t(360) / 32;
	u8 tccd		= 3;
	u8 trcd		= trp;
	u8 t_rrd_s	= txp;

	u8 tmod		= max(ns_to_t(15), 12);
	u8 tcke		= max(ns_to_t(5), 2);
	u8 tcksre	= max(ns_to_t(10), 3);
	u8 txsabort	= ns_to_t(170) / 32;
	u8 txsfast	= txsabort;
	u8 tckesr	= tcke + 1;
	u8 trasmax	= ns_to_t(70200) / 1024;
	u8 trtp		= (trp < 5) ? 9 - trp : 4;
	u8 tphy_wrlat	= 5;
	u8 twr2rd	= trrd + 7;
	u8 tcl		= 7;
	u8 t_rdata_en	= 9;
	u8 trd2wr	= 5;
	u8 twtp		= 14;
	u8 tmrd		= 4;
	u8 tmrw		= 0;
	u8 tcksrx	= tcksre;
	u8 tcwl		= 5;
	u8 txsdll	= 16;

	/* set DRAM timing */
	writel((twtp << 24) | (tfaw << 16) | (trasmax << 8) | tras,
	       &mctl_ctl->dramtmg[0]);
	writel((txp << 16) | (trtp << 8) | trc, &mctl_ctl->dramtmg[1]);
	writel((tcwl << 24) | (tcl << 16) | (trd2wr << 8) | twr2rd,
	       &mctl_ctl->dramtmg[2]);
	writel((tmrw << 20) | (tmrd << 12) | tmod, &mctl_ctl->dramtmg[3]);
	writel((trcd << 24) | (tccd << 16) | (trrd << 8) | trp,
	       &mctl_ctl->dramtmg[4]);
	writel((tcksrx << 24) | (tcksre << 16) | (tckesr << 8) | tcke,
	       &mctl_ctl->dramtmg[5]);
	/* Value suggested by ZynqMP manual and used by libdram */
	writel((txp + 2) | 0x02020000, &mctl_ctl->dramtmg[6]);
	writel((txsfast << 24) | (txsabort << 16) | (txsdll << 8) | txs,
	       &mctl_ctl->dramtmg[8]);
	writel(0x20000 | (t_rrd_s << 8) | wr2rd_s, &mctl_ctl->dramtmg[9]);
	writel(0xE0C05, &mctl_ctl->dramtmg[10]);
	writel(0x440C021C, &mctl_ctl->dramtmg[11]);
	writel(tmrd_pda, &mctl_ctl->dramtmg[12]);
	writel(0xA100002, &mctl_ctl->dramtmg[13]);
	writel(txsr, &mctl_ctl->dramtmg[14]);

	clrsetbits_le32(&mctl_ctl->init[0], 0xC0000FFF, 0x112);
	writel(0x01f20000, &mctl_ctl->init[1]);
	clrsetbits_le32(&mctl_ctl->init[2], 0xFF0F, 0xd05);
	writel(0, &mctl_ctl->dfimisc);

	/* MR values */
	writel(0x05200601, &mctl_ctl->init[3]);
	writel(0x00080000, &mctl_ctl->init[4]);
	writel(0x00000400, &mctl_ctl->init[6]);
	writel(0x00000862, &mctl_ctl->init[7]);

	clrsetbits_le32(&mctl_ctl->rankctl, 0xff0, 0x660);

	/* Configure DFI timing */
	writel(tphy_wrlat | 0x2000000 | (t_rdata_en << 16) | 0x808000,
	       &mctl_ctl->dfitmg0);
	writel(0x100202, &mctl_ctl->dfitmg1);

	/* set refresh timing */
	writel((trefi << 16) | trfc, &mctl_ctl->rfshtmg);
}
