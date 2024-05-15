// SPDX-License-Identifier: GPL-2.0+
/*
 * Helpers that are commonly used with DW memory controller.
 *
 * (C) Copyright 2025 Jernej Skrabec <jernej.skrabec@gmail.com>
 *
 */

#include <init.h>
#include <asm/arch/dram_dw_helpers.h>

void mctl_auto_detect_rank_width(const struct dram_para *para,
				 struct dram_config *config)
{
	/* this is minimum size that it's supported */
	config->cols = 8;
	config->rows = 13;
	if (para->type == SUNXI_DRAM_TYPE_DDR4) {
		config->banks = 2;
		config->bank_groups = 1;
	} else {
		config->banks = 3;
		config->bank_groups = 0;
	}

	/*
	 * Strategy here is to test most demanding combination first and least
	 * demanding last, otherwise HW might not be fully utilized. For
	 * example, half bus width and rank = 1 combination would also work
	 * on HW with full bus width and rank = 2, but only 1/4 RAM would be
	 * visible.
	 */

	debug("testing 32-bit width, rank = 2\n");
	config->bus_full_width = 1;
	config->ranks = 2;
	if (mctl_core_init(para, config))
		return;

	debug("testing 32-bit width, rank = 1\n");
	config->bus_full_width = 1;
	config->ranks = 1;
	if (mctl_core_init(para, config))
		return;

	debug("testing 16-bit width, rank = 2\n");
	config->bus_full_width = 0;
	config->ranks = 2;
	if (mctl_core_init(para, config))
		return;

	debug("testing 16-bit width, rank = 1\n");
	config->bus_full_width = 0;
	config->ranks = 1;
	if (mctl_core_init(para, config))
		return;

	panic("This DRAM setup is currently not supported.\n");
}

static void mctl_write_pattern(void)
{
	unsigned int i;
	u32 *ptr, val;

	ptr = (u32 *)CFG_SYS_SDRAM_BASE;
	for (i = 0; i < 16; ptr++, i++) {
		if (i & 1)
			val = ~(ulong)ptr;
		else
			val = (ulong)ptr;
		writel(val, ptr);
	}
}

static bool mctl_check_pattern(ulong offset)
{
	unsigned int i;
	u32 *ptr, val;

	ptr = (u32 *)CFG_SYS_SDRAM_BASE;
	for (i = 0; i < 16; ptr++, i++) {
		if (i & 1)
			val = ~(ulong)ptr;
		else
			val = (ulong)ptr;
		if (val != *(ptr + offset / 4))
			return false;
	}

	return true;
}

void mctl_auto_detect_dram_size(const struct dram_para *para,
				struct dram_config *config)
{
	unsigned int shift, cols, rows;
	u32 buffer[16];

	/* max. config for columns, banks and bank groups, but not rows */
	config->cols = 11;
	config->rows = 13;
	config->banks = 3;
	config->bank_groups = para->type == SUNXI_DRAM_TYPE_DDR4 ? 2 : 0;
	mctl_core_init(para, config);

	/*
	 * Store content so it can be restored later. This is important
	 * if controller was already initialized and holds any data
	 * which is important for restoring system.
	 */
	memcpy(buffer, (u32 *)CFG_SYS_SDRAM_BASE, sizeof(buffer));

	mctl_write_pattern();

	shift = config->bus_full_width + 1;

	/* detect column address bits */
	for (cols = 8; cols < 11; cols++) {
		if (mctl_check_pattern(1ULL << (cols + shift)))
			break;
	}
	debug("detected %u columns\n", cols);

	if (para->type == SUNXI_DRAM_TYPE_DDR4) {
		/* detect number of bank groups */
		for (config->bank_groups = 1; config->bank_groups < 4; config->bank_groups++) {
			if (mctl_mem_matches(1ULL << (config->bank_groups + 5)))
				break;
		}
		if (config->bank_groups == 3)
			config->bank_groups = 0;
		debug("detected %u bank groups\n", config->bank_groups);

		/* detect number of banks */
		if (mctl_mem_matches(1ULL << (shift + 13)))
			config->banks = 2;
		debug("detected %u banks\n", config->banks);
	}

	/* restore data */
	memcpy((u32 *)CFG_SYS_SDRAM_BASE, buffer, sizeof(buffer));

	/* reconfigure to make sure that all active rows are accessible */
	config->cols = 8;
	config->rows = 17;
	mctl_core_init(para, config);

	/* store data again as it might be moved */
	memcpy(buffer, (u32 *)CFG_SYS_SDRAM_BASE, sizeof(buffer));

	mctl_write_pattern();

	/* detect row address bits */
	shift = config->bus_full_width + 1 + config->bank_groups +
		config->cols + config->banks;
	for (rows = 13; rows < 17; rows++) {
		if (mctl_check_pattern(1ULL << (rows + shift)))
			break;
	}
	debug("detected %u rows\n", rows);

	/* restore data again */
	memcpy((u32 *)CFG_SYS_SDRAM_BASE, buffer, sizeof(buffer));

	config->cols = cols;
	config->rows = rows;
}

unsigned long mctl_calc_size(const struct dram_config *config)
{
	unsigned int shift;

	shift = config->cols + config->rows +
		config->banks + config->bank_groups +
		config->bus_full_width + 1 +
		config->ranks - 1;

	return 1ULL << shift;
}
