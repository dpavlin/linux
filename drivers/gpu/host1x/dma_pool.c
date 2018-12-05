/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/sizes.h>
#include <linux/slab.h>

#include "host1x.h"

static inline size_t host1x_dma_pool_chunk_size(struct host1x *host)
{
	/* limit maximum size to a sensible number */
	return SZ_256K;
}

static int host1x_dma_pool_add_memory(struct host1x *host)
{
	struct host1x_pool_entry *entry;
	struct host1x_alloc_desc desc;
	size_t total;
	int err;

	desc.size	= host1x_dma_pool_chunk_size(host);
	desc.dma_attrs	= DMA_ATTR_WRITE_COMBINE;

	/* don't allow pool to grow boundlessly */
	total = gen_pool_size(host->pool);

	/* limit overall pools size basing on the number of channels */
	if (total >= SZ_64K * host->soc->nb_channels)
		return -ENOMEM;

	/*
	 * gen_pool's chunk entry doesn't carry enough information about
	 * allocation if we're using get_pages(), hence roll out our own
	 * descriptor.
	 */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	err = host1x_alloc_memory(host, &desc);
	if (err)
		goto err_free_entry;

	err = gen_pool_add_virt(host->pool, (unsigned long) desc.vaddr,
				desc.dmaaddr, desc.size, -1);
	if (err)
		goto err_free_memory;

	entry->dmaaddr		= desc.dmaaddr;
	entry->addr		= desc.addr;
	entry->vaddr		= desc.vaddr;
	entry->size		= desc.size;
	entry->dma_attrs	= desc.dma_attrs;

	/* add descriptor to the list to track this allocation */
	spin_lock(&host->pool->lock);
	list_add(&entry->list, &host->pool_chunks);
	spin_unlock(&host->pool->lock);

	return 0;

err_free_memory:
	host1x_free_memory(host, &desc);
err_free_entry:
	kfree(entry);

	return err;
}

int host1x_init_dma_pool(struct host1x *host)
{
	/*
	 * Create HOST1x buffer objects (cmdbufs, gathers) pool.
	 * Note that channel DMA has 16-bytes alignment requirement.
	 */
	host->pool = devm_gen_pool_create(host->dev, 4, -1, "cdma");
	if (!host->pool)
		return -ENOMEM;

	INIT_LIST_HEAD(&host->pool_chunks);

	return 0;
}

static void host1x_dma_pool_release_chunk(struct host1x *host,
					  struct host1x_pool_entry *entry)
{
	struct host1x_alloc_desc desc;

	desc.dmaaddr	= entry->dmaaddr;
	desc.addr	= entry->addr;
	desc.vaddr	= entry->vaddr;
	desc.size	= entry->size;
	desc.dma_attrs	= entry->dma_attrs;

	host1x_free_memory(host, &desc);
	list_del(&entry->list);
	kfree(entry);
}

void host1x_deinit_dma_pool(struct host1x *host)
{
	size_t available = gen_pool_avail(host->pool);
	size_t total = gen_pool_size(host->pool);
	struct host1x_pool_entry *entry, *tmp;

	/* shouldn't happen, all allocations must be freed at this point */
	WARN_ON(available != total);

	/* get back memory held by the pool */
	list_for_each_entry_safe(entry, tmp, &host->pool_chunks, list)
		host1x_dma_pool_release_chunk(host, entry);
}

int host1x_dma_pool_grow(struct host1x *host, size_t size)
{
	int err;

	/* allocation must fit into the pool's chunk */
	if (size > host1x_dma_pool_chunk_size(host))
		return -EINVAL;

	/* add more memory to the pool if allocation fails */
	err = host1x_dma_pool_add_memory(host);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL(host1x_dma_pool_grow);
