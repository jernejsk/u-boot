// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner DW HDMI PHY driver
 *
 * (C) Copyright 2021 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <clk.h>
#include <common.h>
#include <dm.h>
#include <generic-phy.h>
#include <reset.h>
#include <time.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include "sunxi_dw_hdmi_phy.h"

#define SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK		BIT(0)
#define SUN8I_HDMI_PHY_DBG_CTRL_POL_MASK	GENMASK(15, 8)
#define SUN8I_HDMI_PHY_DBG_CTRL_POL_NHSYNC	BIT(8)
#define SUN8I_HDMI_PHY_DBG_CTRL_POL_NVSYNC	BIT(9)
#define SUN8I_HDMI_PHY_DBG_CTRL_ADDR_MASK	GENMASK(23, 16)
#define SUN8I_HDMI_PHY_DBG_CTRL_ADDR(addr)	(addr << 16)

#define SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN	BIT(31)

#define SUN8I_HDMI_PHY_READ_EN_MAGIC		0x54524545

#define SUN8I_HDMI_PHY_UNSCRAMBLE_MAGIC		0x42494E47

#define SUN8I_HDMI_PHY_ANA_CFG1_REG_SWI		BIT(31)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_PWEND	BIT(30)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_PWENC	BIT(29)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_CALSW	BIT(28)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_SVRCAL(x)	((x) << 26)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_SVBH(x)	((x) << 24)
#define SUN8I_HDMI_PHY_ANA_CFG1_AMP_OPT		BIT(23)
#define SUN8I_HDMI_PHY_ANA_CFG1_EMP_OPT		BIT(22)
#define SUN8I_HDMI_PHY_ANA_CFG1_AMPCK_OPT	BIT(21)
#define SUN8I_HDMI_PHY_ANA_CFG1_EMPCK_OPT	BIT(20)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL		BIT(19)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG		BIT(18)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_SCKTMDS	BIT(17)
#define SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN	BIT(16)
#define SUN8I_HDMI_PHY_ANA_CFG1_TXEN_MASK	GENMASK(15, 12)
#define SUN8I_HDMI_PHY_ANA_CFG1_TXEN_ALL	(0xf << 12)
#define SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK	BIT(11)
#define SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2	BIT(10)
#define SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1	BIT(9)
#define SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0	BIT(8)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDSCLK	BIT(7)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2	BIT(6)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1	BIT(5)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0	BIT(4)
#define SUN8I_HDMI_PHY_ANA_CFG1_CKEN		BIT(3)
#define SUN8I_HDMI_PHY_ANA_CFG1_LDOEN		BIT(2)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENVBS		BIT(1)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENBI		BIT(0)

#define SUN8I_HDMI_PHY_ANA_CFG2_M_EN		BIT(31)
#define SUN8I_HDMI_PHY_ANA_CFG2_PLLDBEN		BIT(30)
#define SUN8I_HDMI_PHY_ANA_CFG2_SEN		BIT(29)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_HPDPD	BIT(28)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_HPDEN	BIT(27)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_PLRCK	BIT(26)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_PLR(x)	((x) << 23)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_DENCK	BIT(22)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_DEN		BIT(21)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_CD(x)	((x) << 19)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_CKSS(x)	((x) << 17)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSWCK	BIT(16)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSW	BIT(15)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_CSMPS(x)	((x) << 13)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(x)	((x) << 10)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_BOOSTCK(x)	((x) << 8)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_BOOST(x)	((x) << 6)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_RESDI(x)	((x) << 0)

#define SUN8I_HDMI_PHY_ANA_CFG3_REG_SLOWCK(x)	((x) << 30)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_SLOW(x)	((x) << 28)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_WIRE(x)	((x) << 18)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(x)	((x) << 14)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_EMPCK(x)	((x) << 11)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(x)	((x) << 7)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_EMP(x)	((x) << 4)
#define SUN8I_HDMI_PHY_ANA_CFG3_SDAPD		BIT(3)
#define SUN8I_HDMI_PHY_ANA_CFG3_SDAEN		BIT(2)
#define SUN8I_HDMI_PHY_ANA_CFG3_SCLPD		BIT(1)
#define SUN8I_HDMI_PHY_ANA_CFG3_SCLEN		BIT(0)

#define SUN8I_HDMI_PHY_PLL_CFG1_REG_OD1		BIT(31)
#define SUN8I_HDMI_PHY_PLL_CFG1_REG_OD		BIT(30)
#define SUN8I_HDMI_PHY_PLL_CFG1_LDO2_EN		BIT(29)
#define SUN8I_HDMI_PHY_PLL_CFG1_LDO1_EN		BIT(28)
#define SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33	BIT(27)
#define SUN8I_HDMI_PHY_PLL_CFG1_CKIN_SEL_MSK	BIT(26)
#define SUN8I_HDMI_PHY_PLL_CFG1_CKIN_SEL_SHIFT	26
#define SUN8I_HDMI_PHY_PLL_CFG1_PLLEN		BIT(25)
#define SUN8I_HDMI_PHY_PLL_CFG1_LDO_VSET(x)	((x) << 22)
#define SUN8I_HDMI_PHY_PLL_CFG1_UNKNOWN(x)	((x) << 20)
#define SUN8I_HDMI_PHY_PLL_CFG1_PLLDBEN		BIT(19)
#define SUN8I_HDMI_PHY_PLL_CFG1_CS		BIT(18)
#define SUN8I_HDMI_PHY_PLL_CFG1_CP_S(x)		((x) << 13)
#define SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(x)	((x) << 7)
#define SUN8I_HDMI_PHY_PLL_CFG1_BWS		BIT(6)
#define SUN8I_HDMI_PHY_PLL_CFG1_B_IN_MSK	GENMASK(5, 0)
#define SUN8I_HDMI_PHY_PLL_CFG1_B_IN_SHIFT	0

#define SUN8I_HDMI_PHY_PLL_CFG2_SV_H		BIT(31)
#define SUN8I_HDMI_PHY_PLL_CFG2_PDCLKSEL(x)	((x) << 29)
#define SUN8I_HDMI_PHY_PLL_CFG2_CLKSTEP(x)	((x) << 27)
#define SUN8I_HDMI_PHY_PLL_CFG2_PSET(x)		((x) << 24)
#define SUN8I_HDMI_PHY_PLL_CFG2_PCLK_SEL	BIT(23)
#define SUN8I_HDMI_PHY_PLL_CFG2_AUTOSYNC_DIS	BIT(22)
#define SUN8I_HDMI_PHY_PLL_CFG2_VREG2_OUT_EN	BIT(21)
#define SUN8I_HDMI_PHY_PLL_CFG2_VREG1_OUT_EN	BIT(20)
#define SUN8I_HDMI_PHY_PLL_CFG2_VCOGAIN_EN	BIT(19)
#define SUN8I_HDMI_PHY_PLL_CFG2_VCOGAIN(x)	((x) << 16)
#define SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(x)	((x) << 12)
#define SUN8I_HDMI_PHY_PLL_CFG2_VCO_RST_IN	BIT(11)
#define SUN8I_HDMI_PHY_PLL_CFG2_SINT_FRAC	BIT(10)
#define SUN8I_HDMI_PHY_PLL_CFG2_SDIV2		BIT(9)
#define SUN8I_HDMI_PHY_PLL_CFG2_S(x)		((x) << 6)
#define SUN8I_HDMI_PHY_PLL_CFG2_S6P25_7P5	BIT(5)
#define SUN8I_HDMI_PHY_PLL_CFG2_S5_7		BIT(4)
#define SUN8I_HDMI_PHY_PLL_CFG2_PREDIV_MSK	GENMASK(3, 0)
#define SUN8I_HDMI_PHY_PLL_CFG2_PREDIV_SHIFT	0
#define SUN8I_HDMI_PHY_PLL_CFG2_PREDIV(x)	(((x) - 1) << 0)

#define SUN8I_HDMI_PHY_PLL_CFG3_SOUT_DIV2	BIT(0)

#define SUN8I_HDMI_PHY_ANA_STS_B_OUT_SHIFT	11
#define SUN8I_HDMI_PHY_ANA_STS_B_OUT_MSK	GENMASK(16, 11)
#define SUN8I_HDMI_PHY_ANA_STS_RCALEND2D	BIT(7)
#define SUN8I_HDMI_PHY_ANA_STS_RCAL_MASK	GENMASK(5, 0)

struct sunxi_hdmi_phy {
	u32 dbg_ctrl;
	u32 rext_ctrl;
	u32 res1[2];
	u32 read_en;
	u32 unscramble;
	u32 res2[2];
	u32 ana_cfg1;
	u32 ana_cfg2;
	u32 ana_cfg3;
	u32 pll_cfg1;
	u32 pll_cfg2;
	u32 pll_cfg3;
	u32 ana_sts;
};

struct sunxi_dw_hdmi_phy_priv {
	void *base;
	struct clk clk_bus;
	struct clk clk_mod;
	uint rcal;
	struct reset_ctl reset;
};

void sunxi_dw_hdmi_phy_set(struct phy *_phy, const struct display_timing *edid,
			   int clk_div)
{
	struct sunxi_dw_hdmi_phy_priv *priv = dev_get_priv(_phy->dev);
	struct sunxi_hdmi_phy *phy = priv->base;
	u32 pll_cfg1, pll_cfg2, ana_cfg1, ana_cfg2, ana_cfg3;
	u32 tmp, b_offset = 0;

	/* bandwidth / frequency independent settings */

	pll_cfg1 = SUN8I_HDMI_PHY_PLL_CFG1_LDO2_EN |
		   SUN8I_HDMI_PHY_PLL_CFG1_LDO1_EN |
		   SUN8I_HDMI_PHY_PLL_CFG1_LDO_VSET(7) |
		   SUN8I_HDMI_PHY_PLL_CFG1_UNKNOWN(1) |
		   SUN8I_HDMI_PHY_PLL_CFG1_PLLDBEN |
		   SUN8I_HDMI_PHY_PLL_CFG1_CS |
		   SUN8I_HDMI_PHY_PLL_CFG1_CP_S(2) |
		   SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(63) |
		   SUN8I_HDMI_PHY_PLL_CFG1_BWS;

	pll_cfg2 = SUN8I_HDMI_PHY_PLL_CFG2_SV_H |
		   SUN8I_HDMI_PHY_PLL_CFG2_VCOGAIN_EN |
		   SUN8I_HDMI_PHY_PLL_CFG2_SDIV2;

	ana_cfg1 = SUN8I_HDMI_PHY_ANA_CFG1_REG_SVBH(1) |
		   SUN8I_HDMI_PHY_ANA_CFG1_AMP_OPT |
		   SUN8I_HDMI_PHY_ANA_CFG1_EMP_OPT |
		   SUN8I_HDMI_PHY_ANA_CFG1_AMPCK_OPT |
		   SUN8I_HDMI_PHY_ANA_CFG1_EMPCK_OPT |
		   SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL |
		   SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG |
		   SUN8I_HDMI_PHY_ANA_CFG1_REG_SCKTMDS |
		   SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN |
		   SUN8I_HDMI_PHY_ANA_CFG1_TXEN_ALL |
		   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK |
		   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2 |
		   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1 |
		   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0 |
		   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2 |
		   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1 |
		   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0 |
		   SUN8I_HDMI_PHY_ANA_CFG1_CKEN |
		   SUN8I_HDMI_PHY_ANA_CFG1_LDOEN |
		   SUN8I_HDMI_PHY_ANA_CFG1_ENVBS |
		   SUN8I_HDMI_PHY_ANA_CFG1_ENBI;

	ana_cfg2 = SUN8I_HDMI_PHY_ANA_CFG2_M_EN |
		   SUN8I_HDMI_PHY_ANA_CFG2_REG_DENCK |
		   SUN8I_HDMI_PHY_ANA_CFG2_REG_DEN |
		   SUN8I_HDMI_PHY_ANA_CFG2_REG_CKSS(1) |
		   SUN8I_HDMI_PHY_ANA_CFG2_REG_CSMPS(1);

	ana_cfg3 = SUN8I_HDMI_PHY_ANA_CFG3_REG_WIRE(0x3e0) |
		   SUN8I_HDMI_PHY_ANA_CFG3_SDAEN |
		   SUN8I_HDMI_PHY_ANA_CFG3_SCLEN;

	/* bandwidth / frequency dependent settings */
	if (edid->pixelclock.typ <= 27000000) {
		pll_cfg1 |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
			    SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2 |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
			    SUN8I_HDMI_PHY_PLL_CFG2_S(4);
		ana_cfg1 |= SUN8I_HDMI_PHY_ANA_CFG1_REG_CALSW;
		ana_cfg2 |= SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4) |
			    SUN8I_HDMI_PHY_ANA_CFG2_REG_RESDI(priv->rcal);
		ana_cfg3 |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(3) |
			    SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(5);
	} else if (edid->pixelclock.typ <= 74250000) {
		pll_cfg1 |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
			    SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2 |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
			    SUN8I_HDMI_PHY_PLL_CFG2_S(5);
		ana_cfg1 |= SUN8I_HDMI_PHY_ANA_CFG1_REG_CALSW;
		ana_cfg2 |= SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4) |
			    SUN8I_HDMI_PHY_ANA_CFG2_REG_RESDI(priv->rcal);
		ana_cfg3 |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(5) |
			    SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(7);
	} else if (edid->pixelclock.typ <= 148500000) {
		pll_cfg1 |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
			    SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2 |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
			    SUN8I_HDMI_PHY_PLL_CFG2_S(6);
		ana_cfg2 |= SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSWCK |
			    SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSW |
			    SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(2);
		ana_cfg3 |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(7) |
			    SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(9);
	} else {
		b_offset = 2;
		pll_cfg1 |= SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(63);
		pll_cfg2 |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(6) |
			    SUN8I_HDMI_PHY_PLL_CFG2_S(7);
		ana_cfg2 |= SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSWCK |
			    SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSW |
			    SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4);
		ana_cfg3 |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(9) |
			    SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(13) |
			    SUN8I_HDMI_PHY_ANA_CFG3_REG_EMP(3);
	}

	writel(pll_cfg1, &phy->pll_cfg1);
	writel(pll_cfg2 | (clk_div - 1), &phy->pll_cfg2);
	mdelay(10);
	writel(SUN8I_HDMI_PHY_PLL_CFG3_SOUT_DIV2, &phy->pll_cfg3);
	setbits_le32(&phy->pll_cfg1, SUN8I_HDMI_PHY_PLL_CFG1_PLLEN);
	mdelay(100);
	tmp = readl(&phy->ana_sts) & SUN8I_HDMI_PHY_ANA_STS_B_OUT_MSK;
	tmp >>= SUN8I_HDMI_PHY_ANA_STS_B_OUT_SHIFT;
	tmp = min(tmp + b_offset, (u32)0x3f);
	setbits_le32(&phy->pll_cfg1,
		     SUN8I_HDMI_PHY_PLL_CFG1_REG_OD1 |
		     SUN8I_HDMI_PHY_PLL_CFG1_REG_OD);
	setbits_le32(&phy->pll_cfg1, tmp);
	mdelay(100);
	writel(ana_cfg1, &phy->ana_cfg1);
	writel(ana_cfg2, &phy->ana_cfg2);
	writel(ana_cfg3, &phy->ana_cfg3);

	if (edid->flags & DISPLAY_FLAGS_VSYNC_LOW)
		setbits_le32(&phy->dbg_ctrl,
			     SUN8I_HDMI_PHY_DBG_CTRL_POL_NVSYNC);

	if (edid->flags & DISPLAY_FLAGS_HSYNC_LOW)
		setbits_le32(&phy->dbg_ctrl,
			     SUN8I_HDMI_PHY_DBG_CTRL_POL_NHSYNC);

	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_TXEN_ALL);
}

static int sunxi_dw_hdmi_phy_init(struct phy *phy)
{
	struct sunxi_dw_hdmi_phy_priv *priv = dev_get_priv(phy->dev);
	int ret;

	ret = reset_deassert(&priv->reset);
	if (ret)
		return ret;

	ret = clk_enable(&priv->clk_bus);
	if (ret)
		return ret;

	ret = clk_enable(&priv->clk_mod);
	if (ret)
		return ret;

	return 0;
}

static int sunxi_dw_hdmi_phy_power_on(struct phy *_phy)
{
	struct sunxi_dw_hdmi_phy_priv *priv = dev_get_priv(_phy->dev);
	struct sunxi_hdmi_phy *phy = priv->base;
	unsigned long tmo;

	/* enable read access to HDMI controller */
	writel(SUN8I_HDMI_PHY_READ_EN_MAGIC, &phy->read_en);
	/* descramble register offsets */
	writel(SUN8I_HDMI_PHY_UNSCRAMBLE_MAGIC, &phy->unscramble);

	writel(0, &phy->ana_cfg1);
	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_ENBI);
	udelay(5);
	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN);
	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_ENVBS);
	udelay(10);
	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_LDOEN);
	udelay(5);
	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_CKEN);
	udelay(40);
	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL);
	udelay(100);
	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG);
	setbits_le32(&phy->ana_cfg1,
		     SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0 |
		     SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1 |
		     SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2);

	/* Note that Allwinner code doesn't fail in case of timeout */
	tmo = timer_get_us() + 2000;
	while ((readl(&phy->ana_sts) & SUN8I_HDMI_PHY_ANA_STS_RCALEND2D) == 0) {
		if (timer_get_us() > tmo) {
			printf("Warning: HDMI PHY init timeout!\n");
			break;
		}
	}

	setbits_le32(&phy->ana_cfg1,
		     SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0 |
		     SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1 |
		     SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2 |
		     SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK);
	setbits_le32(&phy->ana_cfg1, SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDSCLK);

	/* enable DDC communication */
	writel(SUN8I_HDMI_PHY_ANA_CFG3_SCLEN |
	       SUN8I_HDMI_PHY_ANA_CFG3_SDAEN, &phy->ana_cfg3);

	priv->rcal = readl(&phy->ana_sts) & SUN8I_HDMI_PHY_ANA_STS_RCAL_MASK;
	priv->rcal >>= 2;

	return 0;
}

/*
 * This callback is abused for executing code after last register in
 * controller is set.
 */
static int sunxi_dw_hdmi_phy_exit(struct phy *_phy)
{
	struct sunxi_dw_hdmi_phy_priv *priv = dev_get_priv(_phy->dev);
	struct sunxi_hdmi_phy *phy = priv->base;

	writel(0, &phy->unscramble);

	return 0;
}

static struct phy_ops sunxi_dw_hdmi_phy_ops = {
	.init = sunxi_dw_hdmi_phy_init,
	.power_on = sunxi_dw_hdmi_phy_power_on,
	.exit = sunxi_dw_hdmi_phy_exit,
};

static int sunxi_dw_hdmi_phy_probe(struct udevice *dev)
{
	struct sunxi_dw_hdmi_phy_priv *priv = dev_get_priv(dev);
	int ret;

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -EINVAL;

	ret = reset_get_by_index(dev, 0, &priv->reset);
	if (ret)
		return ret;

	ret = clk_get_by_name(dev, "bus", &priv->clk_bus);
	if (ret)
		return ret;

	ret = clk_get_by_name(dev, "mod", &priv->clk_mod);
	if (ret)
		return ret;

	return 0;
}

static const struct udevice_id sunxi_dw_hdmi_phy_ids[] = {
	{ .compatible = "allwinner,sun8i-h3-hdmi-phy" },
	{ .compatible = "allwinner,sun50i-a64-hdmi-phy" },
	{ }
};

U_BOOT_DRIVER(sunxi_dw_hdmi_phy) = {
	.name		= "sunxi_dw_hdmi_phy",
	.id		= UCLASS_PHY,
	.of_match	= sunxi_dw_hdmi_phy_ids,
	.ops		= &sunxi_dw_hdmi_phy_ops,
	.probe		= sunxi_dw_hdmi_phy_probe,
	.priv_auto	= sizeof(struct sunxi_dw_hdmi_phy_priv),
};
