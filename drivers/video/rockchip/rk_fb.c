/*
 * drivers/video/rockchip/rk_fb.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 	yxj<yxj@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/earlysuspend.h>
#include <asm/div64.h>
#include <asm/uaccess.h>
#include <linux/rk_fb.h>
#include <plat/ipp.h>
#include <linux/linux_logo.h>
#include "hdmi/rk_hdmi.h"

#include <mach/clock.h>
#include <linux/clk.h>

static struct platform_device *g_fb_pdev;

static struct rk_fb_rgb def_rgb_16 = {
     red:    { offset: 11, length: 5, },
     green:  { offset: 5,  length: 6, },
     blue:   { offset: 0,  length: 5, },
     transp: { offset: 0,  length: 0, },
};

char * get_format_string(enum data_format format,char *fmt)
{
	if(!fmt)
		return NULL;
	switch(format)
	{
		case ARGB888:
			strcpy(fmt,"ARGB888");
			break;
		case RGB888:
			strcpy(fmt,"RGB888");
			break;
		case RGB565:
			strcpy(fmt,"RGB565");
			break;
		case YUV420:
			strcpy(fmt,"YUV420");
			break;
		case YUV422:
			strcpy(fmt,"YUV422");
			break;
		case YUV444:
			strcpy(fmt,"YUV444");
			break;
		case XRGB888:
			strcpy(fmt,"XRGB888");
			break;
		case XBGR888:
			strcpy(fmt,"XBGR888");
			break;
		case ABGR888:
			strcpy(fmt,"XBGR888");
			break;
		default:
			strcpy(fmt,"invalid");
			break;
	}
	return fmt;	
}

/**********************************************************************
For IEP
***********************************************************************/
int rk_fb_dpi_open(bool open)
{
	struct rk_lcdc_device_driver * dev_drv = NULL;
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	int i;
	for( i = 0; i < inf->num_lcdc; i++)
	{
		if(inf->lcdc_dev_drv[i]->enable)
			break;
	}
	dev_drv = inf->lcdc_dev_drv[i];
	if(dev_drv->dpi_open)
		dev_drv->dpi_open(dev_drv,open);

	return 0;
}

int rk_fb_dpi_layer_sel(int layer_id)
{
	struct rk_lcdc_device_driver * dev_drv = NULL;
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	int i;
	for( i = 0; i < inf->num_lcdc; i++)
	{
		if(inf->lcdc_dev_drv[i]->enable)
			break;
	}
	dev_drv = inf->lcdc_dev_drv[i];
	if(dev_drv->dpi_layer_sel)
		dev_drv->dpi_layer_sel(dev_drv,layer_id);

	return 0;
}

/**********************************************************************
this is for hdmi
name: lcdc device name ,lcdc0 , lcdc1
***********************************************************************/
struct rk_lcdc_device_driver * rk_get_lcdc_drv(char *name)
{
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	int i = 0;
	for( i = 0; i < inf->num_lcdc; i++)
	{
		if(!strcmp(inf->lcdc_dev_drv[i]->name,name))
			break;
	}
	return inf->lcdc_dev_drv[i];
}

int rk_fb_switch_screen(rk_screen *screen ,int enable ,int lcdc_id)
{
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	struct rk_lcdc_device_driver * dev_drv = NULL, *dev_drv1 = NULL;
	struct fb_info *info = NULL;
	struct fb_fix_screeninfo *fix = NULL;
	char name[6];
	int ret, i, layer_id;
	
	sprintf(name, "lcdc%d",lcdc_id);
	for(i = 0; i < inf->num_lcdc; i++)  //find the driver the display device connected to
	{
		if(!strcmp(inf->lcdc_dev_drv[i]->name, name))
		{
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	if(i == inf->num_lcdc)
	{
		printk(KERN_ERR "%s driver not found!", name);
		return -ENODEV;
	}
	
	dev_drv->enable = enable;
	if(!enable)
	{
		for(i = 0; i < dev_drv->num_layer; i++)
		{
			//disable the layer which attached to this fb
			if(dev_drv->layer_par[i] && dev_drv->layer_par[i]->state)
				dev_drv->open(dev_drv, i, LAYER_DISABLE);
		}
		return 0;
	}
	else
		memcpy(dev_drv->cur_screen, screen, sizeof(rk_screen));
	
	ret = dev_drv->load_screen(dev_drv, 1);
	
	if(enable != 1)	return 0;
		
	for(i = 0; i < dev_drv->num_layer; i++) {
		#if 1
		info = inf->fb[i];
		dev_drv1 = (struct rk_lcdc_device_driver * )info->par;
		if(dev_drv1 != dev_drv) {
			info->par = dev_drv;
			dev_drv->overlay = dev_drv1->overlay;
			dev_drv->overscan = dev_drv1->overscan;
			dev_drv->vsync_info.active = dev_drv1->vsync_info.active;
		}
		memcpy(dev_drv->cur_screen, screen, sizeof(rk_screen));
		#else
		info = inf->fb[dev_drv->fb_index_base + i];
		#endif
		layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
		if(dev_drv->layer_par[layer_id]) {
			#if 1 
			if(dev_drv1 && dev_drv1->layer_par[layer_id]) {
				dev_drv->layer_par[layer_id]->state = dev_drv1->layer_par[layer_id]->state;
			}
			#endif
			if( dev_drv->layer_par[layer_id]->state )
				dev_drv->open(dev_drv, layer_id, LAYER_ENABLE);
			else
				dev_drv->open(dev_drv, layer_id, LAYER_DISABLE);
		}
		ret = info->fbops->fb_set_par(info);
		ret = info->fbops->fb_pan_display(&info->var, info);
	}
	return 0;
}

static int rk_fb_open(struct fb_info *info,int user)
{
    struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
    int layer_id;
    
    layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
    if(!dev_drv->layer_par[layer_id]->state) {
	    dev_drv->layer_par[layer_id]->state = 1;
    	if(dev_drv->enable)
    		dev_drv->open(dev_drv,layer_id, LAYER_ENABLE);
	}
    return 0;
}

static int rk_fb_close(struct fb_info *info,int user)
{
	#ifndef CONFIG_ANDROID_KITKAT
	struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
    int layer_id;

    layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
    if(dev_drv->layer_par[layer_id]->state) {
    	dev_drv->layer_par[layer_id]->state = 0;
    	//When overlay is not enabled, there is only one layer is enabled for UI display,
    	//We need not close it.
    	if(dev_drv->enable && dev_drv->overlay)
    		dev_drv->open(dev_drv,layer_id, LAYER_DISABLE);
	}
	#endif
	return 0;
}

static ssize_t rk_fb_read(struct fb_info *info, char __user *buf,
			   size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	u8 *buffer, *dst;
	u8 __iomem *src;
	int c, cnt = 0, err = 0;
	unsigned long total_size;
	struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
	struct layer_par *par = NULL;
	int layer_id = 0;

	layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
	if(layer_id < 0)
	{
		return  -ENODEV;
	}
	else
	{
		par = dev_drv->layer_par[layer_id];
	}

	if(par->format == RGB565)
	{
		total_size = par->xact*par->yact<<1; //only read the current frame buffer
	}
	else
		total_size = par->xact*par->yact<<2;
	
	
	if (p >= total_size)
		return 0;
	
	if (count >= total_size)
		count = total_size;

	if (count + p > total_size)
		count = total_size - p;

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
			 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	
	src = (u8 __iomem *) (info->screen_base + p + par->y_offset);

	while (count) {
		c  = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		dst = buffer;
		fb_memcpy_fromfb(dst, src, c);
		dst += c;
		src += c;

		if (copy_to_user(buf, buffer, c)) {
			err = -EFAULT;
			break;
		}
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (err) ? err : cnt;
}

static ssize_t rk_fb_write(struct fb_info *info, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	u8 *buffer, *src;
	u8 __iomem *dst;
	int c, cnt = 0, err = 0;
	unsigned long total_size;
	struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
	struct layer_par *par = NULL;
	int layer_id = 0;

	layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
	if(layer_id < 0)
	{
		return  -ENODEV;
	}
	else
	{
		par = dev_drv->layer_par[layer_id];
	}

	if(par->format == RGB565)
	{
		total_size = par->xact*par->yact<<1; //write the current frame buffer
	}
	else
		total_size = par->xact*par->yact<<2;
	
	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
			 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	dst = (u8 __iomem *) (info->screen_base + p + par->y_offset);

	while (count) {
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		src = buffer;

		if (copy_from_user(src, buf, c)) {
			err = -EFAULT;
			break;
		}

		fb_memcpy_tofb(dst, src, c);
		dst += c;
		src += c;
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (cnt) ? cnt : err;
	
}

static int rk_fb_ioctl(struct fb_info *info, unsigned int cmd,unsigned long arg)
{
	struct fb_fix_screeninfo *fix = &info->fix;
	struct rk_lcdc_device_driver *dev_drv = (struct rk_lcdc_device_driver * )info->par;
	int  layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
	void __user *argp = (void __user *)arg;
	u32 yuv_phy[2];
	int enable, new_layer_id;
	
	switch(cmd)
	{
		case RK_FBIOSET_YUV_ADDR:   //when in video mode, buff alloc by android
			if (copy_from_user(yuv_phy, argp, 8))
				return -EFAULT;
			fix->smem_start = yuv_phy[0];  //four y
			fix->mmio_start = yuv_phy[1];  //four uv
			break;
		case RK_FBIOSET_ENABLE:
			if (copy_from_user(&enable, argp, sizeof(enable)))
				return -EFAULT;
			if(dev_drv->layer_par[layer_id]->state != enable) {
				dev_drv->layer_par[layer_id]->state = enable;
				if(dev_drv->enable)
					dev_drv->open(dev_drv,layer_id,enable);
			}
			break;
		case RK_FBIOGET_ENABLE:
			enable = dev_drv->get_layer_state(dev_drv,layer_id);
			if(copy_to_user(argp,&enable,sizeof(enable)))
				return -EFAULT;
			break;
		case RK_FBIOSET_OVERLAY_STATE:
			if (copy_from_user(&enable, argp, sizeof(enable)))
				return -EFAULT;

			//For some platform, when overlay is enabled, 
			//fb may map to different physical layer, so we
			//need to configure layer status.
			dev_drv->overlay = enable;
			new_layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
			if( layer_id != new_layer_id && 
				dev_drv->layer_par[new_layer_id]->state != dev_drv->layer_par[layer_id]->state) {
				enable = dev_drv->layer_par[new_layer_id]->state;
				dev_drv->layer_par[new_layer_id]->state = dev_drv->layer_par[layer_id]->state;
				dev_drv->open(dev_drv,new_layer_id, dev_drv->layer_par[new_layer_id]->state);
				dev_drv->layer_par[layer_id]->state = enable;
				dev_drv->open(dev_drv,layer_id, dev_drv->layer_par[layer_id]->state);
            }
			
//			dev_drv->ovl_mgr(dev_drv,enable,1);
			break;
		case RK_FBIOGET_OVERLAY_STATE:
			enable = dev_drv->ovl_mgr(dev_drv,0,0);
			if (copy_to_user(argp, &enable, sizeof(enable)))
				return -EFAULT;
			break;
		case RK_FBIOPUT_NUM_BUFFERS:
			if (copy_from_user(&enable, argp, sizeof(enable)))
				return -EFAULT;
			dev_drv->num_buf = enable;
			printk("rk fb use %d buffers\n",enable);
			break;
		case RK_FBIOSET_VSYNC_ENABLE:
			if (copy_from_user(&enable, argp, sizeof(enable)))
				return -EFAULT;
			dev_drv->vsync_info.active = enable;
			break;
		case RK_FBIOSET_CONFIG_DONE:
			if(copy_from_user(&(dev_drv->wait_fs),argp,sizeof(dev_drv->wait_fs)))
				return -EFAULT;
			if(dev_drv->lcdc_reg_update)
				dev_drv->lcdc_reg_update(dev_drv);
			break;
		default:
			dev_drv->ioctl(dev_drv,cmd,arg,layer_id);
            break;
	}
	
	return 0;
}

static int rk_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	
	if( 0==var->xres_virtual || 0==var->yres_virtual ||
		 0==var->xres || 0==var->yres || var->xres<16 ||
		 ((16!=var->bits_per_pixel)&&(32!=var->bits_per_pixel)) )
	 {
		 printk("%s check var fail 1!!! \n",info->fix.id);
		 printk("xres_vir:%d>>yres_vir:%d\n", var->xres_virtual,var->yres_virtual);
		 printk("xres:%d>>yres:%d\n", var->xres,var->yres);
		 printk("bits_per_pixel:%d \n", var->bits_per_pixel);
		 return -EINVAL;
	 }
 
	 if( ((var->xoffset+var->xres) > var->xres_virtual) ||
	     ((var->yoffset+var->yres) > (var->yres_virtual)) )
	 {
		 printk("%s check_var fail 2!!! \n",info->fix.id);
		 printk("xoffset:%d>>xres:%d>>xres_vir:%d\n",var->xoffset,var->xres,var->xres_virtual);
		 printk("yoffset:%d>>yres:%d>>yres_vir:%d\n",var->yoffset,var->yres,var->yres_virtual);
		 return -EINVAL;
	 }

    return 0;
}

static int rk_fb_set_par(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	struct fb_fix_screeninfo *fix = &info->fix;
	struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
	struct layer_par *par = NULL;
	rk_screen *screen =dev_drv->cur_screen;
	int layer_id = 0;	
	u32 cblen = 0,crlen = 0;
	u32 xsize =0,ysize = 0;              //winx display window height/width --->LCDC_WINx_DSP_INFO
	u32 xoffset = var->xoffset;		// offset from virtual to visible 
	u32 yoffset = var->yoffset;		//resolution			
	u32 xpos = (var->nonstd>>8) & 0xfff; //visiable pos in panel
	u32 ypos = (var->nonstd>>20) & 0xfff;
	u32 xvir = var->xres_virtual;
	u32 yvir = var->yres_virtual;
	u8 data_format = var->nonstd&0xff;
 	u16 overscaned_screen_xsize, overscaned_screen_ysize, overscaned_screen_xpos, overscaned_screen_ypos;
 	
	layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
	if(layer_id < 0)
	{
		return  -ENODEV;
	}
	else
	{
		par = dev_drv->layer_par[layer_id];
	}
	
	if(var->grayscale>>8)  //if the application has specific the horizontal and vertical display size
	{
		xsize = (var->grayscale>>8) & 0xfff;  //visiable size in panel ,for vide0
		ysize = (var->grayscale>>20) & 0xfff;
	}
	else  //ohterwise  full  screen display
	{
		xsize = screen->x_res;
		ysize = screen->y_res;
		xpos = 0;
		ypos = 0;
	}

	if(xsize > screen->x_res)
		xsize = screen->x_res;
	if(ysize > screen->y_res)
		ysize = screen->y_res;

	overscaned_screen_xsize = screen->x_res*(dev_drv->overscan.left + dev_drv->overscan.right)/200;
	overscaned_screen_ysize = screen->y_res*(dev_drv->overscan.top + dev_drv->overscan.bottom)/200;
	overscaned_screen_xpos = screen->x_res/2 - screen->x_res* dev_drv->overscan.left/200;
	overscaned_screen_ypos = screen->y_res/2 - screen->y_res* dev_drv->overscan.top/200;

	if(dev_drv->overlay ) {
		if(!strcmp(info->fix.id, "fb0")) {
			xpos = 0;
			ypos = 0;
			xsize = screen->x_res;
			ysize = screen->y_res;
		}
		else {
			if((xsize == screen->x_res) && (ysize == screen->y_res) )
			{
				xsize = overscaned_screen_xsize;
				ysize = overscaned_screen_ysize;
				xpos = overscaned_screen_xpos;
				ypos = overscaned_screen_ypos;
			}
			else {
				#ifndef CONFIG_ANDROID_KITKAT
				xsize = xsize * overscaned_screen_xsize/(1280);
				ysize = ysize * overscaned_screen_ysize/(720);
				xpos = xpos * overscaned_screen_xsize/(1280);
				ypos = ypos * overscaned_screen_ysize/(720);
				xpos += overscaned_screen_xpos;
				ypos += overscaned_screen_ypos;
				#endif
			}
		}
	} else {
		xsize = overscaned_screen_xsize;
		ysize = overscaned_screen_ysize;
		xpos = overscaned_screen_xpos;
		ypos = overscaned_screen_ypos;
	}

	/* calculate y_offset,c_offset,line_length,cblen and crlen  */
	switch (data_format)
	{
		case HAL_PIXEL_FORMAT_RGBX_8888: 
			par->format = XBGR888;
			fix->line_length = 4 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*4;
			break;
		case HAL_PIXEL_FORMAT_RGBA_8888 :      // rgb
			par->format = ABGR888;
			fix->line_length = 4 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*4;
			break;
		case HAL_PIXEL_FORMAT_BGRA_8888 :      // rgb
			par->format = ARGB888;
			fix->line_length = 4 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*4;
			break;
		case HAL_PIXEL_FORMAT_RGB_888 :
			par->format = RGB888;
			fix->line_length = 3 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*3;
			break;
		case HAL_PIXEL_FORMAT_RGB_565:  //RGB565
			par->format = RGB565;
			fix->line_length = 2 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*2;
		    	break;
		case HAL_PIXEL_FORMAT_YCbCr_422_SP : // yuv422
			par->format = YUV422;
			fix->line_length = xvir;
			cblen = crlen = (xvir*yvir)>>1;
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = par->y_offset;
		    	break;
		case HAL_PIXEL_FORMAT_YCrCb_NV12   : // YUV420---uvuvuv
			par->format = YUV420;
			fix->line_length = xvir;
			cblen = crlen = (xvir*yvir)>>2;
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = (yoffset>>1)*xvir + xoffset;
		    	break;
		case HAL_PIXEL_FORMAT_YCrCb_444 : // yuv444
			par->format = YUV444;
			fix->line_length = xvir<<2;
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = yoffset*2*xvir +(xoffset<<1);
			cblen = crlen = (xvir*yvir);
			break;
		default:
			printk("%s:un supported format:0x%x\n",__func__,data_format);
		    return -EINVAL;
	}

	par->xpos = xpos;
	par->ypos = ypos;
	par->xsize = xsize;
	par->ysize = ysize;

	par->smem_start =fix->smem_start;
	par->cbr_start = fix->mmio_start;
	if(dev_drv->overlay && !strcmp(info->fix.id, "fb0")) {
		par->xact = xsize;              //winx active window height,is a part of vir
		par->yact = ysize;
	}
	else {
		par->xact = var->xres;              //winx active window height,is a part of vir
		par->yact = var->yres;
	}
	par->xvir =  var->xres_virtual;		// virtual resolution	 stride --->LCDC_WINx_VIR
	par->yvir =  var->yres_virtual;
	
	dev_drv->set_par(dev_drv,layer_id);
	return 0;
}

static int rk_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
	struct layer_par *par = NULL;
	int layer_id = 0;
	u32 xoffset = var->xoffset;		// offset from virtual to visible 
	u32 yoffset = var->yoffset;				
	u32 xvir = var->xres_virtual;
	u8 data_format = var->nonstd & 0xff;
	rk_screen *screen = dev_drv->cur_screen;
	
	layer_id = dev_drv->fb_get_layer(dev_drv,info->fix.id);
	if(layer_id < 0)
	{
		return  -ENODEV;
	}
	else
	{
		par = dev_drv->layer_par[layer_id];
	}
	
	switch (par->format)
	{
		case XBGR888:
		case ARGB888:
		case ABGR888:
		#ifndef CONFIG_ANDROID_KITKAT
			if(dev_drv->overlay && !strcmp(info->fix.id, "fb0"))
				yoffset += var->yres - screen->y_res;
		#endif
			par->y_offset = (yoffset*xvir + xoffset)*4;
			break;
		case  RGB888:
			par->y_offset = (yoffset*xvir + xoffset)*3;
			break;
		case RGB565:
			par->y_offset = (yoffset*xvir + xoffset)*2;
			break;
		case  YUV422:
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = par->y_offset;
			break;
		case  YUV420:
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = (yoffset>>1)*xvir + xoffset;
			break;
		case  YUV444 : // yuv444
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = yoffset*2*xvir +(xoffset<<1);
			break;
		default:
			printk("un supported format:0x%x\n",data_format);
			return -EINVAL;
    }

	dev_drv->pan_display(dev_drv,layer_id);
	return 0;
}

static int rk_fb_blank(int blank_mode, struct fb_info *info)
{
    struct rk_lcdc_device_driver *dev_drv = (struct rk_lcdc_device_driver * )info->par;
	struct fb_fix_screeninfo *fix = &info->fix;
	int layer_id;
	
	layer_id = dev_drv->fb_get_layer(dev_drv,fix->id);
	if(layer_id < 0)
	{
		return  -ENODEV;
	}
	dev_drv->blank(dev_drv,layer_id,blank_mode);
	return 0;
}

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;
			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);
			pal[regno] = val;
		}
		break;
	default:
		return -1;	/* unknown type */
	}

	return 0;
}

static struct fb_ops fb_ops = {
    .owner          = THIS_MODULE,
    .fb_open        = rk_fb_open,
    .fb_release     = rk_fb_close,
    .fb_check_var   = rk_fb_check_var,
    .fb_set_par     = rk_fb_set_par,
    .fb_blank       = rk_fb_blank,
    .fb_ioctl       = rk_fb_ioctl,
    .fb_pan_display = rk_pan_display,
    .fb_read	    = rk_fb_read,
    .fb_write	    = rk_fb_write,
    .fb_setcolreg   = fb_setcolreg,
    .fb_fillrect    = cfb_fillrect,
    .fb_copyarea    = cfb_copyarea,
    .fb_imageblit   = cfb_imageblit,
};



static struct fb_var_screeninfo def_var = {
#ifdef  CONFIG_LOGO_LINUX_BMP
	.red    	= {16,8,0},
	.green  	= {8,8,0},
	.blue   	= {0,8,0},
	.transp 	= {0,0,0},
	.nonstd 	= HAL_PIXEL_FORMAT_BGRA_8888,
#else
	.red		= {11,5,0},
	.green  	= {5,6,0},
	.blue   	= {0,5,0},
	.transp 	= {0,0,0},
	.nonstd 	= HAL_PIXEL_FORMAT_RGB_565,   //(ypos<<20+xpos<<8+format) format
#endif
	.grayscale	= 0,  //(ysize<<20+xsize<<8)
	.activate    	= FB_ACTIVATE_NOW,
	.accel_flags 	= 0,
	.vmode       	= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo def_fix = {
	.type		 = FB_TYPE_PACKED_PIXELS,
	.type_aux	 = 0,
	.xpanstep	 = 1,
	.ypanstep	 = 1,
	.ywrapstep	 = 0,
	.accel		 = FB_ACCEL_NONE,
	.visual 	 = FB_VISUAL_TRUECOLOR,
		
};


static int rk_fb_wait_for_vsync_thread(void *data)
{
	struct rk_lcdc_device_driver  *dev_drv = data;
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	struct fb_info *fbi = inf->fb[0];

	while (!kthread_should_stop()) {
		ktime_t timestamp = dev_drv->vsync_info.timestamp;
		int ret = wait_event_interruptible(dev_drv->vsync_info.wait,
			!ktime_equal(timestamp, dev_drv->vsync_info.timestamp) &&
			dev_drv->vsync_info.active);

		if (!ret) {
			sysfs_notify(&fbi->dev->kobj, NULL, "vsync");
		}
	}

	return 0;
}

static ssize_t rk_fb_vsync_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			ktime_to_ns(dev_drv->vsync_info.timestamp));
}

static DEVICE_ATTR(vsync, S_IRUGO, rk_fb_vsync_show, NULL);

static int rk_request_fb_buffer(struct fb_info *fbi,int fb_id)
{
	struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )fbi->par;
    	struct layer_par *par = NULL;
	int layer_id;
	struct resource *res;
	struct resource *mem;
	int ret = 0;
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	if (!strcmp(fbi->fix.id,"fb0"))
	{
		res = platform_get_resource_byname(g_fb_pdev, IORESOURCE_MEM, "fb0 buf");
		if (res == NULL)
		{
			dev_err(&g_fb_pdev->dev, "failed to get memory for fb0 \n");
			ret = -ENOENT;
		}
		fbi->fix.smem_start = res->start;
		fbi->fix.smem_len = res->end - res->start + 1;
		mem = request_mem_region(res->start, resource_size(res), g_fb_pdev->name);
		fbi->screen_base = ioremap(res->start, fbi->fix.smem_len);
		memset(fbi->screen_base, 0, fbi->fix.smem_len);
		printk("fb%d:phy:%lx>>vir:%p>>len:0x%x\n",fb_id,
		fbi->fix.smem_start,fbi->screen_base,fbi->fix.smem_len);
	}
	else
	{	
#if defined(CONFIG_FB_ROTATE) || !defined(CONFIG_THREE_FB_BUFFER)
		res = platform_get_resource_byname(g_fb_pdev, IORESOURCE_MEM, "fb2 buf");
		if (res == NULL)
		{
			dev_err(&g_fb_pdev->dev, "failed to get win0 memory \n");
			ret = -ENOENT;
		}
		fbi->fix.smem_start = res->start;
		fbi->fix.smem_len = res->end - res->start + 1;
		mem = request_mem_region(res->start, resource_size(res), g_fb_pdev->name);
		fbi->screen_base = ioremap(res->start, fbi->fix.smem_len);
		memset(fbi->screen_base, 0, fbi->fix.smem_len);
#else    //three buffer no need to copy
		fbi->fix.smem_start = fb_inf->fb[0]->fix.smem_start;
		fbi->fix.smem_len   = fb_inf->fb[0]->fix.smem_len;
		fbi->screen_base    = fb_inf->fb[0]->screen_base;
#endif
		printk("fb%d:phy:%lx>>vir:%p>>len:0x%x\n",fb_id,
			fbi->fix.smem_start,fbi->screen_base,fbi->fix.smem_len);	
	}

	fbi->screen_size = fbi->fix.smem_len;
	layer_id = dev_drv->fb_get_layer(dev_drv,fbi->fix.id);
	if(layer_id >= 0)
	{
		par = dev_drv->layer_par[layer_id];
		par->reserved = fbi->fix.smem_start;
	}

    return ret;
}

static int rk_release_fb_buffer(struct fb_info *fbi)
{
	if(!fbi)
	{
		printk("no need release null fb buffer!\n");
		return -EINVAL;
	}
	if(!strcmp(fbi->fix.id,"fb1")||!strcmp(fbi->fix.id,"fb3"))  //buffer for fb1 and fb3 are alloc by android
		return 0;
	iounmap(fbi->screen_base);
	release_mem_region(fbi->fix.smem_start,fbi->fix.smem_len);
	return 0;
	
}

static int init_layer_par(struct rk_lcdc_device_driver *dev_drv)
{
	int i;
	struct layer_par * def_par = NULL;
	int num_par = dev_drv->num_layer;
	for(i = 0; i < num_par; i++)
	{
		struct layer_par *par = NULL;
		par =  kzalloc(sizeof(struct layer_par), GFP_KERNEL);
		if(!par)
		{
			printk(KERN_ERR "kzmalloc for layer_par fail!");
			return   -ENOMEM;
		}
		def_par = &dev_drv->def_layer_par[i];
		strcpy(par->name,def_par->name);
		par->id = def_par->id;
		par->support_3d = def_par->support_3d;
		dev_drv->layer_par[i] = par;
	}
	return 0;
}

static int init_lcdc_device_driver(struct rk_lcdc_device_driver *dev_drv,
	struct rk_lcdc_device_driver *def_drv,int id)
{
	if(!def_drv)
	{
		printk(KERN_ERR "default lcdc device driver is null!\n");
		return -EINVAL;
	}
	if(!dev_drv)
	{
		printk(KERN_ERR "lcdc device driver is null!\n");
		return -EINVAL;	
	}
	sprintf(dev_drv->name, "lcdc%d",id);
	dev_drv->id		= id;
	dev_drv->open      	= def_drv->open;
	dev_drv->init_lcdc 	= def_drv->init_lcdc;
	dev_drv->ioctl 		= def_drv->ioctl;
	dev_drv->blank 		= def_drv->blank;
	dev_drv->set_par 	= def_drv->set_par;
	dev_drv->pan_display 	= def_drv->pan_display;
	dev_drv->suspend 	= def_drv->suspend;
	dev_drv->resume 	= def_drv->resume;
	dev_drv->load_screen 	= def_drv->load_screen;
	dev_drv->def_layer_par 	= def_drv->def_layer_par;
	dev_drv->num_layer	= def_drv->num_layer;
	dev_drv->get_layer_state= def_drv->get_layer_state;
	dev_drv->get_disp_info  = def_drv->get_disp_info;
	dev_drv->ovl_mgr	= def_drv->ovl_mgr;
	dev_drv->fps_mgr	= def_drv->fps_mgr;
	if(def_drv->fb_get_layer)
		dev_drv->fb_get_layer   = def_drv->fb_get_layer;
	if(def_drv->fb_layer_remap)
		dev_drv->fb_layer_remap = def_drv->fb_layer_remap;
	if(def_drv->set_dsp_lut)
		dev_drv->set_dsp_lut    = def_drv->set_dsp_lut;
	if(def_drv->read_dsp_lut)
		dev_drv->read_dsp_lut   = def_drv->read_dsp_lut;
	if(def_drv->lcdc_hdmi_process)
		dev_drv->lcdc_hdmi_process = def_drv->lcdc_hdmi_process;
	if(def_drv->lcdc_reg_update)
		dev_drv->lcdc_reg_update = def_drv->lcdc_reg_update;
	if(def_drv->poll_vblank)
		dev_drv->poll_vblank = def_drv->poll_vblank;
	if(def_drv->dpi_open)
		dev_drv->dpi_open = def_drv->dpi_open;
	if(def_drv->dpi_layer_sel)
		dev_drv->dpi_layer_sel = def_drv->dpi_layer_sel;
	if(def_drv->dpi_status)
		dev_drv->dpi_status = def_drv->dpi_status;
	dev_drv->overscan.left = 100;
	dev_drv->overscan.right = 100;
	dev_drv->overscan.top = 100;
	dev_drv->overscan.bottom = 100;
	init_layer_par(dev_drv);
	init_completion(&dev_drv->frame_done);
	spin_lock_init(&dev_drv->cpl_lock);
	mutex_init(&dev_drv->fb_win_id_mutex);
	dev_drv->fb_layer_remap(dev_drv,FB_DEFAULT_ORDER); //102
	dev_drv->first_frame = 1;
	
	return 0;
}
 
#ifdef CONFIG_LOGO_LINUX_BMP
static struct linux_logo *bmp_logo;
static int fb_prepare_bmp_logo(struct fb_info *info, int rotate)
{
	bmp_logo = fb_find_logo(24);
	if (bmp_logo == NULL) {
		printk("%s error\n", __func__);
		return 0;
	}
	return 1;
}

static void fb_show_bmp_logo(struct fb_info *info, int rotate)
{
	unsigned char *src=bmp_logo->data;
	unsigned char *dst=info->screen_base;
	int i;
	unsigned int Needwidth=(*(src-24)<<8)|(*(src-23));
	unsigned int Needheight=(*(src-22)<<8)|(*(src-21));
		
	for(i=0;i<Needheight;i++)
		memcpy(dst+info->var.xres*i*4, src+bmp_logo->width*i*4, Needwidth*4);
	
}
#endif

int rk_fb_register(struct rk_lcdc_device_driver *dev_drv,
	struct rk_lcdc_device_driver *def_drv,int id)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	struct fb_info *fbi;
	int i=0,ret = 0;
	int lcdc_id = 0;
	if(NULL == dev_drv)
	{
		printk("null lcdc device driver?");
		return -ENOENT;
	}
	
	for(i=0; i<RK30_MAX_LCDC_SUPPORT; i++)
	{
		if(NULL==fb_inf->lcdc_dev_drv[i])
		{
			fb_inf->lcdc_dev_drv[i] = dev_drv;
			fb_inf->lcdc_dev_drv[i]->id = id;
			fb_inf->num_lcdc++;
			break;
        }
	}
	
	if(i==RK30_MAX_LCDC_SUPPORT)
	{
		printk("rk_fb_register lcdc out of support %d",i);
		return -ENOENT;
	}
	
	lcdc_id = i;
	init_lcdc_device_driver(dev_drv, def_drv, id);
	
	dev_drv->init_lcdc(dev_drv);
	/************fb set,one layer one fb ***********/
	dev_drv->fb_index_base = fb_inf->num_fb;
	for(i=0; i< dev_drv->num_layer; i++)
	{
		fbi= framebuffer_alloc(0, &g_fb_pdev->dev);
		if(!fbi)
		{
		    dev_err(&g_fb_pdev->dev,">> fb framebuffer_alloc fail!");
		    fbi = NULL;
		    ret = -ENOMEM;
		}
		fbi->par = dev_drv;
		fbi->var = def_var;
		fbi->fix = def_fix;
		sprintf(fbi->fix.id,"fb%d",fb_inf->num_fb);
		fbi->var.xres = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->x_res;
		fbi->var.yres = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->y_res;
		fbi->var.grayscale |= (fbi->var.xres<<8) + (fbi->var.yres<<20);
#ifdef  CONFIG_LOGO_LINUX_BMP
		fbi->var.bits_per_pixel = 32; 
#else
		fbi->var.bits_per_pixel = 16; 
#endif
		fbi->fix.line_length  = (fbi->var.xres)*(fbi->var.bits_per_pixel>>3);
		fbi->var.xres_virtual = fbi->var.xres;
		fbi->var.yres_virtual = fbi->var.yres;
		fbi->var.width =  fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->width;
		fbi->var.height = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->height;
		fbi->var.pixclock = fb_inf->lcdc_dev_drv[lcdc_id]->pixclock;
		fbi->var.left_margin = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->left_margin;
		fbi->var.right_margin = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->right_margin;
		fbi->var.upper_margin = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->upper_margin;
		fbi->var.lower_margin = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->lower_margin;
		fbi->var.vsync_len = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->vsync_len;
		fbi->var.hsync_len = fb_inf->lcdc_dev_drv[lcdc_id]->cur_screen->hsync_len;
		fbi->fbops			 = &fb_ops;
		fbi->flags			 = FBINFO_FLAG_DEFAULT;
		fbi->pseudo_palette  = fb_inf->lcdc_dev_drv[lcdc_id]->layer_par[i]->pseudo_pal;
		if(i == 0)
			rk_request_fb_buffer(fbi,fb_inf->num_fb);
		ret = register_framebuffer(fbi);
		if(ret < 0)
		{
	    	printk("%s>>fb%d register_framebuffer fail!\n",__func__,fb_inf->num_fb);
	    	ret = -EINVAL;
		}
		rkfb_create_sysfs(fbi);
		fb_inf->fb[fb_inf->num_fb] = fbi;
		printk("%s>>>>>%s\n",__func__,fb_inf->fb[fb_inf->num_fb]->fix.id);
		fb_inf->num_fb++;
		
		if(i == 0)
		{
			init_waitqueue_head(&dev_drv->vsync_info.wait);
			ret = device_create_file(fbi->dev,&dev_attr_vsync);
			if (ret) 
			{
				dev_err(fbi->dev, "failed to create vsync file\n");
			}
			dev_drv->vsync_info.thread = kthread_run(rk_fb_wait_for_vsync_thread,
				dev_drv, "fb-vsync");
			
			if (dev_drv->vsync_info.thread == ERR_PTR(-ENOMEM)) 
			{
				dev_err(fbi->dev, "failed to run vsync thread\n");
				dev_drv->vsync_info.thread = NULL;
			}
			dev_drv->vsync_info.active = 1;
			fbi->fbops->fb_open(fbi, 1);
		}
			
	}
#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
    if(dev_drv->screen_ctr_info->prop == PRMRY) //show logo for primary display device
    {
	    fb_inf->fb[0]->fbops->fb_open(fb_inf->fb[0],1);
	    fb_inf->fb[0]->fbops->fb_set_par(fb_inf->fb[0]);

#if  defined(CONFIG_LOGO_LINUX_BMP)
		if(fb_prepare_bmp_logo(fb_inf->fb[0], FB_ROTATE_UR)) {
			/* Start display and show logo on boot */
			fb_set_cmap(&fb_inf->fb[0]->cmap, fb_inf->fb[0]);
			fb_show_bmp_logo(fb_inf->fb[0], FB_ROTATE_UR);
			fb_inf->fb[0]->fbops->fb_pan_display(&(fb_inf->fb[0]->var), fb_inf->fb[0]);
		}
#else
		if(fb_prepare_logo(fb_inf->fb[0], FB_ROTATE_UR)) {
			/* Start display and show logo on boot */
			fb_set_cmap(&fb_inf->fb[0]->cmap, fb_inf->fb[0]);
			fb_show_logo(fb_inf->fb[0], FB_ROTATE_UR);
			fb_inf->fb[0]->fbops->fb_pan_display(&(fb_inf->fb[0]->var), fb_inf->fb[0]);
		}
#endif
		fb_inf->fb[0]->fbops->fb_ioctl(fb_inf->fb[0],RK_FBIOSET_CONFIG_DONE,0);
    }
#endif
	return 0;	
}

int rk_fb_unregister(struct rk_lcdc_device_driver *dev_drv)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	struct fb_info *fbi;
	int fb_index_base = dev_drv->fb_index_base;
	int fb_num = dev_drv->num_layer;
	int i=0;
	if(NULL == dev_drv)
	{
		printk(" no need to unregister null lcdc device driver!\n");
		return -ENOENT;
	}

	for(i = 0; i < fb_num; i++)
	{
		kfree(dev_drv->layer_par[i]);
	}

	for(i=fb_index_base;i<(fb_index_base+fb_num);i++)
	{
		fbi = fb_inf->fb[i];
		unregister_framebuffer(fbi);
		//rk_release_fb_buffer(fbi);
		framebuffer_release(fbi);	
	}
	fb_inf->lcdc_dev_drv[dev_drv->id]= NULL;
	fb_inf->num_lcdc--;

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
struct suspend_info {
	struct early_suspend early_suspend;
	struct rk_fb_inf *inf;
};

static void rkfb_early_suspend(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);
	struct rk_fb_inf *inf = info->inf;
	int i;
	for(i = 0; i < inf->num_lcdc; i++)
	{
		if (!inf->lcdc_dev_drv[i])
			continue;
			
		inf->lcdc_dev_drv[i]->suspend(inf->lcdc_dev_drv[i]);
	}
}

static void rkfb_early_resume(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);
	struct rk_fb_inf *inf = info->inf;
	int i;
	for(i = 0; i < inf->num_lcdc; i++)
	{
		if (!inf->lcdc_dev_drv[i])
			continue;
		
		inf->lcdc_dev_drv[i]->resume(inf->lcdc_dev_drv[i]);	       // data out
	}

}

static struct suspend_info suspend_info = {
	.early_suspend.suspend = rkfb_early_suspend,
	.early_suspend.resume = rkfb_early_resume,
	.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};
#endif

/*****************************************************************
this two function is for other module that in the kernel which
need show image directly through fb
fb_id:we have 4 fb here,default we use fb0 for ui display
*******************************************************************/
struct fb_info * rk_get_fb(int fb_id)
{
    struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
    struct fb_info *fb = inf->fb[fb_id];
    return fb;
}
EXPORT_SYMBOL(rk_get_fb);

void rk_direct_fb_show(struct fb_info * fbi)
{
    rk_fb_set_par(fbi);
    rk_pan_display(&fbi->var, fbi);
}
EXPORT_SYMBOL(rk_direct_fb_show);

static int __devinit rk_fb_probe (struct platform_device *pdev)
{
	struct rk_fb_inf *fb_inf = NULL;
	int ret = 0;
	g_fb_pdev = pdev;
    /* Malloc rk_fb_inf and set it to pdev for drvdata */
	fb_inf = kzalloc(sizeof(struct rk_fb_inf), GFP_KERNEL);
	if(!fb_inf)
	{
		dev_err(&pdev->dev, ">>fb inf kmalloc fail!");
		ret = -ENOMEM;
	}
	platform_set_drvdata(pdev,fb_inf);

#ifdef CONFIG_HAS_EARLYSUSPEND
	suspend_info.inf = fb_inf;
	register_early_suspend(&suspend_info.early_suspend);
#endif
	printk("rk fb probe ok!\n");
    return 0;
}

static int __devexit rk_fb_remove(struct platform_device *pdev)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(pdev);
	if(fb_inf)
		kfree(fb_inf);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void rk_fb_shutdown(struct platform_device *pdev)
{
	struct rk_fb_inf *inf = platform_get_drvdata(pdev);
	int i;
	for(i = 0; i < inf->num_lcdc; i++)
	{
		if (!inf->lcdc_dev_drv[i])
			continue;

//		if(inf->lcdc_dev_drv[i]->vsync_info.thread)
//			kthread_stop(inf->lcdc_dev_drv[i]->vsync_info.thread);
	}
//	kfree(fb_inf);
//	platform_set_drvdata(pdev, NULL);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&suspend_info.early_suspend);
#endif
}

static struct platform_driver rk_fb_driver = {
	.probe		= rk_fb_probe,
	.remove		= __devexit_p(rk_fb_remove),
	.driver		= {
		.name	= "rk-fb",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk_fb_shutdown,
};

static int __init rk_fb_init(void)
{
    return platform_driver_register(&rk_fb_driver);
}

static void __exit rk_fb_exit(void)
{
    platform_driver_unregister(&rk_fb_driver);
}

subsys_initcall_sync(rk_fb_init);
module_exit(rk_fb_exit);

