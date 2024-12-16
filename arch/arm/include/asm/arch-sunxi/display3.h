/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sunxi platform display controller register and constant defines
 *
 * (C) Copyright 2024 Jernej Skrabec <jernej.skrabec@gmail.com>
 *
 */

#ifndef _SUNXI_DISPLAY3_H
#define _SUNXI_DISPLAY3_H

/* internal clock settings */
struct de_clk {
	u32 gate_cfg; /* 0x00 */
	u32 bus_cfg;  /* 0x04 */
	u32 mbus_cfg; /* 0x08 */
	u32 res0;     /* 0x0c */
	u32 res1;     /* 0x10 */
	u32 res2;     /* 0x14 */
	u32 res3;     /* 0x18 */
	u32 res4;     /* 0x1c */
	u32 res5;     /* 0x20 */
	u32 reg0;     /* 0x24 */
	u32 reg1;     /* 0x28 */
};

check_member(de_clk, reg0, 0x24);

/* global control */
struct de_glb {
	u32 ctl;
	u32 status;
	u32 size;
	u32 clk;
};

/* alpha blending */
struct de_bld {
	u32 fcolor_ctl;		/* 0x00 */
	struct {
		u32 fcolor;	/* 0x04 */
		u32 insize;	/* 0x08 */
		u32 incoord;	/* 0x0c */
		u32 dum;	/* 0x10 */
	} attr[6];
	u32 dum0[7];		/* 0x64 */
	u32 route;		/* 0x80 */
	u32 premultiply;	/* 0x84 */
	u32 bkcolor;		/* 0x88 */
	u32 output_size;	/* 0x8c */
	u32 bld_mode[5];	/* 0x90 */
	u32 dum1[3];		/* 0xa4 */
	u32 ck_ctl;		/* 0xb0 */
	u32 ck_cfg0;		/* 0xb4 */
	u32 ck_cfg1;		/* 0xb8 */
	u32 dum2;		/* 0xbc */
	u32 ck_max[5];		/* 0xc0 */
	u32 dum3[3];		/* 0xd4 */
	u32 ck_min[5];		/* 0xe0 */
	u32 dum4[2];		/* 0xf4 */
	u32 out_ctl;		/* 0xfc */
};

check_member(de_bld, route,   0x80);
check_member(de_bld, out_ctl, 0xfc);

struct de_ui {
	struct {
		u32 attr;
		u32 size;
		u32 coord;
		u32 pitch;
		u32 top_laddr;
		u32 bot_laddr;
		u32 fcolor;
		u32 dum;
	} cfg[4];
	u32 top_haddr;
	u32 bot_haddr;
	u32 ovl_size;
};

/*
 * DE register constants.
 */
#define SUNXI_DE3_MUX0_BASE			(SUNXI_DE3_BASE + 0x100000)
#define SUNXI_DE3_MUX1_BASE			(SUNXI_DE3_BASE + 0x200000)

#define SUNXI_DE3_MUX_GLB_REGS			(SUNXI_DE3_BASE + 0x8100)
#define SUNXI_DE3_MUX_BLD_REGS			(SUNXI_DE3_BASE + 0x281000)
#define SUNXI_DE3_MUX_CHAN_REGS			0x01000
#define SUNXI_DE3_MUX_CHAN_SZ			0x20000

#define SUNXI_DE3_FORMAT_XRGB_8888		4
#define SUNXI_DE3_FORMAT_RGB_565		10

#define SUNXI_DE3_MUX_GLB_CTL_EN		(1 << 0)
#define SUNXI_DE3_UI_CFG_ATTR_EN		(1 << 0)
#define SUNXI_DE3_UI_CFG_ATTR_FMT(f)		((f & 0xf) << 8)

#define SUNXI_DE3_WH(w, h)			(((h - 1) << 16) | (w - 1))

#endif /* _SUNXI_DISPLAY3_H */
