// SPDX-License-Identifier: GPL-2.0
/*
 * Sharp Memory Display Driver.
 *
 * In this case I'm testing with LS027B7DH01
 * But it should work with others by changing 
 * W x H Display resolution
 *
 * Copyright (c) 2021 Thipok Jiamjarapan
 * <thipok17@gmail.com> 
 *
 */

struct memlcdfb_par {
	struct spi_device *spi;
	struct fb_info *info;
	struct pwm_device *pwm;
	const struct memlcd_platform_data *pdata;
	int virt_cs;
	u32 pwm_period;
	u32 offset;
	u8 YlineStart;
	u8 YlineEnd;
};

//reserve for future use
struct memlcd_platform_data {
	int vcs_gpio;
};