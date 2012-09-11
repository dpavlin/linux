/*
 * cpufreq.c -- MX31 CPUfreq driver.
 * MX31 cpufreq driver
 *
 * Copyright 2008 Lab126, Inc.  All rights reserved.
 * Manish Lachwani (lachwani@lab126.com)
 *
 * Support for CPUFreq on the Mario platform. It supports cpu frequency scaling
 * between four operating points as defined by Freescale. No voltage scaling is
 * done here as it is taken care off by dvfs code.
 * 
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/cacheflush.h>
#include <asm/arch/pmic_power.h>

static struct clk *cpu_clk;

/*
 * turn debug on/off
 */
#undef MX3_CPU_FREQ_DEBUG

/*
 * Freescale defined operating points
 */
static struct cpufreq_frequency_table mx31_freq_table[] = {
	{0x01,	266000},
	{0x02,	532000},
	{0,	CPUFREQ_TABLE_END},
};

static int mx3_verify_speed(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;

	return cpufreq_frequency_table_verify(policy, mx31_freq_table);
}

static unsigned int mx3_get_speed(unsigned int cpu)
{
	if (cpu)
		return 0;
	return clk_get_rate(cpu_clk) / 1000;
}

static int calc_frequency(int target, unsigned int relation)
{
	int i = 0;
	
	if (relation == CPUFREQ_RELATION_H) {
		for (i = 3; i >= 0; i--) {
			if (mx31_freq_table[i].frequency <= target)
				return mx31_freq_table[i].frequency;
		}
	} else if (relation == CPUFREQ_RELATION_L) {
		for (i = 0; i <= 3; i++) {
			if (mx31_freq_table[i].frequency >= target)
				return mx31_freq_table[i].frequency;
		}
	}
	printk(KERN_ERR "Error: No valid cpufreq relation\n");
	return 532000;
}

/* 
 * Set the destination CPU frequency target
 */
static int mx3_set_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	struct cpufreq_freqs freqs;
	long freq;
	unsigned long flags;

	if (target_freq < policy->cpuinfo.min_freq)
		target_freq = policy->cpuinfo.min_freq;

	if (target_freq < policy->min)
		target_freq = policy->min;

	freq = calc_frequency(target_freq, relation) * 1000;
	freqs.old = clk_get_rate(cpu_clk) / 1000;
	freqs.new = (freq + 500) / 1000;
	freqs.cpu = 0;
	freqs.flags = 0;
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

#ifdef MX3_CPU_FREQ_DEBUG
	printk ("ARM frequency: %dMHz\n", (int)clk_get_rate(cpu_clk));
#endif
	local_irq_save(flags);
	/*
	 * MCU clk
	 */
	clk_set_rate(cpu_clk, freq);

	local_irq_restore(flags);

#ifdef MX3_CPU_FREQ_DEBUG
	printk ("ARM frequency after change: %dMHz\n", (int)clk_get_rate(cpu_clk));
#endif
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	clk_put(cpu_clk);
	return 0;
}

/* 
 * Driver initialization
 */
static int __init mx3_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	int ret = 0;

	printk("Mario MX31 CPUFREQ driver\n");

	if (policy->cpu != 0)
		return -EINVAL;
		
	cpu_clk = clk_get(NULL, "cpu_clk");

	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);
		
	policy->cur = policy->min = policy->max = 
		clk_get_rate(cpu_clk) / 1000;
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.min_freq = 266000;
	policy->cpuinfo.max_freq = 532000;
	/* Set the transition latency to 50 us */
	policy->cpuinfo.transition_latency = 5 * 10000;

	ret = cpufreq_frequency_table_cpuinfo(policy, mx31_freq_table);
	if (ret < 0) 
		return ret;

	clk_put(cpu_clk);
	cpufreq_frequency_table_get_attr(mx31_freq_table, policy->cpu);

	return 0;
}

static int mx3_cpufreq_driver_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	/* reset CPU to 532MHz */
	clk_set_rate(cpu_clk, 532000 * 1000);
	clk_put(cpu_clk);
	return 0;
}

static struct cpufreq_driver mx3_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= mx3_verify_speed,
	.target		= mx3_set_target,
	.get		= mx3_get_speed,
	.init		= mx3_cpufreq_driver_init,
	.exit		= mx3_cpufreq_driver_exit,
	.name		= "MX31",
};

static int __init mx3_cpufreq_init(void)
{
	return cpufreq_register_driver(&mx3_driver);
}

arch_initcall(mx3_cpufreq_init);
