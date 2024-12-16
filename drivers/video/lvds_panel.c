// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2024 Jernej Skrabec <jernej.skrabec@gmail.com>
 */

#include <backlight.h>
#include <dm.h>
#include <log.h>
#include <panel.h>
#include <power/regulator.h>

struct lvds_panel_priv {
	struct udevice *reg;
	struct udevice *backlight;
};

static int lvds_panel_enable_backlight(struct udevice *dev)
{
	struct lvds_panel_priv *priv = dev_get_priv(dev);
	int ret;

	debug("%s: start, backlight = '%s'\n", __func__, priv->backlight->name);
	ret = backlight_enable(priv->backlight);
	debug("%s: done, ret = %d\n", __func__, ret);
	if (ret)
		return ret;

	return 0;
}

static int lvds_panel_set_backlight(struct udevice *dev, int percent)
{
	struct lvds_panel_priv *priv = dev_get_priv(dev);
	int ret;

	debug("%s: start, backlight = '%s'\n", __func__, priv->backlight->name);
	ret = backlight_set_brightness(priv->backlight, percent);
	debug("%s: done, ret = %d\n", __func__, ret);
	if (ret)
		return ret;

	return 0;
}

static int lvds_panel_get_display_timing(struct udevice *dev,
					 struct display_timing *timings)
{
	return ofnode_decode_panel_timing(dev_ofnode(dev), timings);
}

static int lvds_panel_of_to_plat(struct udevice *dev)
{
	struct lvds_panel_priv *priv = dev_get_priv(dev);
	int ret;

	if (CONFIG_IS_ENABLED(DM_REGULATOR)) {
		ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
						   "power-supply", &priv->reg);
		if (ret) {
			debug("%s: Warning: cannot get power supply: ret=%d\n",
			      __func__, ret);
			if (ret != -ENOENT)
				return ret;
		}
	}

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
					   "backlight", &priv->backlight);
	if (ret) {
		debug("%s: Cannot get backlight: ret=%d\n", __func__, ret);
		if (ret != -ENOENT)
			return log_ret(ret);
	}

	return 0;
}

static int lvds_panel_probe(struct udevice *dev)
{
	struct lvds_panel_priv *priv = dev_get_priv(dev);
	int ret;

	ret = regulator_set_enable_if_allowed(priv->reg, true);
	if (ret && ret != -ENOSYS) {
		debug("%s: failed to enable regulator '%s' %d\n",
		      __func__, priv->reg->name, ret);
		return ret;
	}

	return 0;
}

static const struct panel_ops lvds_panel_ops = {
	.enable_backlight	= lvds_panel_enable_backlight,
	.set_backlight		= lvds_panel_set_backlight,
	.get_display_timing	= lvds_panel_get_display_timing,
};

static const struct udevice_id lvds_panel_ids[] = {
	{ .compatible = "panel-lvds" },
	{ }
};

U_BOOT_DRIVER(lvds_panel) = {
	.name		= "lvds_panel",
	.id		= UCLASS_PANEL,
	.of_match	= lvds_panel_ids,
	.ops		= &lvds_panel_ops,
	.of_to_plat	= lvds_panel_of_to_plat,
	.probe		= lvds_panel_probe,
	.priv_auto	= sizeof(struct lvds_panel_priv),
};
