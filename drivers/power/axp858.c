// SPDX-License-Identifier: GPL-2.0+
/*
 * AXP858 driver
 *
 * (C) Copyright 2024 Jernej Skrabec <jernej.skrabec@gmail.com>
 */

#include <common.h>
#include <command.h>
#include <errno.h>
#include <asm/arch/pmic_bus.h>
#include <axp_pmic.h>

enum axp858_reg {
	AXP858_CHIP_VERSION	= 0x03,
	AXP858_OUTPUT_CTRL	= 0x10,
	AXP858_DCDC1_CTRL	= 0x13,
	AXP858_SHUTDOWN		= 0x32,
};

#define AXP858_CHIP_VERSION_MASK	0xcf
#define AXP858_CHIP_VERSION_AXP858	0x44

#define AXP858_POWEROFF			BIT(7)

static u8 mvolt_to_cfg(int mvolt, int min, int max, int div)
{
	if (mvolt < min)
		mvolt = min;
	else if (mvolt > max)
		mvolt = max;

	return (mvolt - min) / div;
}

static int axp_set_dcdc(int dcdc_num, unsigned int mvolt)
{
	int ret;
	u8 cfg, enable_mask = 1U << (dcdc_num - 1);
	int volt_reg = AXP858_DCDC1_CTRL + dcdc_num - 1;

	switch (dcdc_num) {
	case 2:
	case 3:
		if (mvolt > 1200)
			cfg = 71 + mvolt_to_cfg(mvolt, 1220, 1540, 20);
		else
			cfg = mvolt_to_cfg(mvolt, 500, 1200, 10);
		break;
	case 5:
		if (mvolt > 1120)
			cfg = 33 + mvolt_to_cfg(mvolt, 1140, 1840, 20);
		else
			cfg = mvolt_to_cfg(mvolt, 800, 1120, 10);
		break;
	default:
		return -EINVAL;
	}

	if (mvolt == 0)
		return pmic_bus_clrbits(AXP858_OUTPUT_CTRL, enable_mask);

	debug("DCDC%d: writing 0x%x to reg 0x%x\n", dcdc_num, cfg, volt_reg);
	ret = pmic_bus_write(volt_reg, cfg);
	if (ret)
		return ret;

	return pmic_bus_setbits(AXP858_OUTPUT_CTRL, enable_mask);
}

int axp_set_dcdc2(unsigned int mvolt)
{
	return axp_set_dcdc(2, mvolt);
}

int axp_set_dcdc3(unsigned int mvolt)
{
	return axp_set_dcdc(3, mvolt);
}

int axp_set_dcdc5(unsigned int mvolt)
{
	return axp_set_dcdc(5, mvolt);
}

int axp_init(void)
{
	u8 axp_chip_id;
	int ret;

	ret = pmic_bus_init();
	if (ret)
		return ret;

	ret = pmic_bus_read(AXP858_CHIP_VERSION, &axp_chip_id);
	if (ret)
		return ret;

	axp_chip_id &= AXP858_CHIP_VERSION_MASK;
	if (axp_chip_id != AXP858_CHIP_VERSION_AXP858) {
		debug("unknown PMIC: 0x%x\n", axp_chip_id);
		return -EINVAL;
	}

	return ret;
}

#if !CONFIG_IS_ENABLED(ARM_PSCI_FW) && !IS_ENABLED(CONFIG_SYSRESET_CMD_POWEROFF)
int do_poweroff(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	pmic_bus_write(AXP858_SHUTDOWN, AXP858_POWEROFF);

	/* infinite loop during shutdown */
	while (1) {}

	/* not reached */
	return 0;
}
#endif
