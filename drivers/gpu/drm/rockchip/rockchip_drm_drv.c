/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.c
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
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_sync_helper.h>
#include <drm/rockchip_drm.h>
#include <linux/devfreq.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <linux/dma-mapping.h>
#include <linux/dma-iommu.h>
#include <linux/genalloc.h>
#include <linux/pm_runtime.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/component.h>
#include <linux/dma-fence.h>
#include <linux/console.h>
#include <linux/iommu.h>
#include <linux/of_reserved_mem.h>

#include <drm/rockchip_drm.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_gem.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"RockChip Soc DRM"
#define DRIVER_DATE	"20140818"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static bool is_support_iommu = true;

static LIST_HEAD(rockchip_drm_subdrv_list);
static DEFINE_MUTEX(subdrv_list_mutex);
static struct drm_driver rockchip_drm_driver;

struct rockchip_drm_mode_set {
	struct list_head head;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_display_mode *mode;
	int hdisplay;
	int vdisplay;
	int vrefresh;
	int flags;
	int crtc_hsync_end;
	int crtc_vsync_end;

	int left_margin;
	int right_margin;
	int top_margin;
	int bottom_margin;

	bool mode_changed;
	bool ymirror;
	int ratio;
};

#ifndef MODULE
static const char *const loader_protect_clocks[] __initconst = {
	"hclk_vio",
	"aclk_vio",
	"aclk_vio0",
};

static struct clk **loader_clocks __initdata;
static int __init rockchip_clocks_loader_protect(void)
{
	int nclocks = ARRAY_SIZE(loader_protect_clocks);
	struct clk *clk;
	int i;

	loader_clocks = kcalloc(nclocks, sizeof(void *), GFP_KERNEL);
	if (!loader_clocks)
		return -ENOMEM;

	for (i = 0; i < nclocks; i++) {
		clk = __clk_lookup(loader_protect_clocks[i]);

		if (clk) {
			loader_clocks[i] = clk;
			clk_prepare_enable(clk);
		}
	}

	return 0;
}
fs_initcall(rockchip_clocks_loader_protect);

static int __init rockchip_clocks_loader_unprotect(void)
{
	int i;

	if (!loader_clocks)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(loader_protect_clocks); i++) {
		struct clk *clk = loader_clocks[i];

		if (clk)
			clk_disable_unprepare(clk);
	}
	kfree(loader_clocks);

	return 0;
}
late_initcall_sync(rockchip_clocks_loader_unprotect);
#endif

/*
 * Attach a (component) device to the shared drm dma mapping from master drm
 * device.  This is used by the VOPs to map GEM buffers to a common DMA
 * mapping.
 */
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	int ret;

	if (!is_support_iommu)
		return 0;

	ret = iommu_attach_device(private->domain, dev);
	if (ret) {
		dev_err(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return 0;
}

void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain *domain = private->domain;

	if (!is_support_iommu)
		return;

	iommu_detach_device(domain, dev);
}

int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe >= ROCKCHIP_MAX_CRTC)
		return -EINVAL;

	priv->crtc_funcs[pipe] = crtc_funcs;

	return 0;
}

void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe >= ROCKCHIP_MAX_CRTC)
		return;

	priv->crtc_funcs[pipe] = NULL;
}

static struct drm_crtc *rockchip_crtc_from_pipe(struct drm_device *drm,
						int pipe)
{
	struct drm_crtc *crtc;
	int i = 0;

	list_for_each_entry(crtc, &drm->mode_config.crtc_list, head)
		if (i++ == pipe)
			return crtc;

	return NULL;
}

static int rockchip_drm_crtc_enable_vblank(struct drm_device *dev,
					   unsigned int pipe)
{
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = rockchip_crtc_from_pipe(dev, pipe);

	if (crtc && priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->enable_vblank)
		return priv->crtc_funcs[pipe]->enable_vblank(crtc);

	return 0;
}

static void rockchip_drm_crtc_disable_vblank(struct drm_device *dev,
					     unsigned int pipe)
{
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = rockchip_crtc_from_pipe(dev, pipe);

	if (crtc && priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->enable_vblank)
		priv->crtc_funcs[pipe]->disable_vblank(crtc);
}

static int rockchip_drm_fault_handler(struct iommu_domain *iommu,
				      struct device *dev,
				      unsigned long iova, int flags, void *arg)
{
	struct drm_device *drm_dev = arg;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->regs_dump)
			priv->crtc_funcs[pipe]->regs_dump(crtc, NULL);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_dump)
			priv->crtc_funcs[pipe]->debugfs_dump(crtc, NULL);
	}

	return 0;
}

static int rockchip_drm_init_iommu(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain_geometry *geometry;
	u64 start, end;

	if (!is_support_iommu)
		return 0;

	private->domain = iommu_domain_alloc(&platform_bus_type);
	if (!private->domain)
		return -ENOMEM;

	geometry = &private->domain->geometry;
	start = geometry->aperture_start;
	end = geometry->aperture_end;

	DRM_DEBUG("IOMMU context initialized (aperture: %#llx-%#llx)\n",
		  start, end);
	drm_mm_init(&private->mm, start, end - start + 1);
	mutex_init(&private->mm_lock);

	iommu_set_fault_handler(private->domain, rockchip_drm_fault_handler,
				drm_dev);

	return 0;
}

static void rockchip_iommu_cleanup(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;

	if (!is_support_iommu)
		return;

	drm_mm_takedown(&private->mm);
	iommu_domain_free(private->domain);
}

#ifdef CONFIG_DEBUG_FS
static int rockchip_drm_mm_dump(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	int ret;

	if (!priv->domain)
		return 0;

	mutex_lock(&priv->mm_lock);

	ret = drm_mm_dump_table(s, &priv->mm);

	mutex_unlock(&priv->mm_lock);

	return ret;
}

static int rockchip_drm_summary_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_dump)
			priv->crtc_funcs[pipe]->debugfs_dump(crtc, s);
	}

	return 0;
}

static struct drm_info_list rockchip_debugfs_files[] = {
	{ "summary", rockchip_drm_summary_show, 0, NULL },
	{ "mm_dump", rockchip_drm_mm_dump, 0, NULL },
};

static int rockchip_drm_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	int ret;

	ret = drm_debugfs_create_files(rockchip_debugfs_files,
				       ARRAY_SIZE(rockchip_debugfs_files),
				       minor->debugfs_root,
				       minor);
	if (ret) {
		dev_err(dev->dev, "could not install rockchip_debugfs_list\n");
		return ret;
	}

	drm_for_each_crtc(crtc, dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_init)
			priv->crtc_funcs[pipe]->debugfs_init(minor, crtc);
	}

	return 0;
}

static void rockchip_drm_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(rockchip_debugfs_files,
				 ARRAY_SIZE(rockchip_debugfs_files), minor);
}
#endif

static int rockchip_gem_pool_init(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;
	struct device_node *np = drm->dev->of_node;
	struct device_node *node;
	phys_addr_t start, size;
	struct resource res;
	int ret;

	node = of_parse_phandle(np, "secure-memory-region", 0);
	if (!node)
		return -ENXIO;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	start = res.start;
	size = resource_size(&res);
	if (!size)
		return -ENOMEM;

	private->secure_buffer_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!private->secure_buffer_pool)
		return -ENOMEM;

	gen_pool_add(private->secure_buffer_pool, start, size, -1);

	return 0;
}

static void rockchip_gem_pool_destroy(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;

	if (!private->secure_buffer_pool)
		return;

	gen_pool_destroy(private->secure_buffer_pool);
}

static void rockchip_attach_connector_property(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;

	mutex_lock(&drm->mode_config.mutex);

	drm_for_each_connector(connector, drm) {
#define ROCKCHIP_PROP_ATTACH(prop, v) \
	drm_object_attach_property(&connector->base, prop, v)

		ROCKCHIP_PROP_ATTACH(conf->tv_brightness_property, 100);
		ROCKCHIP_PROP_ATTACH(conf->tv_contrast_property, 100);
		ROCKCHIP_PROP_ATTACH(conf->tv_saturation_property, 100);
		ROCKCHIP_PROP_ATTACH(conf->tv_hue_property, 100);
	}

	mutex_unlock(&drm->mode_config.mutex);
}

static void rockchip_drm_set_property_default(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_atomic_state *state;
	int ret;

	drm_modeset_lock_all(drm);

	state = drm_atomic_helper_duplicate_state(drm, conf->acquire_ctx);
	if (!state) {
		DRM_ERROR("failed to alloc atomic state\n");
		ret = -ENOMEM;
		goto err_unlock;
	}
	state->acquire_ctx = conf->acquire_ctx;

	drm_for_each_connector(connector, drm) {
		struct drm_connector_state *connector_state;

		connector_state = drm_atomic_get_connector_state(state,
								 connector);
		if (IS_ERR(connector_state))
			DRM_ERROR("Connector[%d]: Failed to get state\n",
				  connector->base.id);

#define CONNECTOR_SET_PROP(prop, val) \
	do { \
		ret = drm_atomic_connector_set_property(connector, \
						connector_state, \
						prop, \
						val); \
		if (ret) \
			DRM_ERROR("Connector[%d]: Failed to initial %s\n", \
				  connector->base.id, #prop); \
	} while (0)

		CONNECTOR_SET_PROP(conf->tv_brightness_property, 50);
		CONNECTOR_SET_PROP(conf->tv_contrast_property, 50);
		CONNECTOR_SET_PROP(conf->tv_saturation_property, 50);
		CONNECTOR_SET_PROP(conf->tv_hue_property, 50);

#undef CONNECTOR_SET_PROP
	}

	ret = drm_atomic_commit(state);
	WARN_ON(ret == -EDEADLK);
	if (ret)
		DRM_ERROR("Failed to update propertys\n");

err_unlock:
	drm_modeset_unlock_all(drm);
}

static int rockchip_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct rockchip_drm_private *private;
	int ret;
	struct device_node *np = dev->of_node;
	struct device_node *parent_np;

	drm_dev = drm_dev_alloc(&rockchip_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	dev_set_drvdata(dev, drm_dev);

	private = devm_kzalloc(drm_dev->dev, sizeof(*private), GFP_KERNEL);
	if (!private) {
		ret = -ENOMEM;
		goto err_free;
	}

	mutex_init(&private->commit_lock);
	INIT_WORK(&private->commit_work, rockchip_drm_atomic_work);

	drm_dev->dev_private = private;

	INIT_LIST_HEAD(&private->psr_list);
	spin_lock_init(&private->psr_list_lock);

	private->dmc_support = false;
	private->devfreq = devfreq_get_devfreq_by_phandle(dev, 0);
	if (IS_ERR(private->devfreq)) {
		if (PTR_ERR(private->devfreq) == -EPROBE_DEFER) {
			parent_np = of_parse_phandle(np, "devfreq", 0);
			if (parent_np &&
			    of_device_is_available(parent_np)) {
				private->dmc_support = true;
				dev_warn(dev, "defer getting devfreq\n");
			} else {
				dev_info(dev, "dmc is disabled\n");
			}
		} else {
			dev_info(dev, "devfreq is not set\n");
		}
		private->devfreq = NULL;
	} else {
		private->dmc_support = true;
		dev_info(dev, "devfreq is ready\n");
	}

	private->hdmi_pll.pll = devm_clk_get(dev, "hdmi-tmds-pll");
	if (PTR_ERR(private->hdmi_pll.pll) == -ENOENT) {
		private->hdmi_pll.pll = NULL;
	} else if (PTR_ERR(private->hdmi_pll.pll) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_free;
	} else if (IS_ERR(private->hdmi_pll.pll)) {
		dev_err(dev, "failed to get hdmi-tmds-pll\n");
		ret = PTR_ERR(private->hdmi_pll.pll);
		goto err_free;
	}

	private->default_pll.pll = devm_clk_get(dev, "default-vop-pll");
	if (PTR_ERR(private->default_pll.pll) == -ENOENT) {
		private->default_pll.pll = NULL;
	} else if (PTR_ERR(private->default_pll.pll) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_free;
	} else if (IS_ERR(private->default_pll.pll)) {
		dev_err(dev, "failed to get default vop pll\n");
		ret = PTR_ERR(private->default_pll.pll);
		goto err_free;
	}

#ifdef CONFIG_DRM_DMA_SYNC
	private->cpu_fence_context = dma_fence_context_alloc(1);
	atomic_set(&private->cpu_fence_seqno, 0);
#endif

	ret = rockchip_drm_init_iommu(drm_dev);
	if (ret)
		goto err_free;

	drm_mode_config_init(drm_dev);

	rockchip_drm_mode_config_init(drm_dev);
	drm_mode_create_tv_properties(drm_dev, 0, NULL);

	/* Try to bind all sub drivers. */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_mode_config_cleanup;

	rockchip_attach_connector_property(drm_dev);

	ret = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (ret)
		goto err_unbind_all;

	drm_mode_config_reset(drm_dev);

	rockchip_drm_set_property_default(drm_dev);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 */
	drm_dev->irq_enabled = true;

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(drm_dev);

	rockchip_gem_pool_init(drm_dev);
	ret = of_reserved_mem_device_init(drm_dev->dev);
	if (ret)
		DRM_DEBUG_KMS("No reserved memory region assign to drm\n");

	ret = rockchip_drm_fbdev_init(drm_dev);
	if (ret)
		goto err_kms_helper_poll_fini;

	drm_dev->mode_config.allow_fb_modifiers = true;

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_fbdev_fini;

	return 0;
err_fbdev_fini:
	rockchip_drm_fbdev_fini(drm_dev);
err_kms_helper_poll_fini:
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);
	drm_vblank_cleanup(drm_dev);
err_unbind_all:
	component_unbind_all(dev, drm_dev);
err_mode_config_cleanup:
	drm_mode_config_cleanup(drm_dev);
	rockchip_iommu_cleanup(drm_dev);
err_free:
	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_unref(drm_dev);
	return ret;
}

static void rockchip_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_dev_unregister(drm_dev);

	rockchip_drm_fbdev_fini(drm_dev);
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);

	drm_vblank_cleanup(drm_dev);
	component_unbind_all(dev, drm_dev);
	drm_mode_config_cleanup(drm_dev);
	rockchip_iommu_cleanup(drm_dev);

	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_unref(drm_dev);
}

int rockchip_drm_register_subdrv(struct drm_rockchip_subdrv *subdrv)
{
	if (!subdrv)
		return -EINVAL;

	mutex_lock(&subdrv_list_mutex);
	list_add_tail(&subdrv->list, &rockchip_drm_subdrv_list);
	mutex_unlock(&subdrv_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_drm_register_subdrv);

int rockchip_drm_unregister_subdrv(struct drm_rockchip_subdrv *subdrv)
{
	if (!subdrv)
		return -EINVAL;

	mutex_lock(&subdrv_list_mutex);
	list_del(&subdrv->list);
	mutex_unlock(&subdrv_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_drm_unregister_subdrv);

static int rockchip_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct rockchip_drm_file_private *file_priv;
	struct drm_rockchip_subdrv *subdrv;
	int ret = 0;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;
	INIT_LIST_HEAD(&file_priv->gem_cpu_acquire_list);

	file->driver_priv = file_priv;

	mutex_lock(&subdrv_list_mutex);
	list_for_each_entry(subdrv, &rockchip_drm_subdrv_list, list) {
		ret = subdrv->open(dev, subdrv->dev, file);
		if (ret) {
			mutex_unlock(&subdrv_list_mutex);
			goto err_free_file_priv;
		}
	}
	mutex_unlock(&subdrv_list_mutex);

	return 0;

err_free_file_priv:
	kfree(file_priv);
	file_priv = NULL;

	return ret;
}

static void rockchip_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	kfree(file->driver_priv);
	file->driver_priv = NULL;
}

static void rockchip_drm_crtc_cancel_pending_vblank(struct drm_crtc *crtc,
						    struct drm_file *file_priv)
{
	struct rockchip_drm_private *priv = crtc->dev->dev_private;
	int pipe = drm_crtc_index(crtc);

	if (pipe < ROCKCHIP_MAX_CRTC &&
	    priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->cancel_pending_vblank)
		priv->crtc_funcs[pipe]->cancel_pending_vblank(crtc, file_priv);
}

static void rockchip_drm_preclose(struct drm_device *dev,
				  struct drm_file *file_priv)
{
	struct rockchip_drm_file_private *file_private = file_priv->driver_priv;
	struct rockchip_gem_object_node *cur, *d;
	struct drm_rockchip_subdrv *subdrv;
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		rockchip_drm_crtc_cancel_pending_vblank(crtc, file_priv);

	mutex_lock(&dev->struct_mutex);
	list_for_each_entry_safe(cur, d,
			&file_private->gem_cpu_acquire_list, list) {
#ifdef CONFIG_DRM_DMA_SYNC
		BUG_ON(!cur->rockchip_gem_obj->acquire_fence);
		drm_fence_signal_and_put(&cur->rockchip_gem_obj->acquire_fence);
#endif
		drm_gem_object_unreference(&cur->rockchip_gem_obj->base);
		kfree(cur);
	}
	/* since we are deleting the whole list, just initialize the header
	 * instead of calling list_del for every element
	 */
	INIT_LIST_HEAD(&file_private->gem_cpu_acquire_list);
	mutex_unlock(&dev->struct_mutex);

	mutex_lock(&subdrv_list_mutex);
	list_for_each_entry(subdrv, &rockchip_drm_subdrv_list, list)
		subdrv->close(dev, subdrv->dev, file_priv);
	mutex_unlock(&subdrv_list_mutex);
}

static const struct drm_ioctl_desc rockchip_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CREATE, rockchip_gem_create_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_MAP_OFFSET,
			  rockchip_gem_map_offset_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CPU_ACQUIRE,
			  rockchip_gem_cpu_acquire_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CPU_RELEASE,
			  rockchip_gem_cpu_release_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_GET_PHYS, rockchip_gem_get_phys_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
};

static const struct file_operations rockchip_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = rockchip_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.release = drm_release,
};

static struct drm_driver rockchip_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM |
				  DRIVER_PRIME | DRIVER_ATOMIC |
				  DRIVER_RENDER,
	.preclose		= rockchip_drm_preclose,
	.get_vblank_counter	= drm_vblank_no_hw_counter,
	.open			= rockchip_drm_open,
	.postclose		= rockchip_drm_postclose,
	.enable_vblank		= rockchip_drm_crtc_enable_vblank,
	.disable_vblank		= rockchip_drm_crtc_disable_vblank,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.gem_free_object_unlocked = rockchip_gem_free_object,
	.dumb_create		= rockchip_gem_dumb_create,
	.dumb_map_offset	= rockchip_gem_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= rockchip_gem_prime_get_sg_table,
	.gem_prime_import_sg_table	= rockchip_gem_prime_import_sg_table,
	.gem_prime_vmap		= rockchip_gem_prime_vmap,
	.gem_prime_vunmap	= rockchip_gem_prime_vunmap,
	.gem_prime_mmap		= rockchip_gem_mmap_buf,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= rockchip_drm_debugfs_init,
	.debugfs_cleanup	= rockchip_drm_debugfs_cleanup,
#endif
	.ioctls			= rockchip_ioctls,
	.num_ioctls		= ARRAY_SIZE(rockchip_ioctls),
	.fops			= &rockchip_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

#ifdef CONFIG_PM_SLEEP
static void rockchip_drm_fb_suspend(struct drm_device *drm)
{
	struct rockchip_drm_private *priv = drm->dev_private;

	console_lock();
	drm_fb_helper_set_suspend(priv->fbdev_helper, 1);
	console_unlock();
}

static void rockchip_drm_fb_resume(struct drm_device *drm)
{
	struct rockchip_drm_private *priv = drm->dev_private;

	console_lock();
	drm_fb_helper_set_suspend(priv->fbdev_helper, 0);
	console_unlock();
}

static int rockchip_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rockchip_drm_private *priv = drm->dev_private;

	drm_kms_helper_poll_disable(drm);
	rockchip_drm_fb_suspend(drm);

	priv->state = drm_atomic_helper_suspend(drm);
	if (IS_ERR(priv->state)) {
		rockchip_drm_fb_resume(drm);
		drm_kms_helper_poll_enable(drm);
		return PTR_ERR(priv->state);
	}

	return 0;
}

static int rockchip_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rockchip_drm_private *priv = drm->dev_private;

	drm_atomic_helper_resume(drm, priv->state);
	rockchip_drm_fb_resume(drm);
	drm_kms_helper_poll_enable(drm);

	return 0;
}
#endif

static const struct dev_pm_ops rockchip_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_drm_sys_suspend,
				rockchip_drm_sys_resume)
};

static int compare_of(struct device *dev, void *data)
{
	struct device_node *np = data;

	return dev->of_node == np;
}

static void rockchip_add_endpoints(struct device *dev,
				   struct component_match **match,
				   struct device_node *port)
{
	struct device_node *ep, *remote;

	for_each_child_of_node(port, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (!remote || !of_device_is_available(remote)) {
			of_node_put(remote);
			continue;
		} else if (!of_device_is_available(remote->parent)) {
			dev_warn(dev, "parent device of %s is not available\n",
				 remote->full_name);
			of_node_put(remote);
			continue;
		}

		drm_of_component_match_add(dev, match, compare_of, remote);
		of_node_put(remote);
	}
}

static const struct component_master_ops rockchip_drm_ops = {
	.bind = rockchip_drm_bind,
	.unbind = rockchip_drm_unbind,
};

static int rockchip_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	struct device_node *np = dev->of_node;
	struct device_node *port;
	int i;

	if (!np)
		return -ENODEV;
	/*
	 * Bind the crtc ports first, so that
	 * drm_of_find_possible_crtcs called from encoder .bind callbacks
	 * works as expected.
	 */
	for (i = 0;; i++) {
		struct device_node *iommu;

		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		iommu = of_parse_phandle(port->parent, "iommus", 0);
		if (!iommu || !of_device_is_available(iommu->parent)) {
			dev_dbg(dev, "no iommu attached for %s, using non-iommu buffers\n",
				port->parent->full_name);
			/*
			 * if there is a crtc not support iommu, force set all
			 * crtc use non-iommu buffer.
			 */
			is_support_iommu = false;
		}

		of_node_put(iommu);
		drm_of_component_match_add(dev, &match, compare_of,
					   port->parent);
		of_node_put(port);
	}

	if (i == 0) {
		dev_err(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!match) {
		dev_err(dev, "No available vop found for display-subsystem.\n");
		return -ENODEV;
	}
	/*
	 * For each bound crtc, bind the encoders attached to its
	 * remote endpoint.
	 */
	for (i = 0;; i++) {
		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		rockchip_add_endpoints(dev, &match, port);
		of_node_put(port);
	}

	port = of_parse_phandle(np, "backlight", 0);
	if (port && of_device_is_available(port)) {
		component_match_add(dev, &match, compare_of, port);
		of_node_put(port);
	}

	return component_master_add_with_match(dev, &rockchip_drm_ops, match);
}

static int rockchip_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &rockchip_drm_ops);

	return 0;
}

static void rockchip_drm_platform_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	rockchip_drm_sys_suspend(dev);
}

static const struct of_device_id rockchip_drm_dt_ids[] = {
	{ .compatible = "rockchip,display-subsystem", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_drm_dt_ids);

static struct platform_driver rockchip_drm_platform_driver = {
	.probe = rockchip_drm_platform_probe,
	.remove = rockchip_drm_platform_remove,
	.shutdown = rockchip_drm_platform_shutdown,
	.driver = {
		.name = "rockchip-drm",
		.of_match_table = rockchip_drm_dt_ids,
		.pm = &rockchip_drm_pm_ops,
	},
};

module_platform_driver(rockchip_drm_platform_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP DRM Driver");
MODULE_LICENSE("GPL v2");
