/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/component.h>

#include "rockchip_drm_vop.h"
#include "rockchip_vop_reg.h"

#define VOP_REG_VER_MASK(off, _mask, s, _write_mask, _major, \
		         _begin_minor, _end_minor) \
		{.offset = off, \
		 .mask = _mask, \
		 .shift = s, \
		 .write_mask = _write_mask, \
		 .major = _major, \
		 .begin_minor = _begin_minor, \
		 .end_minor = _end_minor,}

#define VOP_REG(off, _mask, s) \
		VOP_REG_VER_MASK(off, _mask, s, false, 0, 0, -1)

#define VOP_REG_MASK(off, _mask, s) \
		VOP_REG_VER_MASK(off, _mask, s, true, 0, 0, -1)

#define VOP_REG_VER(off, _mask, s, _major, _begin_minor, _end_minor) \
		VOP_REG_VER_MASK(off, _mask, s, false, \
				 _major, _begin_minor, _end_minor)


static const uint32_t formats_win_full[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV24,
};

static const uint32_t formats_win_lite[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

static const struct vop_scl_extension rk3288_win_full_scl_ext = {
	.cbcr_vsd_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 31),
	.cbcr_vsu_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 30),
	.cbcr_hsd_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 28),
	.cbcr_ver_scl_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 26),
	.cbcr_hor_scl_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 24),
	.yrgb_vsd_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 23),
	.yrgb_vsu_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 22),
	.yrgb_hsd_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 20),
	.yrgb_ver_scl_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 18),
	.yrgb_hor_scl_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 16),
	.line_load_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 15),
	.cbcr_axi_gather_num = VOP_REG(RK3288_WIN0_CTRL1, 0x7, 12),
	.yrgb_axi_gather_num = VOP_REG(RK3288_WIN0_CTRL1, 0xf, 8),
	.vsd_cbcr_gt2 = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 7),
	.vsd_cbcr_gt4 = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 6),
	.vsd_yrgb_gt2 = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 5),
	.vsd_yrgb_gt4 = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 4),
	.bic_coe_sel = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 2),
	.cbcr_axi_gather_en = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 1),
	.yrgb_axi_gather_en = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 0),
	.lb_mode = VOP_REG(RK3288_WIN0_CTRL0, 0x7, 5),
};

static const struct vop_scl_regs rk3288_win_full_scl = {
	.ext = &rk3288_win_full_scl_ext,
	.scale_yrgb_x = VOP_REG(RK3288_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3288_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3288_WIN0_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3288_WIN0_SCL_FACTOR_CBR, 0xffff, 16),
};

static const struct vop_win_phy rk3288_win01_data = {
	.scl = &rk3288_win_full_scl,
	.data_formats = formats_win_full,
	.nformats = ARRAY_SIZE(formats_win_full),
	.enable = VOP_REG(RK3288_WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3288_WIN0_CTRL0, 0x7, 1),
	.fmt_10 = VOP_REG(RK3288_WIN0_CTRL0, 0x7, 4),
	.csc_mode = VOP_REG_VER(RK3288_WIN0_CTRL0, 0x3, 10, 3, 2, -1),
	.rb_swap = VOP_REG(RK3288_WIN0_CTRL0, 0x1, 12),
	.xmirror = VOP_REG_VER(RK3368_WIN0_CTRL0, 0x1, 21, 3, 2, -1),
	.ymirror = VOP_REG_VER(RK3368_WIN0_CTRL0, 0x1, 22, 3, 2, -1),
	.act_info = VOP_REG(RK3288_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3288_WIN0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3288_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN0_VIR, 0x3fff, 0),
	.uv_vir = VOP_REG(RK3288_WIN0_VIR, 0x3fff, 16),
	.src_alpha_ctl = VOP_REG(RK3288_WIN0_SRC_ALPHA_CTRL, 0xffff, 0),
	.global_alpha_val = VOP_REG(RK3288_WIN0_DST_ALPHA_CTRL, 0xff, 16),
	.dst_alpha_ctl = VOP_REG(RK3288_WIN0_DST_ALPHA_CTRL, 0xffffffff, 0),
	.channel = VOP_REG_VER(RK3288_WIN0_CTRL2, 0xff, 0, 3, 8, 8),
};

static const struct vop_win_phy rk3288_win23_data = {
	.data_formats = formats_win_lite,
	.nformats = ARRAY_SIZE(formats_win_lite),
	.gate = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 0),
	.enable = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 4),
	.format = VOP_REG(RK3288_WIN2_CTRL0, 0x7, 1),
	.rb_swap = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 12),
	.dsp_info = VOP_REG(RK3288_WIN2_DSP_INFO0, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN2_DSP_ST0, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN2_MST0, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN2_VIR0_1, 0x1fff, 0),
	.src_alpha_ctl = VOP_REG(RK3288_WIN2_SRC_ALPHA_CTRL, 0xffff, 0),
	.global_alpha_val = VOP_REG(RK3288_WIN2_SRC_ALPHA_CTRL, 0xff, 16),
	.dst_alpha_ctl = VOP_REG(RK3288_WIN2_DST_ALPHA_CTRL, 0xffffffff, 0),
};

static const struct vop_win_phy rk3288_area1_data = {
	.enable = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 5),
	.dsp_info = VOP_REG(RK3288_WIN2_DSP_INFO1, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN2_DSP_ST1, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN2_MST1, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN2_VIR0_1, 0x1fff, 16),
};

static const struct vop_win_phy rk3288_area2_data = {
	.enable = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 6),
	.dsp_info = VOP_REG(RK3288_WIN2_DSP_INFO2, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN2_DSP_ST2, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN2_MST2, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN2_VIR2_3, 0x1fff, 0),
};

static const struct vop_win_phy rk3288_area3_data = {
	.enable = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 7),
	.dsp_info = VOP_REG(RK3288_WIN2_DSP_INFO3, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN2_DSP_ST3, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN2_MST3, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN2_VIR2_3, 0x1fff, 16),
};

static const struct vop_win_phy *rk3288_area_data[] = {
	&rk3288_area1_data,
	&rk3288_area2_data,
	&rk3288_area3_data
};

static const struct vop_ctrl rk3288_ctrl_data = {
	.version = VOP_REG(RK3288_VERSION_INFO, 0xffff, 16),
	.standby = VOP_REG(RK3288_SYS_CTRL, 0x1, 22),
	.axi_outstanding_max_num = VOP_REG(RK3288_SYS_CTRL1, 0x1f, 13),
	.axi_max_outstanding_en = VOP_REG(RK3288_SYS_CTRL1, 0x1, 12),
	.htotal_pw = VOP_REG(RK3288_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3288_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3288_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3288_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3288_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3288_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.hpost_st_end = VOP_REG(RK3288_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3288_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3288_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3288_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3288_POST_SCL_CTRL, 0x3, 0),

	.dsp_interlace = VOP_REG(RK3288_DSP_CTRL0, 0x1, 10),
	.auto_gate_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 23),
	.dsp_layer_sel = VOP_REG(RK3288_DSP_CTRL1, 0xff, 8),
	.post_lb_mode = VOP_REG_VER(RK3288_SYS_CTRL, 0x1, 18, 3, 2, -1),
	.global_regdone_en = VOP_REG_VER(RK3288_SYS_CTRL, 0x1, 11, 3, 2, -1),
	.overlay_mode = VOP_REG_VER(RK3288_SYS_CTRL, 0x1, 16, 3, 2, -1),
	.core_dclk_div = VOP_REG_VER(RK3366_DSP_CTRL0, 0x1, 4, 3, 4, -1),
	.p2i_en = VOP_REG_VER(RK3366_DSP_CTRL0, 0x1, 5, 3, 4, -1),
	.dclk_ddr = VOP_REG_VER(RK3288_DSP_CTRL0, 0x1, 8, 3, 1, -1),
	.dp_en = VOP_REG_VER(RK3399_SYS_CTRL, 0x1, 11, 3, 5, -1),
	.hdmi_dclk_out_en = VOP_REG_VER(RK3288_SYS_CTRL, 0x1, 11, 3, 1, 1),
	.rgb_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 12),
	.hdmi_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 13),
	.edp_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 14),
	.mipi_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 15),
	.mipi_dual_channel_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 3),
	.data01_swap = VOP_REG_VER(RK3288_SYS_CTRL, 0x1, 17, 3, 5, -1),
	.dclk_pol = VOP_REG_VER(RK3288_DSP_CTRL0, 0x1, 7, 3, 0, 1),
	.pin_pol = VOP_REG_VER(RK3288_DSP_CTRL0, 0x7, 4, 3, 0, 1),
	.dp_dclk_pol = VOP_REG_VER(RK3399_DSP_CTRL1, 0x1, 19, 3, 5, -1),
	.dp_pin_pol = VOP_REG_VER(RK3399_DSP_CTRL1, 0x7, 16, 3, 5, -1),
	.rgb_dclk_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0x1, 19, 3, 2, -1),
	.rgb_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0x7, 16, 3, 2, -1),
	.hdmi_dclk_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0x1, 23, 3, 2, -1),
	.hdmi_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0x7, 20, 3, 2, -1),
	.edp_dclk_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0x1, 27, 3, 2, -1),
	.edp_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0x7, 24, 3, 2, -1),
	.mipi_dclk_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0x1, 31, 3, 2, -1),
	.mipi_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0x7, 28, 3, 2, -1),

	.dither_down = VOP_REG(RK3288_DSP_CTRL1, 0xf, 1),
	.dither_up = VOP_REG(RK3288_DSP_CTRL1, 0x1, 6),

	.dsp_out_yuv = VOP_REG_VER(RK3399_POST_SCL_CTRL, 0x1, 2, 3, 5, -1),
	.dsp_data_swap = VOP_REG(RK3288_DSP_CTRL0, 0x1f, 12),
	.dsp_ccir656_avg = VOP_REG(RK3288_DSP_CTRL0, 0x1, 20),
	.dsp_blank = VOP_REG(RK3288_DSP_CTRL0, 0x3, 18),
	.update_gamma_lut = VOP_REG_VER(RK3288_DSP_CTRL1, 0x1, 7, 3, 5, -1),
	.lut_buffer_index = VOP_REG_VER(RK3399_DBG_POST_REG1, 0x1, 1, 3, 5, -1),
	.dsp_lut_en = VOP_REG(RK3288_DSP_CTRL1, 0x1, 0),
	.out_mode = VOP_REG(RK3288_DSP_CTRL0, 0xf, 0),

	.afbdc_rstn = VOP_REG_VER(RK3399_AFBCD0_CTRL, 0x1, 3, 3, 5, -1),
	.afbdc_en = VOP_REG_VER(RK3399_AFBCD0_CTRL, 0x1, 0, 3, 5, -1),
	.afbdc_sel = VOP_REG_VER(RK3399_AFBCD0_CTRL, 0x3, 1, 3, 5, -1),
	.afbdc_format = VOP_REG_VER(RK3399_AFBCD0_CTRL, 0x1f, 16, 3, 5, -1),
	.afbdc_hreg_block_split = VOP_REG_VER(RK3399_AFBCD0_CTRL,
					      0x1, 21, 3, 5, -1),
	.afbdc_hdr_ptr = VOP_REG_VER(RK3399_AFBCD0_HDR_PTR, 0xffffffff,
				     0, 3, 5, -1),
	.afbdc_pic_size = VOP_REG_VER(RK3399_AFBCD0_PIC_SIZE, 0xffffffff,
				      0, 3, 5, -1),
	.cabc_config_mode = VOP_REG_VER(RK3399_CABC_CTRL0, 0x3, 2, 3, 5, -1),
	.cabc_calc_pixel_num = VOP_REG_VER(RK3399_CABC_CTRL0, 0x7fffff, 4,
					   3, 5, -1),
	.cabc_handle_en = VOP_REG_VER(RK3399_CABC_CTRL0, 0x1, 1, 3, 5, -1),
	.cabc_en = VOP_REG_VER(RK3399_CABC_CTRL0, 0x1, 0, 3, 5, -1),
	.cabc_total_num = VOP_REG_VER(RK3399_CABC_CTRL1, 0x7fffff, 4, 3, 5, -1),
	.cabc_lut_en = VOP_REG_VER(RK3399_CABC_CTRL1, 0x1, 0, 3, 5, -1),
	.cabc_stage_up_mode = VOP_REG_VER(RK3399_CABC_CTRL2, 0x1, 19, 3, 5, -1),
	.cabc_stage_up = VOP_REG_VER(RK3399_CABC_CTRL2, 0x1ff, 8, 3, 5, -1),
	.cabc_stage_down = VOP_REG_VER(RK3399_CABC_CTRL2, 0xff, 0, 3, 5, -1),
	.cabc_global_dn = VOP_REG_VER(RK3399_CABC_CTRL3, 0xff, 0, 3, 5, -1),
	.cabc_global_dn_limit_en = VOP_REG_VER(RK3399_CABC_CTRL3, 0x1, 8,
					       3, 5, -1),

	.bcsh_brightness = VOP_REG(RK3288_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3288_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3288_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3288_BCSH_BCS, 0x3, 0),
	.bcsh_sin_hue = VOP_REG(RK3288_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3288_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG_VER(RK3368_BCSH_CTRL, 0x1, 6, 3, 1, -1),
	.bcsh_r2y_en = VOP_REG_VER(RK3368_BCSH_CTRL, 0x1, 4, 3, 1, -1),
	.bcsh_y2r_csc_mode = VOP_REG_VER(RK3368_BCSH_CTRL, 0x3, 2, 3, 1, -1),
	.bcsh_y2r_en = VOP_REG_VER(RK3368_BCSH_CTRL, 0x1, 0, 3, 1, -1),
	.bcsh_color_bar = VOP_REG(RK3288_BCSH_COLOR_BAR, 0xffffff, 8),
	.bcsh_en = VOP_REG(RK3288_BCSH_COLOR_BAR, 0x1, 0),

	.xmirror = VOP_REG(RK3288_DSP_CTRL0, 0x1, 22),
	.ymirror = VOP_REG(RK3288_DSP_CTRL0, 0x1, 23),

	.dsp_background = VOP_REG(RK3288_DSP_BG, 0xffffffff, 0),

	.cfg_done = VOP_REG(RK3288_REG_CFG_DONE, 0x1, 0),
};

/*
 * Note: rk3288 has a dedicated 'cursor' window, however, that window requires
 * special support to get alpha blending working.  For now, just use overlay
 * window 3 for the drm cursor.
 *
 */
static const struct vop_win_data rk3288_vop_win_data[] = {
	{ .base = 0x00, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .base = 0x40, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_OVERLAY },
	{ .base = 0x00, .phy = &rk3288_win23_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .area = rk3288_area_data,
	  .area_size = ARRAY_SIZE(rk3288_area_data), },
	{ .base = 0x50, .phy = &rk3288_win23_data,
	  .type = DRM_PLANE_TYPE_CURSOR,
	  .area = rk3288_area_data,
	  .area_size = ARRAY_SIZE(rk3288_area_data), },
};

static const int rk3288_vop_intrs[] = {
	DSP_HOLD_VALID_INTR,
	FS_INTR,
	LINE_FLAG_INTR,
	BUS_ERROR_INTR,
};

static const struct vop_intr rk3288_vop_intr = {
	.intrs = rk3288_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3288_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3288_INTR_CTRL0, 0x1fff, 12),
	.status = VOP_REG(RK3288_INTR_CTRL0, 0xf, 0),
	.enable = VOP_REG(RK3288_INTR_CTRL0, 0xf, 4),
	.clear = VOP_REG(RK3288_INTR_CTRL0, 0xf, 8),
};

static const struct vop_data rk3288_vop = {
	.version = VOP_VERSION(3, 0),
	.feature = VOP_FEATURE_OUTPUT_10BIT,
	.max_input = {4096, 8192},
	/*
	 * TODO: rk3288 have two vop, big one support 3840x2160,
	 * little one only support 2560x1600.
	 * Now force use 3840x2160.
	 */
	.max_output = {3840, 2160},
	.intr = &rk3288_vop_intr,
	.ctrl = &rk3288_ctrl_data,
	.win = rk3288_vop_win_data,
	.win_size = ARRAY_SIZE(rk3288_vop_win_data),
};

static const int rk3368_vop_intrs[] = {
	FS_INTR,
	FS_NEW_INTR,
	ADDR_SAME_INTR,
	LINE_FLAG_INTR,
	LINE_FLAG1_INTR,
	BUS_ERROR_INTR,
	WIN0_EMPTY_INTR,
	WIN1_EMPTY_INTR,
	WIN2_EMPTY_INTR,
	WIN3_EMPTY_INTR,
	HWC_EMPTY_INTR,
	POST_BUF_EMPTY_INTR,
	PWM_GEN_INTR,
	DSP_HOLD_VALID_INTR,
};

static const u32 sdr2hdr_bt1886eotf_yn_for_hlg_hdr[65] = {
	0,
	1,	7,	17,	35,
	60,	92,	134,	184,
	244,	315,	396,	487,
	591,	706,	833,	915,
	1129,	1392,	1717,	2118,
	2352,	2612,	2900,	3221,
	3577,	3972,	4411,	4899,
	5441,	6042,	6710,	7452,
	7853,	8276,	8721,	9191,
	9685,	10207,	10756,	11335,
	11945,	12588,	13266,	13980,
	14732,	15525,	16361,	17241,
	17699,	18169,	18652,	19147,
	19656,	20178,	20714,	21264,
	21829,	22408,	23004,	23615,
	24242,	24886,	25547,	26214,
};

static const u32 sdr2hdr_bt1886eotf_yn_for_bt2020[65] = {
	0,
	1820,   3640,   5498,   7674,
	10256,  13253,  16678,  20539,
	24847,  29609,  34833,  40527,
	46699,  53354,  60499,  68141,
	76285,  84937,  94103,  103787,
	108825, 113995, 119296, 124731,
	130299, 136001, 141837, 147808,
	153915, 160158, 166538, 173055,
	176365, 179709, 183089, 186502,
	189951, 193434, 196952, 200505,
	204093, 207715, 211373, 215066,
	218795, 222558, 226357, 230191,
	232121, 234060, 236008, 237965,
	239931, 241906, 243889, 245882,
	247883, 249894, 251913, 253941,
	255978, 258024, 260079, 262143,
};

static u32 sdr2hdr_bt1886eotf_yn_for_hdr[65] = {
	/* dst_range 425int */
	0,
	5,     21,    49,     91,
	150,   225,   320,   434,
	569,   726,   905,   1108,
	1336,  1588,  1866,  2171,
	2502,  2862,  3250,  3667,
	3887,  4114,  4349,  4591,
	4841,  5099,  5364,  5638,
	5920,  6209,  6507,  6812,
	6968,  7126,  7287,  7449,
	7613,  7779,  7948,  8118,
	8291,  8466,  8643,  8822,
	9003,  9187,  9372,  9560,
	9655,  9750,  9846,  9942,
	10039, 10136, 10234, 10333,
	10432, 10531, 10631, 10732,
	10833, 10935, 11038, 11141,
};

static const u32 sdr2hdr_st2084oetf_yn_for_hlg_hdr[65] = {
	0,
	668,	910,	1217,	1600,
	2068,	2384,	2627,	3282,
	3710,	4033,	4879,	5416,
	5815,	6135,	6401,	6631,
	6833,	7176,	7462,	7707,
	7921,	8113,	8285,	8442,
	8586,	8843,	9068,	9268,
	9447,	9760,	10027,	10259,
	10465,	10650,	10817,	10971,
	11243,	11480,	11689,	11877,
	12047,	12202,	12345,	12477,
	12601,	12716,	12926,	13115,
	13285,	13441,	13583,	13716,
	13839,	13953,	14163,	14350,
	14519,	14673,	14945,	15180,
	15570,	15887,	16153,	16383,
};

static const u32 sdr2hdr_st2084oetf_yn_for_bt2020[65] = {
	0,
	0,     0,     1,     2,
	4,     6,     9,     18,
	27,    36,    72,    108,
	144,   180,   216,   252,
	288,   360,   432,   504,
	576,   648,   720,   792,
	864,   1008,  1152,  1296,
	1444,  1706,  1945,  2166,
	2372,  2566,  2750,  2924,
	3251,  3553,  3834,  4099,
	4350,  4588,  4816,  5035,
	5245,  5447,  5832,  6194,
	6536,  6862,  7173,  7471,
	7758,  8035,  8560,  9055,
	9523,  9968,  10800, 11569,
	12963, 14210, 15347, 16383,
};

static u32 sdr2hdr_st2084oetf_yn_for_hdr[65] = {
	0,
	281,   418,   610,   871,
	1217,  1464,  1662,  2218,
	2599,  2896,  3699,  4228,
	4628,  4953,  5227,  5466,
	5676,  6038,  6341,  6602,
	6833,  7039,  7226,  7396,
	7554,  7835,  8082,  8302,
	8501,  8848,  9145,  9405,
	9635,  9842,  10031, 10204,
	10512, 10779, 11017, 11230,
	11423, 11599, 11762, 11913,
	12054, 12185, 12426, 12641,
	12835, 13013, 13177, 13328,
	13469, 13600, 13840, 14055,
	14248, 14425, 14737, 15006,
	15453, 15816, 16121, 16383,
};

static const u32 sdr2hdr_st2084oetf_dxn_pow2[64] = {
	0,  0,  1,  2,
	3,  3,  3,  5,
	5,  5,  7,  7,
	7,  7,  7,  7,
	7,  8,  8,  8,
	8,  8,  8,  8,
	8,  9,  9,  9,
	9,  10, 10, 10,
	10, 10, 10, 10,
	11, 11, 11, 11,
	11, 11, 11, 11,
	11, 11, 12, 12,
	12, 12, 12, 12,
	12, 12, 13, 13,
	13, 13, 14, 14,
	15, 15, 15, 15,
};

static const u32 sdr2hdr_st2084oetf_dxn[64] = {
	1,     1,     2,     4,
	8,     8,     8,     32,
	32,    32,    128,   128,
	128,   128,   128,   128,
	128,   256,   256,   256,
	256,   256,   256,   256,
	256,   512,   512,   512,
	512,   1024,  1024,  1024,
	1024,  1024,  1024,  1024,
	2048,  2048,  2048,  2048,
	2048,  2048,  2048,  2048,
	2048,  2048,  4096,  4096,
	4096,  4096,  4096,  4096,
	4096,  4096,  8192,  8192,
	8192,  8192,  16384, 16384,
	32768, 32768, 32768, 32768,
};

static const u32 sdr2hdr_st2084oetf_xn[63] = {
	1,      2,      4,      8,
	16,     24,     32,     64,
	96,     128,    256,    384,
	512,    640,    768,    896,
	1024,   1280,   1536,   1792,
	2048,   2304,   2560,   2816,
	3072,   3584,   4096,   4608,
	5120,   6144,   7168,   8192,
	9216,   10240,  11264,  12288,
	14336,  16384,  18432,  20480,
	22528,  24576,  26624,  28672,
	30720,  32768,  36864,  40960,
	45056,  49152,  53248,  57344,
	61440,  65536,  73728,  81920,
	90112,  98304,  114688, 131072,
	163840, 196608, 229376,
};

static u32 hdr2sdr_eetf_yn[33] = {
	1716,
	1880,	2067,	2277,	2508,
	2758,	3026,	3310,	3609,
	3921,	4246,	4581,	4925,
	5279,	5640,	6007,	6380,
	6758,	7140,	7526,	7914,
	8304,	8694,	9074,	9438,
	9779,	10093,	10373,	10615,
	10812,	10960,	11053,	11084,
};

static u32 hdr2sdr_bt1886oetf_yn[33] = {
	0,
	0,	0,	0,	0,
	0,	0,	0,	314,
	746,	1323,	2093,	2657,
	3120,	3519,	3874,	4196,
	4492,	5024,	5498,	5928,
	6323,	7034,	7666,	8239,
	8766,	9716,	10560,	11325,
	12029,	13296,	14422,	16383,
};

static const u32 hdr2sdr_sat_yn[9] = {
	0,
	1792, 3584, 3472, 2778,
	2083, 1389, 694,  0,
};

static const struct vop_hdr_table rk3328_hdr_table = {
	.hdr2sdr_eetf_oetf_y0_offset = RK3328_HDR2SDR_EETF_OETF_Y0,
	.hdr2sdr_eetf_oetf_y1_offset = RK3328_HDR2SDR_EETF_OETF_Y1,
	.hdr2sdr_eetf_yn	= hdr2sdr_eetf_yn,
	.hdr2sdr_bt1886oetf_yn	= hdr2sdr_bt1886oetf_yn,
	.hdr2sdr_sat_y0_offset = RK3328_HDR2DR_SAT_Y0,
	.hdr2sdr_sat_y1_offset = RK3328_HDR2DR_SAT_Y1,
	.hdr2sdr_sat_yn = hdr2sdr_sat_yn,

	.hdr2sdr_src_range_min = 494,
	.hdr2sdr_src_range_max = 12642,
	.hdr2sdr_normfaceetf = 1327,
	.hdr2sdr_dst_range_min = 4,
	.hdr2sdr_dst_range_max = 3276,
	.hdr2sdr_normfacgamma = 5120,

	.sdr2hdr_eotf_oetf_y0_offset = RK3328_SDR2HDR_EOTF_OETF_Y0,
	.sdr2hdr_eotf_oetf_y1_offset = RK3328_SDR2HDR_EOTF_OETF_Y1,
	.sdr2hdr_bt1886eotf_yn_for_hlg_hdr = sdr2hdr_bt1886eotf_yn_for_hlg_hdr,
	.sdr2hdr_bt1886eotf_yn_for_bt2020 = sdr2hdr_bt1886eotf_yn_for_bt2020,
	.sdr2hdr_bt1886eotf_yn_for_hdr = sdr2hdr_bt1886eotf_yn_for_hdr,
	.sdr2hdr_st2084oetf_yn_for_hlg_hdr = sdr2hdr_st2084oetf_yn_for_hlg_hdr,
	.sdr2hdr_st2084oetf_yn_for_bt2020 = sdr2hdr_st2084oetf_yn_for_bt2020,
	.sdr2hdr_st2084oetf_yn_for_hdr = sdr2hdr_st2084oetf_yn_for_hdr,
	.sdr2hdr_oetf_dx_dxpow1_offset = RK3328_SDR2HDR_OETF_DX_DXPOW1,
	.sdr2hdr_oetf_xn1_offset = RK3328_SDR2HDR_OETF_XN1,
	.sdr2hdr_st2084oetf_dxn_pow2 = sdr2hdr_st2084oetf_dxn_pow2,
	.sdr2hdr_st2084oetf_dxn = sdr2hdr_st2084oetf_dxn,
	.sdr2hdr_st2084oetf_xn = sdr2hdr_st2084oetf_xn,
};

static const struct vop_ctrl rk3328_ctrl_data = {
	.standby = VOP_REG(RK3328_SYS_CTRL, 0x1, 22),
	.axi_outstanding_max_num = VOP_REG(RK3328_SYS_CTRL1, 0x1f, 13),
	.axi_max_outstanding_en = VOP_REG(RK3328_SYS_CTRL1, 0x1, 12),
	.reg_done_frm = VOP_REG(RK3328_SYS_CTRL1, 0x1, 24),
	.auto_gate_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 23),
	.htotal_pw = VOP_REG(RK3328_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3328_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3328_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3328_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3328_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3328_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.hpost_st_end = VOP_REG(RK3328_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3328_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3328_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3328_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3328_POST_SCL_CTRL, 0x3, 0),
	.dsp_out_yuv = VOP_REG(RK3328_POST_SCL_CTRL, 0x1, 2),
	.dsp_interlace = VOP_REG(RK3328_DSP_CTRL0, 0x1, 10),
	.dsp_layer_sel = VOP_REG(RK3328_DSP_CTRL1, 0xff, 8),
	.post_lb_mode = VOP_REG(RK3328_SYS_CTRL, 0x1, 18),
	.global_regdone_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 11),
	.overlay_mode = VOP_REG(RK3328_SYS_CTRL, 0x1, 16),
	.core_dclk_div = VOP_REG(RK3328_DSP_CTRL0, 0x1, 4),
	.dclk_ddr = VOP_REG(RK3328_DSP_CTRL0, 0x1, 8),
	.p2i_en = VOP_REG(RK3328_DSP_CTRL0, 0x1, 5),
	.rgb_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 12),
	.hdmi_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 13),
	.edp_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 14),
	.mipi_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 15),
	.tve_dclk_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 24),
	.tve_dclk_pol = VOP_REG(RK3328_SYS_CTRL, 0x1, 25),
	.tve_sw_mode = VOP_REG(RK3328_SYS_CTRL, 0x1, 26),
	.sw_uv_offset_en  = VOP_REG(RK3328_SYS_CTRL, 0x1, 27),
	.sw_genlock   = VOP_REG(RK3328_SYS_CTRL, 0x1, 28),
	.sw_dac_sel = VOP_REG(RK3328_SYS_CTRL, 0x1, 29),
	.rgb_pin_pol = VOP_REG(RK3328_DSP_CTRL1, 0x7, 16),
	.hdmi_pin_pol = VOP_REG(RK3328_DSP_CTRL1, 0x7, 20),
	.edp_pin_pol = VOP_REG(RK3328_DSP_CTRL1, 0x7, 24),
	.mipi_pin_pol = VOP_REG(RK3328_DSP_CTRL1, 0x7, 28),
	.rgb_dclk_pol = VOP_REG(RK3328_DSP_CTRL1, 0x1, 19),
	.hdmi_dclk_pol = VOP_REG(RK3328_DSP_CTRL1, 0x1, 23),
	.edp_dclk_pol = VOP_REG(RK3328_DSP_CTRL1, 0x1, 27),
	.mipi_dclk_pol = VOP_REG(RK3328_DSP_CTRL1, 0x1, 31),

	.dither_down = VOP_REG(RK3328_DSP_CTRL1, 0xf, 1),
	.dither_up = VOP_REG(RK3328_DSP_CTRL1, 0x1, 6),

	.dsp_data_swap = VOP_REG(RK3328_DSP_CTRL0, 0x1f, 12),
	.dsp_ccir656_avg = VOP_REG(RK3328_DSP_CTRL0, 0x1, 20),
	.dsp_blank = VOP_REG(RK3328_DSP_CTRL0, 0x3, 18),
	.dsp_lut_en = VOP_REG(RK3328_DSP_CTRL1, 0x1, 0),
	.out_mode = VOP_REG(RK3328_DSP_CTRL0, 0xf, 0),

	.xmirror = VOP_REG(RK3328_DSP_CTRL0, 0x1, 22),
	.ymirror = VOP_REG(RK3328_DSP_CTRL0, 0x1, 23),

	.dsp_background = VOP_REG(RK3328_DSP_BG, 0xffffffff, 0),

	.alpha_hard_calc = VOP_REG(RK3328_SYS_CTRL1, 0x1, 27),
	.level2_overlay_en = VOP_REG(RK3328_SYS_CTRL1, 0x1, 28),

	.hdr2sdr_en = VOP_REG(RK3328_HDR2DR_CTRL, 0x1, 0),
	.hdr2sdr_en_win0_csc = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 9),
	.hdr2sdr_src_min = VOP_REG(RK3328_HDR2DR_SRC_RANGE, 0x3fff, 0),
	.hdr2sdr_src_max = VOP_REG(RK3328_HDR2DR_SRC_RANGE, 0x3fff, 16),
	.hdr2sdr_normfaceetf = VOP_REG(RK3328_HDR2DR_NORMFACEETF, 0x7ff, 0),
	.hdr2sdr_dst_min = VOP_REG(RK3328_HDR2DR_DST_RANGE, 0x3fff, 0),
	.hdr2sdr_dst_max = VOP_REG(RK3328_HDR2DR_DST_RANGE, 0x3fff, 16),
	.hdr2sdr_normfacgamma = VOP_REG(RK3328_HDR2DR_NORMFACGAMMA, 0xffff, 0),

	.bt1886eotf_pre_conv_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 0),
	.rgb2rgb_pre_conv_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 1),
	.rgb2rgb_pre_conv_mode = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 2),
	.st2084oetf_pre_conv_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 3),
	.bt1886eotf_post_conv_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 4),
	.rgb2rgb_post_conv_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 5),
	.rgb2rgb_post_conv_mode = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 6),
	.st2084oetf_post_conv_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 7),
	.win_csc_mode_sel = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 31),

	.bcsh_brightness = VOP_REG(RK3328_BCSH_BCS, 0xff, 0),
	.bcsh_contrast = VOP_REG(RK3328_BCSH_BCS, 0x1ff, 8),
	.bcsh_sat_con = VOP_REG(RK3328_BCSH_BCS, 0x3ff, 20),
	.bcsh_out_mode = VOP_REG(RK3328_BCSH_BCS, 0x3, 30),
	.bcsh_sin_hue = VOP_REG(RK3328_BCSH_H, 0x1ff, 0),
	.bcsh_cos_hue = VOP_REG(RK3328_BCSH_H, 0x1ff, 16),
	.bcsh_r2y_csc_mode = VOP_REG(RK3328_BCSH_CTRL, 0x3, 6),
	.bcsh_r2y_en = VOP_REG(RK3328_BCSH_CTRL, 0x1, 4),
	.bcsh_y2r_csc_mode = VOP_REG(RK3328_BCSH_CTRL, 0x3, 2),
	.bcsh_y2r_en = VOP_REG(RK3328_BCSH_CTRL, 0x1, 0),
	.bcsh_color_bar = VOP_REG(RK3328_BCSH_COLOR_BAR, 0xffffff, 8),
	.bcsh_en = VOP_REG(RK3328_BCSH_COLOR_BAR, 0x1, 0),

	.cfg_done = VOP_REG(RK3328_REG_CFG_DONE, 0x1, 0),
};

static const struct vop_intr rk3328_vop_intr = {
	.intrs = rk3368_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3368_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3328_LINE_FLAG, 0xffff, 0),
	.line_flag_num[1] = VOP_REG(RK3328_LINE_FLAG, 0xffff, 16),
	.status = VOP_REG_MASK(RK3328_INTR_STATUS0, 0xffff, 0),
	.enable = VOP_REG_MASK(RK3328_INTR_EN0, 0xffff, 0),
	.clear = VOP_REG_MASK(RK3328_INTR_CLEAR0, 0xffff, 0),
};

static const struct vop_csc rk3328_win0_csc = {
	.r2y_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 8),
	.r2r_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 5),
	.y2r_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 9),
};

static const struct vop_csc rk3328_win1_csc = {
	.r2y_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 10),
	.r2r_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 1),
	.y2r_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 11),
};

static const struct vop_csc rk3328_win2_csc = {
	.r2y_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 12),
	.r2r_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 1),
	.y2r_en = VOP_REG(RK3328_SDR2HDR_CTRL, 0x1, 13),
};

static const struct vop_win_data rk3328_vop_win_data[] = {
	{ .base = 0xd0, .phy = &rk3288_win01_data,  .csc = &rk3328_win0_csc,
	  .type = DRM_PLANE_TYPE_PRIMARY,
	  .feature = WIN_FEATURE_HDR2SDR | WIN_FEATURE_SDR2HDR },
	{ .base = 0x1d0, .phy = &rk3288_win01_data, .csc = &rk3328_win1_csc,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .feature = WIN_FEATURE_SDR2HDR | WIN_FEATURE_PRE_OVERLAY },
	{ .base = 0x2d0, .phy = &rk3288_win01_data, .csc = &rk3328_win2_csc,
	  .type = DRM_PLANE_TYPE_CURSOR,
	  .feature = WIN_FEATURE_SDR2HDR | WIN_FEATURE_PRE_OVERLAY },
};

static const struct vop_data rk3328_vop = {
	.version = VOP_VERSION(3, 8),
	.feature = VOP_FEATURE_OUTPUT_10BIT,
	.hdr_table = &rk3328_hdr_table,
	.max_input = {4096, 8192},
	.max_output = {4096, 2160},
	.intr = &rk3328_vop_intr,
	.ctrl = &rk3328_ctrl_data,
	.win = rk3328_vop_win_data,
	.win_size = ARRAY_SIZE(rk3328_vop_win_data),
};

static const struct vop_scl_regs rk3036_win0_scl = {
	.scale_yrgb_x = VOP_REG(RK3036_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3036_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3036_WIN0_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3036_WIN0_SCL_FACTOR_CBR, 0xffff, 16),
};

static const struct vop_scl_regs rk3036_win1_scl = {
	.scale_yrgb_x = VOP_REG(RK3036_WIN1_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3036_WIN1_SCL_FACTOR_YRGB, 0xffff, 16),
};

static const struct vop_win_phy rk3036_win0_data = {
	.scl = &rk3036_win0_scl,
	.data_formats = formats_win_full,
	.nformats = ARRAY_SIZE(formats_win_full),
	.enable = VOP_REG(RK3036_SYS_CTRL, 0x1, 0),
	.format = VOP_REG(RK3036_SYS_CTRL, 0x7, 3),
	.rb_swap = VOP_REG(RK3036_SYS_CTRL, 0x1, 15),
	.act_info = VOP_REG(RK3036_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3036_WIN0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3036_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3036_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3036_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3036_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3036_WIN0_VIR, 0x1fff, 16),
	.alpha_mode = VOP_REG(RK3036_DSP_CTRL0, 0x1, 18),
	.alpha_en = VOP_REG(RK3036_ALPHA_CTRL, 0x1, 0)
};

static const struct vop_win_phy rk3036_win1_data = {
	.scl = &rk3036_win1_scl,
	.data_formats = formats_win_lite,
	.nformats = ARRAY_SIZE(formats_win_lite),
	.enable = VOP_REG(RK3036_SYS_CTRL, 0x1, 1),
	.format = VOP_REG(RK3036_SYS_CTRL, 0x7, 6),
	.rb_swap = VOP_REG(RK3036_SYS_CTRL, 0x1, 19),
	.act_info = VOP_REG(RK3036_WIN1_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3036_WIN1_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3036_WIN1_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3036_WIN1_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3036_WIN1_VIR, 0xffff, 0),
	.alpha_mode = VOP_REG(RK3036_DSP_CTRL0, 0x1, 19),
	.alpha_en = VOP_REG(RK3036_ALPHA_CTRL, 0x1, 1)
};

static const struct vop_win_data rk3036_vop_win_data[] = {
	{ .base = 0x00, .phy = &rk3036_win0_data,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .base = 0x00, .phy = &rk3036_win1_data,
	  .type = DRM_PLANE_TYPE_OVERLAY },
};

static const int rk3036_vop_intrs[] = {
	DSP_HOLD_VALID_INTR,
	FS_INTR,
	LINE_FLAG_INTR,
	BUS_ERROR_INTR,
};

static const struct vop_intr rk3036_intr = {
	.intrs = rk3036_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3036_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3036_INT_STATUS, 0xfff, 12),
	.status = VOP_REG(RK3036_INT_STATUS, 0xf, 0),
	.enable = VOP_REG(RK3036_INT_STATUS, 0xf, 4),
	.clear = VOP_REG(RK3036_INT_STATUS, 0xf, 8),
};

static const struct vop_ctrl rk3036_ctrl_data = {
	.standby = VOP_REG(RK3036_SYS_CTRL, 0x1, 30),
	.out_mode = VOP_REG(RK3036_DSP_CTRL0, 0xf, 0),
	.dsp_blank = VOP_REG(RK3036_DSP_CTRL1, 0x1, 24),
	.dclk_pol = VOP_REG(RK3036_DSP_CTRL0, 0x1, 7),
	.pin_pol = VOP_REG(RK3036_DSP_CTRL0, 0x7, 4),
	.dsp_layer_sel = VOP_REG(RK3036_DSP_CTRL0, 0x1, 8),
	.htotal_pw = VOP_REG(RK3036_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3036_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3036_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3036_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.cfg_done = VOP_REG(RK3036_REG_CFG_DONE, 0x1, 0),
};

static const struct vop_data rk3036_vop = {
	.version = VOP_VERSION(2, 2),
	.max_input = {1920, 1080},
	.max_output = {1920, 1080},
	.ctrl = &rk3036_ctrl_data,
	.intr = &rk3036_intr,
	.win = rk3036_vop_win_data,
	.win_size = ARRAY_SIZE(rk3036_vop_win_data),
};

static const struct of_device_id vop_driver_dt_match[] = {
	{ .compatible = "rockchip,rk3288-vop",
	  .data = &rk3288_vop },
	{ .compatible = "rockchip,rk3036-vop",
	  .data = &rk3036_vop },
	{ .compatible = "rockchip,rk3328-vop",
	  .data = &rk3328_vop },
	{},
};
MODULE_DEVICE_TABLE(of, vop_driver_dt_match);

static int vop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!dev->of_node) {
		dev_err(dev, "can't find vop devices\n");
		return -ENODEV;
	}

	return component_add(dev, &vop_component_ops);
}

static int vop_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vop_component_ops);

	return 0;
}

static struct platform_driver vop_platform_driver = {
	.probe = vop_probe,
	.remove = vop_remove,
	.driver = {
		.name = "rockchip-vop",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(vop_driver_dt_match),
	},
};

module_platform_driver(vop_platform_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP VOP Driver");
MODULE_LICENSE("GPL v2");
