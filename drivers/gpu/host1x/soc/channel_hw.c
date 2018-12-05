/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/io.h>

#define CH_DMASTART(chid) \
	(host->base_regs + HOST1X_CHANNEL_DMASTART(chid))

#define CH_DMAPUT(chid) \
	(host->base_regs + HOST1X_CHANNEL_DMAPUT(chid))

#define CH_DMAGET(chid) \
	(host->base_regs + HOST1X_CHANNEL_DMAGET(chid))

#define CH_DMAEND(chid) \
	(host->base_regs + HOST1X_CHANNEL_DMAEND(chid))

#define CH_DMACTRL(chid) \
	(host->base_regs + HOST1X_CHANNEL_DMACTRL(chid))

#if HOST1X_HW < 6

#define CH_CMDPROC_STOP \
	(host->base_regs + HOST1X_SYNC_OFFSET + HOST1X_SYNC_CMDPROC_STOP)

#define CH_TEARDOWN \
	(host->base_regs + HOST1X_SYNC_OFFSET + HOST1X_SYNC_CH_TEARDOWN)

#define CH_CBREAD(chid) \
	(host->base_regs + HOST1X_SYNC_OFFSET + HOST1X_SYNC_CBREAD(chid))

#define CH_CBSTAT(chid) \
	(host->base_regs + HOST1X_SYNC_OFFSET + HOST1X_SYNC_CBSTAT(chid))

#if HOST1X_HW >= 4
#define CHANNEL_CHANNELCTRL(chid) \
	(host->base_regs + HOST1X_CHANNEL_CHANNELCTRL(chid))
#endif

#else

#define CH_DMASTART_HI(chid) \
	(host->base_regs + HOST1X_CHANNEL_DMASTART_HI(chid))

#define CH_DMAEND_HI(chid) \
	(host->base_regs + HOST1X_CHANNEL_DMAEND_HI(chid))

#define CH_CMDPROC_STOP(chid) \
	(host->base_regs + HOST1X_CHANNEL_CMDPROC_STOP(chid))

#define CH_TEARDOWN(chid) \
	(host->base_regs + HOST1X_CHANNEL_TEARDOWN(chid))

#define CH_CMDFIFO_RDATA(chid) \
	(host->base_regs + HOST1X_CHANNEL_CMDFIFO_RDATA(chid))

#define CH_CMDP_OFFSET(chid) \
	(host->base_regs + HOST1X_CHANNEL_CMDP_OFFSET(chid))

#define CH_CMDP_CLASS(chid) \
	(host->base_regs + HOST1X_CHANNEL_CMDP_CLASS(chid))

#define CH_CMDP_CLASS(chid) \
	(host->base_regs + HOST1X_CHANNEL_CMDP_CLASS(chid))

#define HV_CH_KERNEL_FILTER_GBUFFER(idx) \
	(host->hv_regs + HOST1X_HV_CH_KERNEL_FILTER_GBUFFER(idx))

#endif

static inline
void host1x_hw_channel_stop(struct host1x *host, unsigned int id)
{
#if HOST1X_HW < 6
	u32 value;

	/* stop issuing commands from the command FIFO */
	value = readl_relaxed(CH_CMDPROC_STOP);
	writel_relaxed(value | BIT(id), CH_CMDPROC_STOP);
#else
	writel_relaxed(1, CH_CMDPROC_STOP(id));
#endif

	/* stop DMA from fetching on this channel and set DMAGET = DMAPUT */
	writel_relaxed(HOST1X_CHANNEL_DMACTRL_DMASTOP |
		       HOST1X_CHANNEL_DMACTRL_DMAGETRST |
		       HOST1X_CHANNEL_DMACTRL_DMAINITGET, CH_DMACTRL(id));
}

static inline
void host1x_hw_channel_start(struct host1x *host, unsigned int id)
{
#if HOST1X_HW < 6
	u32 value;

	value = readl_relaxed(CH_CMDPROC_STOP);
	writel_relaxed(value & ~BIT(id), CH_CMDPROC_STOP);
#else
	writel_relaxed(0, CH_CMDPROC_STOP(id));
#endif

	/* stop holding DMA, it shall be in a paused state now */
	writel_relaxed(0x0, CH_DMACTRL(id));
}

static inline void
host1x_hw_channel_teardown(struct host1x *host, unsigned int id)
{
	/*
	 * Reset channel's command FIFO and release any locks it has in
	 * the arbiter.
	 */
#if HOST1X_HW < 6
	writel_relaxed(BIT(id), CH_TEARDOWN);
#else
	writel_relaxed(1, CH_TEARDOWN(id));
#endif
}

static inline u32
host1x_hw_channel_dmaget(struct host1x *host, unsigned int id)
{
	return readl_relaxed(CH_DMAGET(id));
}

static inline u32
host1x_hw_channel_dmaput(struct host1x *host, unsigned int id)
{
	return readl_relaxed(CH_DMAPUT(id));
}

static inline u32
host1x_hw_channel_dmactrl(struct host1x *host, unsigned int id)
{
	return readl_relaxed(CH_DMACTRL(id));
}

#if HOST1X_HW < 6
static inline u32
host1x_hw_channel_cbread(struct host1x *host, unsigned int id)
{
	return readl_relaxed(CH_CBREAD(id));
}

static inline u32
host1x_hw_channel_cbstat(struct host1x *host, unsigned int id)
{
	return readl_relaxed(CH_CBSTAT(id));
}
#else
static inline u32
host1x_hw_channel_cmdfifo_rdata(struct host1x *host, unsigned int id)
{
	return readl_relaxed(CH_CMDFIFO_RDATA(id));
}

static inline u32
host1x_hw_channel_cmdp_offset(struct host1x *host, unsigned int id)
{
	return readl_relaxed(CH_CMDP_OFFSET(id));
}

static inline u32
host1x_hw_channel_cmdp_class(struct host1x *host, unsigned int id)
{
	return readl_relaxed(CH_CMDP_CLASS(id));
}
#endif

static inline void
host1x_hw_channel_init(struct host1x_channel *chan)
{
	struct host1x_pushbuf *pb = &chan->pb;
	struct host1x *host = chan->host;
	unsigned int id = chan->id;
#if HOST1X_HW >= 6
	u32 value;
#endif
	/* reset HW state */
	host1x_hw_channel_stop(chan->host, chan->id);
	host1x_hw_channel_teardown(chan->host, chan->id);

	/*
	 * Keep DMA on hold while updating addresses since any update
	 * triggers the memory fetching process.
	 */
	writel_relaxed(HOST1X_CHANNEL_DMACTRL_DMASTOP, CH_DMACTRL(id));

	/* set DMAPUT to push buffer's start */
	writel_relaxed(host1x_soc_pushbuf_dmastart(pb), CH_DMAPUT(id));

	/* set DMAGET = DMAPUT */
	writel_relaxed(HOST1X_CHANNEL_DMACTRL_DMASTOP |
		       HOST1X_CHANNEL_DMACTRL_DMAGETRST |
		       HOST1X_CHANNEL_DMACTRL_DMAINITGET, CH_DMACTRL(id));

	/*
	 * Set DMASTART to 0x0, DMAGET and DMAPUT will be treated as absolute
	 * addresses in this case.
	 */
	writel_relaxed(0x00000000, CH_DMASTART(id));

	/* do not limit DMA addressing */
	writel_relaxed(0xffffffff, CH_DMAEND(id));

#if HOST1X_HW >= 6
	/* set upper halves of the addresses */
	writel_relaxed(0x00000000, CH_DMASTART_HI(id));
	writel_relaxed(0xffffffff, CH_DMAEND_HI(id));

	/* disable setclass command filter for gather buffers */
	spin_lock(&host->channels_lock);

	value = readl_relaxed(HV_CH_KERNEL_FILTER_GBUFFER(id / 32));

	writel_relaxed(value & ~BIT(id % 32),
		       HV_CH_KERNEL_FILTER_GBUFFER(id / 32));

	spin_unlock(&host->channels_lock);
#elif HOST1X_HW >= 4
	writel_relaxed(HOST1X_CHANNEL_CHANNELCTRL_KERNEL_FILTER_GBUFFER(0),
		       CHANNEL_CHANNELCTRL(id));
#endif
	host1x_hw_channel_start(chan->host, chan->id);
}

static inline void
host1x_hw_channel_submit(struct host1x_channel *chan,
			 struct host1x_job *job)
{
	struct host1x_pushbuf *pb = &chan->pb;
	struct host1x *host = chan->host;
	unsigned int id = chan->id;

	/* trigger DMA execution (DMAGET != DMAPUT) */
	writel_relaxed(host1x_soc_pushbuf_dmaput_addr(pb), CH_DMAPUT(id));
}
