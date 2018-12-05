/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TEGRA_DRM_JOB_H
#define __TEGRA_DRM_JOB_H

#include <linux/workqueue.h>

#include "drm.h"
/* include hw specification, host1x01 is common enough */
#include "host1x01_hardware.h"

struct tegra_drm_bo_fences {
	struct dma_fence *excl;
	struct dma_fence **shared;
	unsigned int num_shared;
};

#define TEGRA_JOB_BOS_BITMAP_SZ \
	DIV_ROUND_UP(DRM_TEGRA_BO_TABLE_MAX_ENTRIES_NUM, BITS_PER_LONG)

struct tegra_drm_job {
	unsigned long bos_write_bitmap[TEGRA_JOB_BOS_BITMAP_SZ];
	unsigned long bos_gart_bitmap[TEGRA_JOB_BOS_BITMAP_SZ];
	struct drm_sched_job sched_job;
	struct host1x *host;
	struct host1x_job base;
	struct tegra_drm *tegra;
	struct tegra_drm_channel *drm_channel;
	struct dma_fence *in_fence;
	struct dma_fence *hw_fence;
	struct drm_syncobj *out_syncobj;
	struct tegra_drm_bo_fences *bo_fences;
	struct tegra_bo **bos;
	unsigned int num_bos;
	struct kref refcount;
	bool prepared : 1;
	u64 pipes;

	atomic_t *num_active_jobs;
	struct work_struct free_work;
	void (*free)(struct tegra_drm_job *job);
};

static inline void
tegra_drm_work_free_job(struct work_struct *work)
{
	struct tegra_drm_job *job = container_of(work, struct tegra_drm_job,
						 free_work);
	atomic_t *num_active_jobs = job->num_active_jobs;

	job->free(job);

	atomic_dec(num_active_jobs);
}

static inline void
tegra_drm_init_job(struct tegra_drm_job *job,
		   struct tegra_drm *tegra,
		   struct drm_syncobj *out_syncobj,
		   struct dma_fence *in_fence,
		   struct host1x_syncpt *syncpt,
		   u64 fence_context,
		   atomic_t *num_active_jobs,
		   void (*free_job)(struct tegra_drm_job *job))
{
	unsigned int i;

	for (i = 0; i < TEGRA_JOB_BOS_BITMAP_SZ; i++) {
		job->bos_write_bitmap[i] = 0;
		job->bos_gart_bitmap[i] = 0;
	}

	job->host		= syncpt->host;
	job->tegra		= tegra;
	job->num_bos		= 0;
	job->free		= free_job;
	job->bo_fences		= NULL;
	job->out_syncobj	= out_syncobj;
	job->in_fence		= in_fence;
	job->hw_fence		= NULL;
	job->num_active_jobs	= num_active_jobs;
	job->prepared		= false;

	INIT_WORK(&job->free_work, tegra_drm_work_free_job);
	host1x_init_job(&job->base, syncpt, fence_context);
	memset(&job->sched_job, 0, sizeof(job->sched_job));
	atomic_inc(num_active_jobs);
	kref_init(&job->refcount);
}

static inline void
tegra_drm_free_job(struct tegra_drm_job *job)
{
	if (irqs_disabled()) {
		host1x_finish_job(&job->base);

		/*
		 * kernel/dma/mapping.c complains about freeing memory with
		 * IRQ's being disabled, telling that this is risky and leads
		 * to a hang. Hence we won't do it.
		 */
		schedule_work(&job->free_work);
	} else {
		tegra_drm_work_free_job(&job->free_work);
	}
}

static inline struct tegra_drm_job *
tegra_drm_job_get(struct tegra_drm_job *job)
{
	kref_get(&job->refcount);

	return job;
}

static inline void
tegra_drm_job_release(struct kref *kref)
{
	struct tegra_drm_job *job = container_of(kref, struct tegra_drm_job,
						 refcount);
	tegra_drm_free_job(job);
}

static inline void
tegra_drm_job_put(struct tegra_drm_job *job)
{
	kref_put(&job->refcount, tegra_drm_job_release);
}

int tegra_drm_copy_and_patch_cmdstream(const struct tegra_drm *tegra,
				       struct tegra_drm_job *drm_job,
				       struct tegra_bo *const *bos,
				       u64 pipes_expected,
				       u32 *words_in,
				       u64 *ret_pipes,
				       unsigned int *ret_incrs);

int tegra_drm_submit_job_v1(struct drm_device *drm,
			    struct drm_tegra_submit *submit,
			    struct drm_file *file);

int tegra_drm_submit_job_v2(struct drm_device *drm,
			    struct drm_tegra_submit_v2 *submit,
			    struct drm_file *file);

#endif
