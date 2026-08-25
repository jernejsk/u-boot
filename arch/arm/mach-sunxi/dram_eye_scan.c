// SPDX-License-Identifier: GPL-2.0+
/*
 * Per data bit eye centring for sunxi DRAM PHYs.
 *
 * (C) Copyright 2026 Jernej Skrabec <jernej.skrabec@gmail.com>
 */

#include <config.h>
#include <log.h>
#include <asm/io.h>
#include <asm/arch/dram_eye_scan.h>
#include <linux/kernel.h>

#define EYE_PATTERN_A		0x01234567
#define EYE_PATTERN_A_STEP	0x01010101
#define EYE_PATTERN_B		0xfdb97531
#define EYE_TEST_WORDS		256
#define EYE_TEST_ROUNDS		3
#define EYE_BITS_PER_LANE	8

static bool eye_memtest_once(void)
{
	u32 *mem = (u32 *)CFG_SYS_SDRAM_BASE;
	u32 val = EYE_PATTERN_A;
	int i;

	for (i = 0; i < EYE_TEST_WORDS; i++) {
		writel(val, &mem[i]);
		val += EYE_PATTERN_A_STEP;
	}

	val = EYE_PATTERN_A;
	for (i = 0; i < EYE_TEST_WORDS; i++) {
		if (readl(&mem[i]) != val)
			return false;
		val += EYE_PATTERN_A_STEP;
	}

	for (i = 0; i < EYE_TEST_WORDS; i++)
		writel(EYE_PATTERN_B, &mem[i]);

	for (i = 0; i < EYE_TEST_WORDS; i++)
		if (readl(&mem[i]) != EYE_PATTERN_B)
			return false;

	return true;
}

/* One failure is noise; only a delay that survives every round passes. */
static bool eye_memtest(void)
{
	int i;

	for (i = 0; i < EYE_TEST_ROUNDS; i++)
		if (!eye_memtest_once())
			return false;

	return true;
}

static void eye_set_one(bool tx, unsigned int lane, unsigned int bit, u8 delay)
{
	mctl_phy_bit_delay_begin(tx);
	mctl_phy_bit_delay_set(tx, lane, bit, delay);
	mctl_phy_bit_delay_commit(tx);
}

/* Walk one bit outwards from its static delay, park it in the middle. */
static u32 eye_centre_bit(bool tx, unsigned int lane, unsigned int bit,
			  u8 seed, u8 limit, u8 *centre)
{
	int left, right;

	eye_set_one(tx, lane, bit, seed);
	if (!eye_memtest()) {
		*centre = seed;
		return 0;
	}

	for (left = seed; left > 0; left--) {
		eye_set_one(tx, lane, bit, left - 1);
		if (!eye_memtest())
			break;
	}

	for (right = seed; right < limit; right++) {
		eye_set_one(tx, lane, bit, right + 1);
		if (!eye_memtest())
			break;
	}

	*centre = left + (right - left) / 2;
	eye_set_one(tx, lane, bit, *centre);

	return right - left + 1;
}

static void eye_set_all(bool tx, unsigned int lanes, u8 delay)
{
	unsigned int lane, bit;

	mctl_phy_bit_delay_begin(tx);

	for (lane = 0; lane < lanes; lane++)
		for (bit = 0; bit < EYE_BITS_PER_LANE; bit++)
			mctl_phy_bit_delay_set(tx, lane, bit, delay);

	mctl_phy_bit_delay_commit(tx);
}

/* One delay for every bit, to start from when the board's own do not work. */
static bool eye_coarse(bool tx, unsigned int lanes, u8 limit, u8 *found)
{
	unsigned int best_start = 0, best_len = 0, start = 0, len = 0;
	unsigned int delay;

	for (delay = 0; delay <= limit; delay++) {
		eye_set_all(tx, lanes, delay);

		if (eye_memtest()) {
			if (!len)
				start = delay;
			len++;
			if (len > best_len) {
				best_len = len;
				best_start = start;
			}
		} else {
			len = 0;
		}
	}

	if (!best_len)
		return false;

	*found = best_start + best_len / 2;
	eye_set_all(tx, lanes, *found);

	return true;
}

static bool eye_scan_dir(bool tx, unsigned int lanes, u32 min_width)
{
	u8 limit = mctl_phy_bit_delay_max(tx);
	unsigned int lane, bit;
	bool result = true;
	u8 coarse;

	eye_set_all(tx, lanes, limit / 2);
	if (!eye_memtest()) {
		if (!eye_coarse(tx, lanes, limit, &coarse)) {
			debug("DRAM: no %s delay works for all bits\n",
			      tx ? "transmit" : "receive");
			return false;
		}
		debug("DRAM: %s delays start from %u for every bit\n",
		      tx ? "transmit" : "receive", coarse);
	}

	for (lane = 0; lane < lanes; lane++) {
		u8 widths[EYE_BITS_PER_LANE], centres[EYE_BITS_PER_LANE];

		for (bit = 0; bit < EYE_BITS_PER_LANE; bit++) {
			u8 seed = min(mctl_phy_bit_delay_get(tx, lane, bit),
				      limit);
			u32 width;

			width = eye_centre_bit(tx, lane, bit, seed, limit,
					       &centres[bit]);
			widths[bit] = min(width, 0xffu);

			if (width < min_width)
				result = false;
		}

		debug("DRAM: %s lane %u eyes %u %u %u %u %u %u %u %u at %u %u %u %u %u %u %u %u\n",
		      tx ? "tx" : "rx", lane,
		      widths[0], widths[1], widths[2], widths[3],
		      widths[4], widths[5], widths[6], widths[7],
		      centres[0], centres[1], centres[2], centres[3],
		      centres[4], centres[5], centres[6], centres[7]);
	}

	return result;
}

bool mctl_phy_eye_scan(unsigned int lanes, u32 min_width)
{
	bool result = true;

	/* Transmit first: a bit written wrong cannot be read back right. */
	if (!eye_scan_dir(true, lanes, min_width))
		result = false;
	if (!eye_scan_dir(false, lanes, min_width))
		result = false;

	debug("DRAM: eye scan wants at least %u steps per bit\n", min_width);

	return result;
}
