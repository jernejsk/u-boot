// SPDX-License-Identifier: GPL-2.0
// Copyright 2024 John Watts <contact@jookia.org>
/*
 * The Allwinner D1's PWM channels are paired 16-bit counters.
 *
 * Each channel must be programmed using three variables:
 * - The entire cycle count (used for the period)
 * - The active cycle count (used for the duty cycle)
 * - The active state polarity (specifies if the signal goes high or low)
 *
 * All counts are zero based, but the datasheet spends a lot of time
 * adding 1 to the entire cycle count. There's no hidden extra cycle,
 * it's just trying to make it human understandable. After all, you can
 * have 0 active counts for a 100% duty cycle, but 0 entire cycles
 * doesn't really mean sense or work in time calculations.
 *
 * The counter works like this (quoting the datasheet):
 * - PCNTR = (PCNTR == PWM_ENTIRE_CYCLE) ? 0 : PCNTR + 1
 * - PCNTR > (PWM_ENTIRE_CYCLE - PWM_ACT_CYCLE) = Output active state
 * - PCNTR <= (PWM_ENTIRE_CYCLE - PWM_ACT_CYCLE) = Output inactive state
 *
 * Here's a 2-bit table of cycle counts versus active cycle counts:
 *   Active:  |       0 |        1  |        2  |        3 |
 *   Count 0  | Active  | Inactive  | Inactive  | Inactive |
 *   Count 1  | Active  | Active    | Inactive  | Inactive |
 *   Count 2  | Active  | Active    | Active    | Inactive |
 *   Count 3  | Active  | Active    | Active    | Active   |
 *
 * An entire count of 2 and active count of 3 would always be inactive.
 *
 * The main takeaways here for us are:
 * - The counter wraps around when it hits the entire cycle count
 * - The output is active after the counter equals the active cycle count
 * - An active count of 0 means the period is a 100% active cycle
 * - An active count larger than the entire cycle count is a 0% active cycle
 *
 * This driver deals with the last problem by limiting the entire cycles
 * to 65534 so we can always specify 65535 for a 0% active cycle.
 *
 * The PWM channels are paired and clocked together, resulting in a
 * cycle time found using the following formula:
 *
 * PWM0_CYCLE_NS = 1000000000 / (BUS_CLOCK / COMMON_DIV / PWM0_PRESCALER_K)
 * PWM1_CYCLE_NS = 1000000000 / (BUS_CLOCK / COMMON_DIV / PWM1_PRESCALER_K)
 *
 * This means both clocks should ideally be set at the same time and not
 * impact each other too much.
 */

#include <dm.h>
#include <dm/device_compat.h>
#include <dm/devres.h>
#include <clk.h>
#include <reset.h>
#include <pwm.h>
#include <asm/io.h>

/* PWM channel information */
struct pwm_channel {
	uint period_ns;
	uint duty_ns;
	bool polarity;
	bool enable;
	bool updated;
};

/* Timings found for a PWM channel */
struct pwm_timings {
	uint cycle_ns;
	uint period_ns;
	uint duty_ns;
	uint clock_id;
	uint common_div;
	uint prescale_k;
	uint entire_cycles;
	uint active_cycles;
	uint polarity;
};

/* Driver state */
struct sunxi_pwm_h616_priv {
	void *base;
	struct clk *clk_bus;
	struct clk *clk_srcs[3]; /* Last value must be NULL */
	struct reset_ctl *reset;
	int npwm;
	struct pwm_channel *channels;
};

/* Divides a nanosecond value, rounding up for very low values */
uint div_ns(uint ns, uint div)
{
	uint result = (ns / div);

	/* If the number is less than 1000, round it to the nearest digit */
	if (result < 1000)
		result = (ns + (div - 1)) / div;

	if (result < 1)
		result = 1;

	return result;
}

/* Checks if an error is relatively too large */
int error_too_large(uint actual, uint target)
{
	/* For a target of zero we want zero */
	if (target == 0)
		return (actual == 0);

	/* Don't overflow large numbers when we multiply by 100 */
	while (actual > 1000) {
		actual /= 100;
		target /= 100;
	}

	int error_percent = (actual * 100) / target;

	return (error_percent < 80 || 120 < error_percent);
}

/* Calculates the cycle nanoseconds from clock parameters */
int get_cycle_ns(uint parent_hz, uint common_div, uint prescaler)
{
	return 1000000000 / ((parent_hz / common_div) / prescaler);
}

int find_channel_dividers(uint period_ns,
			  uint parent_hz,
			  struct pwm_timings *out)
{
	uint ideal_cycle_ns = div_ns(period_ns, 65535);
	int common_div = out->common_div;
	int prescaler = 1;
	uint cycle_ns = 0;

	for (;;) {
		cycle_ns = get_cycle_ns(parent_hz, common_div, prescaler);
		if (cycle_ns >= ideal_cycle_ns)
			break;

		prescaler *= 2;
		if (prescaler > 256) {
			if (common_div < 256) {
				prescaler = 1;
				common_div *= 2;
			} else {
				return -1;
			}
		}
	}

	out->common_div = common_div;
	out->prescale_k = prescaler;
	out->cycle_ns = cycle_ns;

	return 0;
}

int find_channel_timings(const struct pwm_channel *in,
			 struct pwm_timings *out,
			 uint parent_hz)
{
	struct pwm_timings new = *out;

	if (find_channel_dividers(in->period_ns, parent_hz, &new))
		return -1;

	new.entire_cycles = (in->period_ns / new.cycle_ns);
	new.active_cycles = (in->duty_ns / new.cycle_ns);
	new.period_ns = (new.entire_cycles * new.cycle_ns);
	new.duty_ns = (new.active_cycles * new.cycle_ns);
	new.polarity = in->polarity;

	if (error_too_large(new.period_ns, in->period_ns))
		return -1;

	if (in->duty_ns && error_too_large(new.duty_ns, in->duty_ns))
		return -1;

	*out = new;

	return 0;
}

int find_pair_timings(const struct pwm_channel *channel0,
		      const struct pwm_channel *channel1,
		      struct pwm_timings *timings0,
		      struct pwm_timings *timings1,
		      int clock_hz)
{
	struct pwm_timings new0 = *timings0;
	struct pwm_timings new1 = *timings1;
	int err0 = 0;
	int err1 = 0;

	new0.common_div = 1;
	new1.common_div = 1;

	if (channel0->enable) {
		err0 = find_channel_timings(channel0, &new0, clock_hz);
		new1.common_div = new0.common_div;
	}

	if (channel1->enable) {
		err1 = find_channel_timings(channel1, &new1, clock_hz);
		new0 = *timings0;
		new0.common_div = new1.common_div;
	}

	if (channel0->enable && channel1->enable) {
		err0 = find_channel_timings(channel0, &new0, clock_hz);

		if (new0.common_div != new1.common_div)
			return -1;
	}

	if (err0 || err1)
		return -1;

	*timings0 = new0;
	*timings1 = new1;

	return 0;
}

int find_pair_timings_clocked(struct clk **clk_srcs,
			      const struct pwm_channel *channel0,
			      const struct pwm_channel *channel1,
			      struct pwm_timings *timings0,
			      struct pwm_timings *timings1)
{
	struct clk *clock = *clk_srcs;

	for (int clock_id = 0; clock; clock = clk_srcs[++clock_id]) {
		int clock_hz = clk_get_rate(clock);

		if (clock_hz == 0 || IS_ERR_VALUE(clock_hz))
			continue;

		timings0->clock_id = clock_id;
		timings1->clock_id = clock_id;

		if (find_pair_timings(channel0, channel1,
				      timings0, timings1,
				      clock_hz))
			continue;

		return 0;
	}

	return -1;
}

#define PCGR(base, channel) ((base) + 0x20 + ((channel) / 2) * 4)
#define PCGR_CLK_GATE BIT(4)

#define PER(base) ((base) + 0x40)
#define PER_ENABLE_PWM(channel) BIT(channel)

#define PCCR(base, pair) ((base) + 0x20 + ((pair) * 2))
#define PCCR_CLK_SRC(src) ((src) << 7)
#define PCCR_CLK_SRC_MASK GENMASK(8, 7)
#define PCCR_CLK_DIV_M(m) (m)
#define PCCR_CLK_DIV_M_MASK GENMASK(3, 0)

#define PCR(base, channel) ((base) + 0x60 + ((channel) * 0x20))
#define PCR_PRESCAL_K(k) (k)
#define PCR_PRESCAL_K_MASK GENMASK(7, 0)
#define PCR_PWM_ACTIVE BIT(8)

#define PPR(base, channel) ((base) + 0x64 + ((channel) * 0x20))
#define PPR_ENTIRE_CYCLE(n) ((n) << 16)
#define PPR_ENTIRE_CYCLE_MASK GENMASK(31, 16)
#define PPR_ACT_CYCLE(n) (n)
#define PPR_ACT_CYCLE_MASK GENMASK(15, 0)

/* Like clrsetbits_le32 but with memory barriers */
void clrsetreg(void *addr, u32 clear, u32 set)
{
	u32 val = readl(addr);

	val &= ~clear;
	val |= set;

	writel(val, addr);
}

void disable_pair(void *base, int pair)
{
	u32 PER_clear = (PER_ENABLE_PWM(pair) | PER_ENABLE_PWM(pair + 1));

	clrsetreg(PER(base), PER_clear, 0);
	clrsetreg(PCGR(base, pair), PCGR_CLK_GATE, 0);
}

void enable_pair(void *base, int pair, int clk_src, int clk_div)
{
	int div_m = fls(clk_div) - 1;

	u32 PCCR_clear = (PCCR_CLK_SRC_MASK | PCCR_CLK_DIV_M_MASK);
	u32 PCCR_set = (PCCR_CLK_SRC(clk_src) | PCCR_CLK_DIV_M(div_m));

	clrsetreg(PCGR(base, pair), 0, PCGR_CLK_GATE);
	clrsetreg(PCCR(base, pair), PCCR_clear, PCCR_set);

	log_debug("%s: pair %i, clk_src %i, div_m %i, PCCR 0x%x\n",
		  __func__, pair, clk_src, div_m, readl(PCCR(base, pair)));
}

void enable_channel(void *base, int channel, struct pwm_timings *timings)
{
	u32 pwm_active = (timings->polarity) ? 0 : PCR_PWM_ACTIVE;
	u32 prescale = (timings->prescale_k - 1);
	u32 entire_cycles = (timings->entire_cycles - 1);
	u32 active_cycles = timings->active_cycles;

	u32 PCR_clear = (PCR_PRESCAL_K_MASK | PCR_PWM_ACTIVE);
	u32 PCR_set = (PCR_PRESCAL_K(prescale) | pwm_active);
	u32 PPR_clear = (PPR_ENTIRE_CYCLE_MASK | PPR_ACT_CYCLE_MASK);
	u32 PPR_set = (PPR_ENTIRE_CYCLE(entire_cycles) | PPR_ACT_CYCLE(active_cycles));
	u32 PER_set = PER_ENABLE_PWM(channel);

	clrsetreg(PCR(base, channel), PCR_clear, PCR_set);
	clrsetreg(PPR(base, channel), PPR_clear, PPR_set);
	clrsetreg(PER(base), 0, PER_set);

	log_debug("%s: channel %u, clock_id %u, period_ns %u, duty_ns %u, common_div %u, prescale_k %u, entire_cycles %u, active_cycles %u, polarity %u, PCGR 0x%x, PCR 0x%x, PPR 0x%x, PER 0x%x\n",
		  __func__, channel, timings->clock_id, timings->period_ns,
		  timings->duty_ns, timings->common_div, timings->prescale_k,
		  timings->entire_cycles, timings->active_cycles,
		  timings->polarity, readl(PCGR(base, channel)),
		  readl(PCR(base, channel)), readl(PPR(base, channel)),
		  readl(PER(base)));
}

int update_channel_pair(struct sunxi_pwm_h616_priv *priv, int pair)
{
	struct pwm_timings timings0 = {0};
	struct pwm_timings timings1 = {0};
	struct pwm_channel *channel0 = &priv->channels[pair + 0];
	struct pwm_channel *channel1 = &priv->channels[pair + 1];
	void *base = priv->base;

	if (channel0->updated && channel1->updated)
		return 0;

	disable_pair(base, pair);

	if (find_pair_timings_clocked(priv->clk_srcs, channel0, channel1, &timings0, &timings1))
		return -1;

	if (channel0->enable || channel1->enable)
		enable_pair(base, pair, timings0.clock_id, timings0.common_div);

	if (channel0->enable)
		enable_channel(base, pair + 0, &timings0);

	if (channel1->enable)
		enable_channel(base, pair + 1, &timings1);

	channel0->updated = true;
	channel1->updated = true;

	return 0;
}

static int update_channels(struct udevice *dev)
{
	struct sunxi_pwm_h616_priv *priv = dev_get_priv(dev);
	int i;

	for (i = 0; i < priv->npwm; i += 2) {
		int ret = update_channel_pair(priv, i);

		if (ret != 0)
			return ret;
	}

	return 0;
}

static int sunxi_pwm_h616_set_invert(struct udevice *dev, uint channel_num,
				   bool polarity)
{
	struct sunxi_pwm_h616_priv *priv = dev_get_priv(dev);
	struct pwm_channel *channel;

	if (channel_num >= priv->npwm)
		return -EINVAL;

	channel = &priv->channels[channel_num];
	channel->updated = (channel->polarity == polarity);
	channel->polarity = polarity;

	return update_channels(dev);
}

static int sunxi_pwm_h616_set_config(struct udevice *dev, uint channel_num,
				   uint period_ns, uint duty_ns)
{
	struct sunxi_pwm_h616_priv *priv = dev_get_priv(dev);
	struct pwm_channel *channel;

	if (channel_num >= priv->npwm)
		return -EINVAL;

	channel = &priv->channels[channel_num];
	channel->updated = (channel->period_ns == period_ns && channel->duty_ns == duty_ns);
	channel->period_ns = period_ns;
	channel->duty_ns = duty_ns;

	return update_channels(dev);
}

static int sunxi_pwm_h616_set_enable(struct udevice *dev, uint channel_num, bool enable)
{
	struct sunxi_pwm_h616_priv *priv = dev_get_priv(dev);
	struct pwm_channel *channel;

	if (channel_num >= priv->npwm)
		return -EINVAL;

	channel = &priv->channels[channel_num];
	channel->updated = (channel->enable == enable);
	channel->enable = enable;

	return update_channels(dev);
}

static int sunxi_pwm_h616_of_to_plat(struct udevice *dev)
{
	struct sunxi_pwm_h616_priv *priv = dev_get_priv(dev);
	struct clk *clk_hosc;
	struct clk *clk_apb;
	int ret;

	priv->base = dev_read_addr_ptr(dev);

	if (!priv->base)  {
		dev_err(dev, "Unset device tree offset?\n");
		return -EINVAL;
	}

	priv->clk_bus = devm_clk_get(dev, "bus");

	if (IS_ERR(priv->clk_bus)) {
		dev_err(dev, "failed to get bus clock: %ld",
			PTR_ERR(priv->clk_bus));
		return PTR_ERR(priv->clk_bus);
	}

	ret = clk_enable(priv->clk_bus);

	if (ret) {
		dev_err(dev, "failed to enable bus clk: %d", ret);
		return ret;
	}

	clk_hosc = devm_clk_get(dev, "hosc");

	if (IS_ERR(clk_hosc)) {
		dev_err(dev, "failed to get hosc clock: %ld",
			PTR_ERR(clk_hosc));
		return PTR_ERR(clk_hosc);
	}

	clk_apb = devm_clk_get(dev, "apb0");

	if (IS_ERR(clk_apb)) {
		dev_err(dev, "failed to get apb clock: %ld",
			PTR_ERR(clk_apb));
		return PTR_ERR(clk_apb);
	}

	priv->clk_srcs[0] = clk_hosc;
	priv->clk_srcs[1] = clk_apb;
	priv->clk_srcs[2] = NULL;

	priv->reset = devm_reset_control_get(dev, NULL);

	if (IS_ERR(priv->reset)) {
		dev_err(dev, "failed to get reset: %ld",
			PTR_ERR(priv->reset));
		return PTR_ERR(priv->reset);
	}

	priv->npwm = 6;

	priv->channels = devm_kzalloc(dev,
				      sizeof(struct pwm_channel) * priv->npwm,
				      GFP_KERNEL);

	if (!priv->channels) {
		dev_err(dev, "failed to read allocate pwm channels");
		return -ENOMEM;
	}

	return 0;
}

static int sunxi_pwm_h616_probe(struct udevice *dev)
{
	struct sunxi_pwm_h616_priv *priv = dev_get_priv(dev);
	int ret;

	ret = reset_deassert(priv->reset);

	if (ret < 0) {
		dev_err(dev, "failed to deassert reset: %d", ret);
		return ret;
	}

	return update_channels(dev);
}

static const struct pwm_ops sunxi_pwm_h616_ops = {
	.set_invert	= sunxi_pwm_h616_set_invert,
	.set_config	= sunxi_pwm_h616_set_config,
	.set_enable	= sunxi_pwm_h616_set_enable,
};

static const struct udevice_id sunxi_pwm_h616_ids[] = {
	{ .compatible = "allwinner,sun50i-h616-pwm" },
	{ }
};

U_BOOT_DRIVER(sunxi_pwm_h616) = {
	.name	= "sunxi_pwm_h616",
	.id	= UCLASS_PWM,
	.of_match = sunxi_pwm_h616_ids,
	.ops	= &sunxi_pwm_h616_ops,
	.of_to_plat	= sunxi_pwm_h616_of_to_plat,
	.probe		= sunxi_pwm_h616_probe,
	.priv_auto	= sizeof(struct sunxi_pwm_h616_priv),
};
