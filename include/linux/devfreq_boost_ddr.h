/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018-2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */
#ifndef _DEVFREQ_BOOST_DDR_H_
#define _DEVFREQ_BOOST_DDR_H_

#include <linux/devfreq.h>

enum df_device_ddr {
	DEVFREQ_MSM_DDRBW,
	DEVFREQ_MAX_DDR
};

#ifdef CONFIG_DEVFREQ_BOOST_DDR
void devfreq_boost_ddr_kick_flex(enum df_device_ddr device, unsigned int duration_ms);
void devfreq_boost_ddr_kick_max(enum df_device_ddr device, unsigned int duration_ms);
void devfreq_boost_ddr_kick_wake(enum df_device_ddr device, unsigned int duration_ms);
void devfreq_register_boost_ddr_device(enum df_device_ddr device, struct devfreq *df);
#else
static inline
void devfreq_boost_ddr_kick_flex(enum df_device_ddr device, unsigned int duration_ms)
{
}
static inline
void devfreq_boost_ddr_kick_max(enum df_device_ddr device, unsigned int duration_ms)
{
}
static inline
void devfreq_boost_ddr_kick_wake(enum df_device_ddr device, unsigned int duration_ms)
{
}
static inline
void devfreq_register_boost_ddr_device(enum df_device_ddr device, struct devfreq *df)
{
}
#endif

#endif /* _DEVFREQ_BOOST_DDR_H_ */
