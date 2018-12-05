/*
 * Copyright (c) 2015, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/host1x.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <soc/tegra/pmc.h>

#include "drm.h"
#include "falcon.h"
#include "vic.h"

struct vic_config {
	const char *firmware;
	unsigned int version;
	bool supports_sid;
};

struct vic {
	struct falcon falcon;
	bool booted;

	void __iomem *regs;
	struct iommu_group *group;
	struct tegra_drm_client client;
	struct tegra_drm_channel *channel;
	struct device *dev;
	struct clk *clk;
	struct reset_control *rst;
	struct mutex lock;

	/* Platform configuration */
	const struct vic_config *config;
};

static inline struct vic *to_vic(struct tegra_drm_client *client)
{
	return container_of(client, struct vic, client);
}

static void vic_writel(struct vic *vic, u32 value, unsigned int offset)
{
	writel(value, vic->regs + offset);
}

static int vic_runtime_resume(struct device *dev)
{
	struct vic *vic = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(vic->clk);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	err = reset_control_deassert(vic->rst);
	if (err < 0)
		goto disable;

	usleep_range(10, 20);

	return 0;

disable:
	clk_disable_unprepare(vic->clk);
	return err;
}

static int vic_runtime_suspend(struct device *dev)
{
	struct vic *vic = dev_get_drvdata(dev);
	int err;

	err = reset_control_assert(vic->rst);
	if (err < 0)
		return err;

	usleep_range(2000, 4000);

	clk_disable_unprepare(vic->clk);

	vic->booted = false;

	return 0;
}

static int vic_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm_client = to_tegra_drm_client(client);
	struct drm_device *drm = dev_get_drvdata(client->parent);
	struct tegra_drm *tegra_drm = drm->dev_private;
	struct vic *vic = to_vic(drm_client);
	int err;

	vic->group = tegra_drm_client_iommu_attach(drm_client, false);
	if (IS_ERR(vic->group)) {
		err = PTR_ERR(vic->group);
		dev_err(vic->dev, "failed to attach to domain: %d\n", err);
		return err;
	}

	err = tegra_drm_register_client(tegra_drm, drm_client);
	if (err) {
		dev_err(vic->dev, "failed to register client: %d\n", err);
		goto detach_iommu;
	}

	vic->channel = tegra_drm_open_channel(tegra_drm, drm_client,
					      TEGRA_DRM_PIPE_VIC,
					      32, 1, 0, 600, "vic channel");
	if (IS_ERR(vic->channel)) {
		err = PTR_ERR(vic->channel);
		dev_err(vic->dev, "failed to open channel: %d\n", err);
		goto unreg_client;
	}

	return 0;

unreg_client:
	tegra_drm_unregister_client(drm_client);

detach_iommu:
	tegra_drm_client_iommu_detach(drm_client, vic->group);

	return err;
}

static int vic_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm_client = to_tegra_drm_client(client);
	struct vic *vic = to_vic(drm_client);

	tegra_drm_close_channel(vic->channel);
	tegra_drm_unregister_client(drm_client);
	tegra_drm_client_iommu_detach(drm_client, vic->group);

	return 0;
}

static const struct host1x_client_ops vic_host1x_client_ops = {
	.init = vic_init,
	.exit = vic_exit,
};

static int
vic_refine_class(struct tegra_drm_client *client, u64 pipes,
		 unsigned int *classid)
{
	enum drm_tegra_cmdstream_class drm_class = *classid;

	if (pipes != TEGRA_DRM_PIPE_VIC)
		return -EINVAL;

	switch (drm_class) {
	case DRM_TEGRA_CMDSTREAM_CLASS_VIC:
		*classid = HOST1X_CLASS_VIC;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int vic_load_firmware(struct vic *vic)
{
	int err;

	if (vic->falcon.data)
		return 0;

	vic->falcon.data = vic->client.drm;

	err = falcon_read_firmware(&vic->falcon, vic->config->firmware);
	if (err < 0)
		goto cleanup;

	err = falcon_load_firmware(&vic->falcon);
	if (err < 0)
		goto cleanup;

	return 0;

cleanup:
	vic->falcon.data = NULL;
	return err;
}

static int vic_boot(struct vic *vic)
{
	u32 fce_ucode_size, fce_bin_data_offset;
	void *hdr;
	int err = 0;

	if (vic->booted)
		return 0;

	if (vic->config->supports_sid) {
		struct iommu_fwspec *spec = dev_iommu_fwspec_get(vic->dev);
		u32 value;

		value = TRANSCFG_ATT(1, TRANSCFG_SID_FALCON) |
			TRANSCFG_ATT(0, TRANSCFG_SID_HW);
		vic_writel(vic, value, VIC_TFBIF_TRANSCFG);

		if (spec && spec->num_ids > 0) {
			value = spec->ids[0] & 0xffff;

			vic_writel(vic, value, VIC_THI_STREAMID0);
			vic_writel(vic, value, VIC_THI_STREAMID1);
		}
	}

	/* setup clockgating registers */
	vic_writel(vic, CG_IDLE_CG_DLY_CNT(4) |
			CG_IDLE_CG_EN |
			CG_WAKEUP_DLY_CNT(4),
		   NV_PVIC_MISC_PRI_VIC_CG);

	err = falcon_boot(&vic->falcon);
	if (err < 0)
		return err;

	hdr = vic->falcon.firmware.vaddr;
	fce_bin_data_offset = *(u32 *)(hdr + VIC_UCODE_FCE_DATA_OFFSET);
	hdr = vic->falcon.firmware.vaddr +
		*(u32 *)(hdr + VIC_UCODE_FCE_HEADER_OFFSET);
	fce_ucode_size = *(u32 *)(hdr + FCE_UCODE_SIZE_OFFSET);

	falcon_execute_method(&vic->falcon, VIC_SET_APPLICATION_ID, 1);
	falcon_execute_method(&vic->falcon, VIC_SET_FCE_UCODE_SIZE,
			      fce_ucode_size);
	falcon_execute_method(&vic->falcon, VIC_SET_FCE_UCODE_OFFSET,
			      (vic->falcon.firmware.paddr + fce_bin_data_offset)
				>> 8);

	err = falcon_wait_idle(&vic->falcon);
	if (err < 0) {
		dev_err(vic->dev,
			"failed to set application ID and FCE base\n");
		return err;
	}

	vic->booted = true;

	return 0;
}

static int vic_prepare_job(struct tegra_drm_client *drm_client,
			   struct tegra_drm_job *job)
{
	struct vic *vic = to_vic(drm_client);
	int err;

	err = pm_runtime_get_sync(vic->dev);
	if (err)
		return err;

	if (!vic->booted) {
		mutex_lock(&vic->lock);

		err = vic_load_firmware(vic);
		if (err < 0)
			goto unlock;

		err = vic_boot(vic);
		if (err)
			goto unlock;

		mutex_unlock(&vic->lock);
	}

	return 0;

unlock:
	mutex_unlock(&vic->lock);
	pm_runtime_put(vic->dev);
	return err;
}

static int vic_unprepare_job(struct tegra_drm_client *drm_client,
			     struct tegra_drm_job *job)
{
	struct vic *vic = to_vic(drm_client);
	int err;

	err = pm_runtime_put(vic->dev);
	if (err)
		return err;

	return 0;
}

static void *vic_falcon_alloc(struct falcon *falcon, size_t size,
			      dma_addr_t *iova)
{
	struct tegra_drm *tegra = falcon->data;

	return tegra_drm_alloc(tegra, size, iova);
}

static void vic_falcon_free(struct falcon *falcon, size_t size,
			    dma_addr_t iova, void *va)
{
	struct tegra_drm *tegra = falcon->data;

	return tegra_drm_free(tegra, size, va, iova);
}

static const struct falcon_ops vic_falcon_ops = {
	.alloc = vic_falcon_alloc,
	.free = vic_falcon_free
};

#define NVIDIA_TEGRA_124_VIC_FIRMWARE "nvidia/tegra124/vic03_ucode.bin"

static const struct vic_config vic_t124_config = {
	.firmware = NVIDIA_TEGRA_124_VIC_FIRMWARE,
	.version = 0x40,
	.supports_sid = false,
};

#define NVIDIA_TEGRA_210_VIC_FIRMWARE "nvidia/tegra210/vic04_ucode.bin"

static const struct vic_config vic_t210_config = {
	.firmware = NVIDIA_TEGRA_210_VIC_FIRMWARE,
	.version = 0x21,
	.supports_sid = false,
};

#define NVIDIA_TEGRA_186_VIC_FIRMWARE "nvidia/tegra186/vic04_ucode.bin"

static const struct vic_config vic_t186_config = {
	.firmware = NVIDIA_TEGRA_186_VIC_FIRMWARE,
	.version = 0x18,
	.supports_sid = true,
};

#define NVIDIA_TEGRA_194_VIC_FIRMWARE "nvidia/tegra194/vic.bin"

static const struct vic_config vic_t194_config = {
	.firmware = NVIDIA_TEGRA_194_VIC_FIRMWARE,
	.version = 0x19,
	.supports_sid = true,
};

static const struct of_device_id vic_match[] = {
	{ .compatible = "nvidia,tegra124-vic", .data = &vic_t124_config },
	{ .compatible = "nvidia,tegra210-vic", .data = &vic_t210_config },
	{ .compatible = "nvidia,tegra186-vic", .data = &vic_t186_config },
	{ .compatible = "nvidia,tegra194-vic", .data = &vic_t194_config },
	{ },
};

static int vic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *regs;
	struct vic *vic;
	int err;

	vic = devm_kzalloc(dev, sizeof(*vic), GFP_KERNEL);
	if (!vic)
		return -ENOMEM;

	vic->config = of_device_get_match_data(dev);

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "failed to get registers\n");
		return -ENXIO;
	}

	vic->regs = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vic->regs))
		return PTR_ERR(vic->regs);

	vic->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(vic->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(vic->clk);
	}

	if (!dev->pm_domain) {
		vic->rst = devm_reset_control_get(dev, "vic");
		if (IS_ERR(vic->rst)) {
			dev_err(&pdev->dev, "failed to get reset\n");
			return PTR_ERR(vic->rst);
		}
	}

	vic->falcon.dev = dev;
	vic->falcon.regs = vic->regs;
	vic->falcon.ops = &vic_falcon_ops;

	mutex_init(&vic->lock);

	err = falcon_init(&vic->falcon);
	if (err < 0)
		return err;

	platform_set_drvdata(pdev, vic);

	INIT_LIST_HEAD(&vic->client.list);
	vic->client.base.dev = dev;
	vic->client.base.ops = &vic_host1x_client_ops;
	vic->client.base.class = HOST1X_CLASS_VIC;
	vic->dev = dev;

	vic->client.prepare_job = vic_prepare_job;
	vic->client.unprepare_job = vic_unprepare_job;
	vic->client.refine_class = vic_refine_class;
	vic->client.pipe = TEGRA_DRM_PIPE_VIC;

	err = host1x_client_register(&vic->client.base);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		goto exit_falcon;
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		err = vic_runtime_resume(&pdev->dev);
		if (err < 0)
			goto unregister_client;
	}

	return 0;

unregister_client:
	host1x_client_unregister(&vic->client.base);
exit_falcon:
	falcon_exit(&vic->falcon);

	return err;
}

static int vic_remove(struct platform_device *pdev)
{
	struct vic *vic = platform_get_drvdata(pdev);
	int err;

	err = host1x_client_unregister(&vic->client.base);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	if (pm_runtime_enabled(&pdev->dev))
		pm_runtime_disable(&pdev->dev);
	else
		vic_runtime_suspend(&pdev->dev);

	falcon_exit(&vic->falcon);

	return 0;
}

static const struct dev_pm_ops vic_pm_ops = {
	SET_RUNTIME_PM_OPS(vic_runtime_suspend, vic_runtime_resume, NULL)
};

struct platform_driver tegra_vic_driver = {
	.driver = {
		.name = "tegra-vic",
		.of_match_table = vic_match,
		.pm = &vic_pm_ops
	},
	.probe = vic_probe,
	.remove = vic_remove,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_124_VIC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_210_VIC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_186_VIC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_194_VIC_FIRMWARE);
#endif
