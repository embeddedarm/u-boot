/*
 * Copyright (C) 2012 Boundary Devices Inc.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <common.h>
#include <malloc.h>
#include <asm/arch/clock.h>
#include <asm/arch/imx-regs.h>
#include <asm/errno.h>
#include <asm/gpio.h>
#include <asm/imx-common/mxc_i2c.h>
#include <watchdog.h>

int force_idle_bus(void *priv)
{
	int i;
	int sda, scl;
	ulong elapsed, start_time;
	struct i2c_pads_info *p = (struct i2c_pads_info *)priv;
	int ret = 0;

	gpio_direction_input(p->sda.gp);
	gpio_direction_input(p->scl.gp);

	imx_iomux_v3_setup_pad(p->sda.gpio_mode);
	imx_iomux_v3_setup_pad(p->scl.gpio_mode);

	sda = gpio_get_value(p->sda.gp);
	scl = gpio_get_value(p->scl.gp);
	if ((sda & scl) == 1)
		goto exit;		/* Bus is idle already */

	printf("%s: sda=%d scl=%d sda.gp=0x%x scl.gp=0x%x\n", __func__,
		sda, scl, p->sda.gp, p->scl.gp);
	/* Send high and low on the SCL line */
	for (i = 0; i < 9; i++) {
		gpio_direction_output(p->scl.gp, 0);
		udelay(50);
		gpio_direction_input(p->scl.gp);
		udelay(50);
	}
	start_time = get_timer(0);
	for (;;) {
		sda = gpio_get_value(p->sda.gp);
		scl = gpio_get_value(p->scl.gp);
		if ((sda & scl) == 1)
			break;
		WATCHDOG_RESET();
		elapsed = get_timer(start_time);
		if (elapsed > (CONFIG_SYS_HZ / 5)) {	/* .2 seconds */
			ret = -EBUSY;
			printf("%s: failed to clear bus, sda=%d scl=%d\n",
					__func__, sda, scl);
			break;
		}
	}
	ret = 1;
exit:
	imx_iomux_v3_setup_pad(p->sda.i2c_mode);
	imx_iomux_v3_setup_pad(p->scl.i2c_mode);
	return ret;
}

static void * const i2c_bases[] = {
	(void *)I2C1_BASE_ADDR,
	(void *)I2C2_BASE_ADDR,
#ifdef I2C3_BASE_ADDR
	(void *)I2C3_BASE_ADDR,
#endif
};

/* i2c_index can be from 0 - 2 */
int setup_i2c(unsigned i2c_index, int speed, int slave_addr,
	      struct i2c_pads_info *p)
{
	char *name1, *name2;
	int ret;

	if (i2c_index >= ARRAY_SIZE(i2c_bases))
		return -EINVAL;

	name1 = malloc(9);
	name2 = malloc(9);
	if (!name1 || !name2)
		return -ENOMEM;

	sprintf(name1, "i2c_sda%d", i2c_index);
	sprintf(name2, "i2c_scl%d", i2c_index);
	ret = gpio_request(p->sda.gp, name1);
	if (ret)
		goto err_req1;

	ret = gpio_request(p->scl.gp, name2);
	if (ret)
		goto err_req2;

	/* Enable i2c clock */
	ret = enable_i2c_clk(1, i2c_index);
	if (ret)
		goto err_clk;

	/* Make sure bus is idle */
	ret = force_idle_bus(p);
	if (ret)
		goto err_idle;

	bus_i2c_init(i2c_bases[i2c_index], speed, slave_addr,
			force_idle_bus, p);

	return 0;

err_idle:
err_clk:
	gpio_free(p->scl.gp);
err_req2:
	gpio_free(p->sda.gp);
err_req1:
	free(name1);
	free(name2);

	return ret;
}