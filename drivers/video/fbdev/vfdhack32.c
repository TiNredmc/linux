// SPDX-License-Identifier: GPL-2.0
/*
 * VFDHack32 Project Linux side (Host) Driver.
 *
 * This is the Host driver for project VFDHack32. 
 * VFDHack32 is the project that I huntdown the datasheet for Noritake Itron MN15439A.
 * and design the driver board plus reverse engineering the We1rD SPI protocol that they were using.
 * By using stm32 as a Frame buffer and display controller/refresher and SPI DMA RX from host and TX to Display.
 * The Display itself is Monochrome 154x39 pixels but the Framebuffer is the size of Monochrome 160x39 pixels (750 bytes).
 * 
 *
 * Copyright (c) 2021 Thipok Jiamjarapan
 * <thipok17@gmail.com> 
 *
 */
 
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
//#include <linux/of_gpio.h>
//#include <linux/gpio.h>
#include <linux/spi/spi.h>
//#include <linux/pwm.h>
#include <linux/uaccess.h>
#include <linux/bitrev.h>

#define VFD_W			154// Actual display width is 154 pixels 
#define VFD_H			39 // display height is 39 pixels 

#define VFD_VirtW		160// Virtual resolution used to make sure that framebuffer is byte aligned 

#define VFD_VMEM_SIZE	750// Vmem require 750 bytes for Frame buffer of 160x39 pixels.

// Struct for our display driver 
struct vfdhack32fb_par {
	struct spi_device *spi;
	struct fb_info *info;
	//const struct vfdhack32fb_platform_data *pdata;

};

static const struct fb_fix_screeninfo vfdhack32fb_fix  = {
	.id = 			"VFDHack32FB",
	.type = 		FB_TYPE_PACKED_PIXELS,
	.visual = 		FB_VISUAL_MONO10, //
	.xpanstep = 		0,
	.ypanstep = 		0,
	.ywrapstep = 		0,
	.line_length =		VFD_VirtW,
	.accel = 		FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo vfdhack32fb_var  = {
	.xres = 		VFD_W,
	.yres = 		VFD_H,
	.xres_virtual =		VFD_VirtW,
	.yres_virtual = 	VFD_H,
	/* monochrome pixel format */
	.bits_per_pixel = 	1,
	.red = {0, 1, 0},
	.green = {0, 1, 0},
	.blue = {0, 1, 0},
	.transp = {0, 0, 0},
	.left_margin = 		0,
	.right_margin =		0,
	.upper_margin = 	0,
	.lower_margin = 	0,
	.vmode =		FB_VMODE_NONINTERLACED,
};

static void vfdhack32fb_update_display(struct vfdhack32fb_par *par){
	u8  *vmem = par->info->screen_base; 

	spi_write(par->spi, vmem, VFD_VMEM_SIZE);

}

static ssize_t vfdhack32fb_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos){
	struct vfdhack32fb_par *par = info->par;
	unsigned long total_size;
	unsigned long p = *ppos;
	u8 __iomem *dst;

	total_size = info->fix.smem_len;// total size is 12000 bytes equal to the frame buffer.

	if (p > total_size)// if offset of data is out of bound, reject it.
		return -EINVAL;

	if (count + p > total_size)// if the *buf data is not fit, Cap the size limit 
		count = total_size - p;

	if (!count)
		return -EINVAL;

	dst = (void __force *) (info->screen_base + p);// dst keeping our Frame Buffer mem address.
	
	if (copy_from_user(dst, buf, count))
		return -EFAULT; 
	
	vfdhack32fb_update_display(par);

	*ppos += count;

	return count;	
}

static int vfdhack32fb_blank(int blank_mode, struct fb_info *info){
	struct vfdhack32fb_par *par = info->par;

	//if (blank_mode != FB_BLANK_UNBLANK)
		//gpio_set_value_cansleep(par->virt_cs, 0);// turn screen off
	//else
		//gpio_set_value_cansleep(par->virt_cs, 1);// turn screen on
	return 0;
}

static void vfdhack32fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect){
	struct vfdhack32fb_par *par = info->par;
	sys_fillrect(info, rect);
	vfdhack32fb_update_display(par);
}

static void vfdhack32fb_copyarea(struct fb_info *info, const struct fb_copyarea *area){
	struct vfdhack32fb_par *par = info->par;
	sys_copyarea(info, area);
	vfdhack32fb_update_display(par);
}

static void vfdhack32fb_imageblit(struct fb_info *info, const struct fb_image *image){
	struct vfdhack32fb_par *par = info->par;
	sys_imageblit(info, image);
	vfdhack32fb_update_display(par);
}

static struct fb_ops vfdhack32fb_ops = {
	.owner = 		THIS_MODULE,
	.fb_read = 		fb_sys_read,
	.fb_write = 		vfdhack32fb_write,
	.fb_blank = 		vfdhack32fb_blank,
	.fb_fillrect = 		vfdhack32fb_fillrect,
	.fb_copyarea = 		vfdhack32fb_copyarea,
	.fb_imageblit = 	vfdhack32fb_imageblit,
};

static void vfdhack32fb_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	vfdhack32fb_update_display(info->par);
}

static struct fb_deferred_io vfdhack32fb_defio = {
	.delay		= HZ / 10,// Display update is 10Hz
	.deferred_io	= vfdhack32fb_deferred_io,
};

static int vfdhack32fb_probe(struct spi_device *spi){
	//struct device_node *np = spi->dev.of_node;
	struct fb_info *info;
	u8 *vmem;
	u16 vmem_size = VFD_VMEM_SIZE;
	struct vfdhack32fb_par *par;

	
	if (!spi->dev.of_node) {// check node data from device tree.
		dev_err(&spi->dev, "No device tree data found!\n");
		return -EINVAL;
	}
	
	/* Allocate memory for Frame buffer */
	vmem = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(vmem_size));// Get free page that filled with zero.
	if (!vmem){
		printk("VFD FB allocation failed!\n");
		goto fballoc_fail;
	}

	
	info = framebuffer_alloc(sizeof(struct vfdhack32fb_par), &spi->dev);
	if (!info)
		goto fballoc_fail;
	
	info->screen_base = (u8 __force __iomem *)vmem;
	info->fix = vfdhack32fb_fix;
	info->fix.smem_len = vmem_size;
    info->fix.smem_start = __pa(vmem);
	printk("VFDHack32 mem bank at %lx\n",info->fix.smem_start);
	info->fbops = &vfdhack32fb_ops;

	info->var = vfdhack32fb_var;

	info->fbdefio = &vfdhack32fb_defio;
	fb_deferred_io_init(info);

	par = info->par;
	par->info = info;
	par->spi = spi;

	spi_set_drvdata(spi, info);
	
	printk("VFDHack32 registering fb...\n");
	if (register_framebuffer(info))
		goto fbreg_fail;
	
	printk("VFDHack32 fb registered\n");	

	printk(KERN_INFO
		"fb%d: %s frame buffer device,\n\tusing %u KB of video memory\n",
		info->node, info->fix.id, vmem_size);

	return 0;

	/* TODO: release gpios on fail */
init_fail:
	spi_set_drvdata(spi, NULL);
	printk(KERN_INFO"VFDHack32 init failed");

fbreg_fail:
	framebuffer_release(info);
	printk(KERN_INFO"VFDHack32 fbreg failed");

fballoc_fail:
	framebuffer_release(info);
	vfree(vmem);
	printk(KERN_INFO"VFDHack32 fb mem allocate failed");
	return -ENOMEM;
}


static int vfdhack32fb_remove(struct spi_device *spi){
	struct vfdhack32fb_par *par = spi_get_drvdata(spi);
	struct fb_info *info = par->info;
	
	spi_unregister_device(par->spi);	
	
	fb_deferred_io_cleanup(info);
	vfree(info->screen_base);
	framebuffer_release(info);

	return 0;
}

static const struct spi_device_id vfdhack32fb_ids[] = {
	{ .name = "VFDHack32"},
	{ }
};
 
//MODULE_DEVICE_TABLE(spi, vfdhack32fb_ids);


static const struct of_device_id vfdhack32fb_of_match[] = {
	{ .compatible = "noritake,mn15439a"},
	{ .compatible = "tlhx,vfdhack32"},
	{ }
};
MODULE_DEVICE_TABLE(of, vfdhack32fb_of_match);

static struct spi_driver vfdhack32_driver = {
	.driver = {
		.name = "VFDHack32",
		.owner = THIS_MODULE, 
		.of_match_table = vfdhack32fb_of_match,
	},
	.id_table = vfdhack32fb_ids,
	.probe = vfdhack32fb_probe,
	.remove = vfdhack32fb_remove,
};
module_spi_driver(vfdhack32_driver);

MODULE_DESCRIPTION("VFDHack32 Project Linux side (Host) Driver");
MODULE_AUTHOR("Thipok Jiamjarapan <thipok17@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

