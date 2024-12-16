// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner DE3 display driver
 *
 * (C) Copyright 2024 Jernej Skrabec <jernej.skrabec@gmail.com>
 */
#define DEBUG 1
#include <display.h>
#include <dm.h>
#include <edid.h>
#include <efi_loader.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <log.h>
#include <part.h>
#include <video.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/display3.h>
#include <linux/bitops.h>

DECLARE_GLOBAL_DATA_PTR;

enum {
	/* Maximum LCD size we support */
	LCD_MAX_WIDTH		= 3840,
	LCD_MAX_HEIGHT		= 2160,
	LCD_MAX_LOG2_BPP	= VIDEO_BPP32,
};

static void sunxi_de3_composer_init(void)
{
	struct sunxi_ccm_reg * const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
	//u32 reg_value;

	/* set SRAM for video use */
	//reg_value = readl(SUNXI_SRAMC_BASE + 0x04);
	//reg_value &= ~(0x01 << 24);
	//writel(reg_value, SUNXI_SRAMC_BASE + 0x04);
	writel(0, SUNXI_SRAMC_BASE + 0x00);
	writel(0, SUNXI_SRAMC_BASE + 0x04);

	clock_set_pll10(600000000);

	/* Set DE parent to pll10 */
	clrsetbits_le32(&ccm->de_clk_cfg, CCM_DE3_CTRL_PLL_MASK,
			CCM_DE3_CTRL_PLL10);

	/* Set ahb gating to pass */
	writel(BIT(RESET_SHIFT) | BIT(GATE_SHIFT), &ccm->de_gate_reset);

	/* Clock on */
	setbits_le32(&ccm->de_clk_cfg, CCM_DE3_CTRL_GATE);
}

static void sunxi_de3_mode_set(int mux, const struct display_timing *mode,
			       int bpp, ulong address)
{
	ulong de_mux_base = (mux == 0) ?
			    SUNXI_DE3_MUX0_BASE : SUNXI_DE3_MUX1_BASE;
	struct de_clk * const de_clk_regs =
		(struct de_clk *)(SUNXI_DE3_BASE + 0x8000);
	struct de_glb * const de_glb_regs =
		(struct de_glb *)(SUNXI_DE3_MUX_GLB_REGS);
	struct de_bld * const de_bld_regs =
		(struct de_bld *)(SUNXI_DE3_MUX_BLD_REGS);
	struct de_ui * const de_ui_regs =
		(struct de_ui *)(de_mux_base +
				 SUNXI_DE3_MUX_CHAN_REGS +
				 SUNXI_DE3_MUX_CHAN_SZ * 6);
	u32 size = SUNXI_DE3_WH(mode->hactive.typ, mode->vactive.typ);
	u32 format;

	/* enable clock */
	setbits_le32(&de_clk_regs->gate_cfg, BIT(mux));
	setbits_le32(&de_clk_regs->bus_cfg, BIT(mux));
	writel(1, &de_clk_regs->mbus_cfg);

	writel(SUNXI_DE3_MUX_GLB_CTL_EN, &de_glb_regs->ctl);
	writel(1, &de_glb_regs->clk);
	writel(size, &de_glb_regs->size);

	writel(0, &de_clk_regs->reg0);
	writel(0xA980, &de_clk_regs->reg1);

	switch (bpp) {
	case 16:
		format = SUNXI_DE3_UI_CFG_ATTR_FMT(SUNXI_DE3_FORMAT_RGB_565);
		break;
	case 32:
	default:
		format = SUNXI_DE3_UI_CFG_ATTR_FMT(SUNXI_DE3_FORMAT_XRGB_8888);
		break;
	}

	writel(size, &de_ui_regs->ovl_size);
	writel(0, &de_ui_regs->top_haddr);
	writel(0, &de_ui_regs->bot_haddr);
	//writel(0xffff0000, &de_ui_regs->cfg[0].fcolor);
	writel(size, &de_ui_regs->cfg[0].size);
	writel(0, &de_ui_regs->cfg[0].coord);
	writel((bpp / 8) * mode->hactive.typ, &de_ui_regs->cfg[0].pitch);
	writel(address, &de_ui_regs->cfg[0].top_laddr);
	writel(0xff000000 | SUNXI_DE3_UI_CFG_ATTR_EN | format /*| BIT(4)*/, &de_ui_regs->cfg[0].attr);
	//writel(1, &de_glb_regs->dbuff);

	memset(de_bld_regs, 0, sizeof(struct de_bld));

	writel(0xff000000, &de_bld_regs->bkcolor);

	writel(0x03010301, &de_bld_regs->bld_mode[0]);

	writel(size, &de_bld_regs->output_size);
	//writel(mode->flags & DISPLAY_FLAGS_INTERLACED ? 2 : 0,
	//       &de_bld_regs->out_ctl);
	writel(0, &de_bld_regs->out_ctl);

	writel(0xff000000, &de_bld_regs->attr[0].fcolor);
	writel(size, &de_bld_regs->attr[0].insize);
	writel(0, &de_bld_regs->attr[0].incoord);

	writel(0x00000101, &de_bld_regs->fcolor_ctl);

	writel(1, &de_bld_regs->route);
}

static int sunxi_de3_init(struct udevice *dev, ulong fbbase,
			  enum video_log2_bpp l2bpp,
			  struct udevice *disp, int mux)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct display_timing timing;
	struct display_plat *disp_uc_plat;
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

	sunxi_de3_composer_init();
	sunxi_de3_mode_set(mux, &timing, 1 << l2bpp, fbbase);

	ret = display_enable(disp, 1 << l2bpp, &timing);
	if (ret) {
		debug("%s: Failed to enable display\n", __func__);
		return ret;
	}

	uc_priv->xsize = timing.hactive.typ;
	uc_priv->ysize = timing.vactive.typ;
	uc_priv->bpix = l2bpp;
	debug("fb=%lx, size=%d %d\n", fbbase, uc_priv->xsize, uc_priv->ysize);

	return 0;
}

static int sunxi_de3_probe(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct udevice *disp;
	int ret;

	/* Before relocation we don't need to do anything */
	if (!(gd->flags & GD_FLG_RELOC))
		return 0;

	ret = uclass_get_device_by_driver(UCLASS_DISPLAY,
					  DM_DRIVER_GET(sunxi_lcd), &disp);
	if (!ret) {
		ret = sunxi_de3_init(dev, plat->base, VIDEO_BPP32, disp, 0);
		if (!ret) {
			video_set_flush_dcache(dev, 1);
			return 0;
		}
	}

	debug("%s: lcd display not found (ret=%d)\n", __func__, ret);

	return -ENODEV;
}

static int sunxi_de3_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->size = LCD_MAX_WIDTH * LCD_MAX_HEIGHT *
		(1 << LCD_MAX_LOG2_BPP) / 8;

	return 0;
}

static const struct video_ops sunxi_de3_ops = {
};

U_BOOT_DRIVER(sunxi_de3) = {
	.name	= "sunxi_de3",
	.id	= UCLASS_VIDEO,
	.ops	= &sunxi_de3_ops,
	.bind	= sunxi_de3_bind,
	.probe	= sunxi_de3_probe,
	.flags	= DM_FLAG_PRE_RELOC,
};

U_BOOT_DRVINFO(sunxi_de3) = {
	.name = "sunxi_de3"
};
