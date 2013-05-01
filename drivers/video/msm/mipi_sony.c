/* Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_sony.h"
#include <mach/gpio.h>

#define LCD_BL_EN      13
#define BL_MAX         32
#define LCD_RESET_N     49
static struct msm_panel_common_pdata *mipi_sony_pdata;

static struct dsi_buf sony_tx_buf;
static struct dsi_buf sony_rx_buf;
static int mipi_sony_lcd_init(void);

static char exit_sleep[2] = {0x11, 0x00};
static char display_ctl[2]  = {0x36, 0x40};
static char display_on[2] = {0x29, 0x00};
static char display_off[2] = {0x28, 0x00};
static char enter_sleep[2] = {0x10, 0x00};

static struct dsi_cmd_desc sony_display_off_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_off), display_off},
	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(enter_sleep), enter_sleep}
};
static struct dsi_cmd_desc sony_display_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 140, sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(display_ctl), display_ctl},
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on}
};



void mipi_sony_lcd_reset(void)
{
	gpio_tlmm_config(GPIO_CFG(LCD_RESET_N, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);	/* RFR */
	gpio_set_value_cansleep(LCD_RESET_N,1);
	mdelay(5);
	gpio_set_value_cansleep(LCD_RESET_N,0);
	mdelay(5);
	gpio_set_value_cansleep(LCD_RESET_N,1);
	mdelay(20);
}

static int mipi_sony_lcd_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;

	mipi_sony_lcd_reset();
	mipi_dsi_cmds_tx(&sony_tx_buf, sony_display_on_cmds,
			ARRAY_SIZE(sony_display_on_cmds));

	return 0;
}

static int mipi_sony_lcd_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;

	mipi_dsi_cmds_tx(&sony_tx_buf, sony_display_off_cmds,
			ARRAY_SIZE(sony_display_off_cmds));
    gpio_tlmm_config(GPIO_CFG(LCD_RESET_N, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
    gpio_set_value_cansleep(LCD_RESET_N,0);

	return 0;
}


static void mipi_sony_set_backlight(struct msm_fb_data_type *mfd)
{
	int cnt, bl_level;
	unsigned long flags;
	bl_level = mfd->bl_level;

	if (bl_level == 0) {
		gpio_set_value_cansleep(LCD_BL_EN ,0);
	} else {
		cnt = BL_MAX - bl_level;
		do {
			local_save_flags(flags);
			local_irq_disable();
			gpio_set_value_cansleep(LCD_BL_EN ,0);
			udelay(3);	// T LO
			gpio_set_value_cansleep(LCD_BL_EN ,1);
			local_irq_restore(flags);
			udelay(10); 
		} while (cnt--);
	}
}

static int __devinit mipi_sony_lcd_probe(struct platform_device *pdev)
{
	if (pdev->id == 0) {
		mipi_sony_pdata = pdev->dev.platform_data;
		return 0;
	}

	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = mipi_sony_lcd_probe,
	.driver = {
		.name   = "mipi_sony",
	},
};

static struct msm_fb_panel_data sony_panel_data = {
	.on             = mipi_sony_lcd_on,
	.off            = mipi_sony_lcd_off,
	.set_backlight  = mipi_sony_set_backlight,
};

static int ch_used[3];

int mipi_sony_device_register(struct msm_panel_info *pinfo,
					u32 channel, u32 panel)
{
	struct platform_device *pdev = NULL;
	int ret;

	if ((channel >= 3) || ch_used[channel])
		return -ENODEV;

	ch_used[channel] = TRUE;

	ret = mipi_sony_lcd_init();
	if (ret) {
		pr_err("mipi_sony_lcd_init() failed with ret %u\n", ret);
		return ret;
	}

	pdev = platform_device_alloc("mipi_sony", (panel << 8)|channel);
	if (!pdev)
		return -ENOMEM;

	sony_panel_data.panel_info = *pinfo;

	ret = platform_device_add_data(pdev, &sony_panel_data,
		sizeof(sony_panel_data));
	if (ret) {
		printk(KERN_ERR
		  "%s: platform_device_add_data failed!\n", __func__);
		goto err_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		printk(KERN_ERR
		  "%s: platform_device_register failed!\n", __func__);
		goto err_device_put;
	}

	return 0;

err_device_put:
	platform_device_put(pdev);
	return ret;
}

#ifdef CONFIG_SW_RESET
extern int msm_reset_reason_read_only(void);
#endif
static int mipi_sony_lcd_init(void)
{
#ifdef FEATURE_SKY_BACLIGHT_MAX8831
    led_i2c_api_Init();
#endif
    mipi_dsi_buf_alloc(&sony_tx_buf, DSI_BUF_SIZE);
    mipi_dsi_buf_alloc(&sony_rx_buf, DSI_BUF_SIZE);

	return platform_driver_register(&this_driver);
}
