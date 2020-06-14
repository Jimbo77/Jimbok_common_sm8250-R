// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */
// Copyright (C) 2019 all additions Erik Müller <pappschlumpf@xda>

#define pr_fmt(fmt) "devfreq_boost_gpu: " fmt

#include <linux/devfreq_boost_gpu.h>
#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#else
#include <linux/fb.h>
#endif
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/version.h>

/* The sched_param struct is located elsewhere in newer kernels */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <uapi/linux/sched/types.h>
#endif

static unsigned short flex_boost_gpu_duration __read_mostly = CONFIG_FLEX_DEVFREQ_BOOST_GPU_DURATION_MS;
static unsigned short input_boost_gpu_duration __read_mostly = CONFIG_DEVFREQ_INPUT_BOOST_GPU_DURATION_MS;
static unsigned int devfreq_thread_prio __read_mostly = CONFIG_DEVFREQ_GPU_THREAD_PRIORITY;
static unsigned int devfreq_boost_gpu_freq_low  __read_mostly = CONFIG_DEVFREQ_MSM_GPUBW_BOOST_FREQ_LOW;
static unsigned int devfreq_boost_gpu_freq __read_mostly = CONFIG_DEVFREQ_MSM_GPUBW_BOOST_FREQ;

module_param(flex_boost_gpu_duration, short, 0644);
module_param(input_boost_gpu_duration, short, 0644);
module_param(devfreq_boost_gpu_freq, uint, 0644);
module_param(devfreq_boost_gpu_freq_low, uint, 0644);

enum {
	SCREEN_ON,
	INPUT_BOOST,
	FLEX_BOOST,
	WAKE_BOOST,
	MAX_BOOST
};

struct boost_dev {
	struct workqueue_struct *wq_i;
	struct workqueue_struct *wq_f;
	struct workqueue_struct *wq_m;
	struct devfreq *df;
	struct delayed_work input_unboost;
	struct delayed_work flex_unboost;
	struct delayed_work max_unboost;
	struct work_struct boost;
	wait_queue_head_t boost_waitq;
	unsigned long state;
};

struct df_boost_drv {
	struct boost_dev devices[DEVFREQ_MAX_GPU];
#ifdef CONFIG_DRM_MSM
	struct notifier_block msm_drm_notif;
#else
	struct notifier_block fb_notif;
#endif
};

static void devfreq_input_unboost(struct work_struct *work);
static void devfreq_max_unboost(struct work_struct *work);
static void devfreq_flex_unboost(struct work_struct *work);

#define BOOST_DEV_INIT(b, dev) .devices[dev] = {				\
	.input_unboost =							\
		__DELAYED_WORK_INITIALIZER((b).devices[dev].input_unboost,	\
					   devfreq_input_unboost, 0),		\
	.flex_unboost =								\
		__DELAYED_WORK_INITIALIZER((b).devices[dev].flex_unboost,	\
					   devfreq_flex_unboost, 0),		\
	.max_unboost =								\
		__DELAYED_WORK_INITIALIZER((b).devices[dev].max_unboost,	\
					   devfreq_max_unboost, 0),		\
	.boost_waitq =								\
		__WAIT_QUEUE_HEAD_INITIALIZER((b).devices[dev].boost_waitq),	\
}

static struct df_boost_drv df_boost_drv_g __read_mostly = {
	BOOST_DEV_INIT(df_boost_drv_g, DEVFREQ_MSM_GPUBW)
};

static void __devfreq_boost_gpu_kick(struct boost_dev *b)
{
	if (!READ_ONCE(b->df) || !test_bit(SCREEN_ON, &b->state))
		return;

	if (!mod_delayed_work(b->wq_i, &b->input_unboost,
			msecs_to_jiffies(input_boost_gpu_duration))) {
		set_bit(INPUT_BOOST, &b->state);
		wake_up(&b->boost_waitq);
	}
}

static void __devfreq_boost_kick_flex(struct boost_dev *b, unsigned int duration_ms)
{
	unsigned int act_duration_ms = flex_boost_gpu_duration;

	if (!READ_ONCE(b->df) || !test_bit(SCREEN_ON, &b->state) || ((flex_boost_gpu_duration == 0) && (duration_ms == 0)))
		return;

	if (duration_ms > 0)
		act_duration_ms = duration_ms;

	if (!mod_delayed_work(b->wq_f, &b->flex_unboost,
			msecs_to_jiffies(act_duration_ms))) {
		set_bit(FLEX_BOOST, &b->state);
		wake_up(&b->boost_waitq);
	}
}

void devfreq_boost_gpu_kick_flex(enum df_device_gpu device, unsigned int duration_ms)
{
	struct df_boost_drv *d = &df_boost_drv_g;

	__devfreq_boost_kick_flex(d->devices + device, duration_ms);
}

static void __devfreq_boost_kick_max(struct boost_dev *b,
				     unsigned int duration_ms)
{
	unsigned long boost_jiffies = msecs_to_jiffies(duration_ms);
	unsigned long curr_expires, new_expires;

	if (!READ_ONCE(b->df) || !test_bit(SCREEN_ON, &b->state))
		return;

	if (!mod_delayed_work(b->wq_m, &b->max_unboost,
			      msecs_to_jiffies(duration_ms))) {
		set_bit(MAX_BOOST, &b->state);
		wake_up(&b->boost_waitq);
	}
}

void devfreq_boost_gpu_kick_max(enum df_device_gpu device, unsigned int duration_ms)
{
	struct df_boost_drv *d = &df_boost_drv_g;
	struct boost_dev *b = d->devices + device;
		
	__devfreq_boost_kick_max(b, duration_ms);
}

static void __devfreq_boost_kick_wake(struct boost_dev *b,
				     unsigned int duration_ms)
{
	unsigned long boost_jiffies = msecs_to_jiffies(duration_ms);
	unsigned long curr_expires, new_expires;

	if (!READ_ONCE(b->df))
		return;

	if (!mod_delayed_work(b->wq_m, &b->max_unboost,
			      msecs_to_jiffies(duration_ms))) {
		set_bit(WAKE_BOOST, &b->state);
		wake_up(&b->boost_waitq);
	}
}

void devfreq_boost_gpu_kick_wake(enum df_device_gpu device, unsigned int duration_ms)
{
	struct df_boost_drv *d = &df_boost_drv_g;
	struct boost_dev *b = d->devices + device;
		
	__devfreq_boost_kick_wake(b, duration_ms);
}

void devfreq_register_boost_gpu_device(enum df_device_gpu device, struct devfreq *df)
{
	struct df_boost_drv *d = &df_boost_drv_g;
	struct boost_dev *b;

	df->is_boost_device = true;
	b = d->devices + device;
	WRITE_ONCE(b->df, df);
}

static void devfreq_input_unboost(struct work_struct *work)
{
	struct boost_dev *b = container_of(to_delayed_work(work),
					   typeof(*b), input_unboost);

	clear_bit(INPUT_BOOST, &b->state);
	wake_up(&b->boost_waitq);
}

static void devfreq_flex_unboost(struct work_struct *work)
{
	struct boost_dev *b = container_of(to_delayed_work(work),
					   typeof(*b), flex_unboost);

	clear_bit(FLEX_BOOST, &b->state);
	wake_up(&b->boost_waitq);
}

static void devfreq_max_unboost(struct work_struct *work)
{
	struct boost_dev *b = container_of(to_delayed_work(work),
					   typeof(*b), max_unboost);

	clear_bit(WAKE_BOOST, &b->state);
	clear_bit(MAX_BOOST, &b->state);
	wake_up(&b->boost_waitq);
}

static void devfreq_update_boosts(struct boost_dev *b, unsigned long state)
{
	struct devfreq *df = b->df;
	if (!READ_ONCE(b->df))
		return;
	if (unlikely(!(0x01 & state))) {
		mutex_lock(&df->lock);
		df->min_freq = df->profile->freq_table[0];
		df->max_boost = 0x08 & state ? 
					true :
					false;
		update_devfreq(df);
		mutex_unlock(&df->lock);
	} else {
		mutex_lock(&df->lock);
		df->min_freq = 0x04 & state ?
			devfreq_boost_gpu_freq_low :
			df->profile->freq_table[0];
		df->min_freq = 0x02 & state ?
			devfreq_boost_gpu_freq :
			df->profile->freq_table[0];
			df->max_boost = 0x10 & state;
		update_devfreq(df);
		mutex_unlock(&df->lock);
	}
}


static int devfreq_boost_thread(void *data)
{
	static struct sched_param sched_max_rt_prio;
	struct boost_dev *b = data;
	unsigned long old_state = 0;

	if (devfreq_thread_prio == 99)
 		sched_max_rt_prio.sched_priority = MAX_RT_PRIO - 1;
	else 
		sched_max_rt_prio.sched_priority = devfreq_thread_prio;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &sched_max_rt_prio);

	while (!kthread_should_stop()) {
		unsigned long curr_state;

		wait_event(b->boost_waitq,
			(curr_state = READ_ONCE(b->state)) != old_state ||
			kthread_should_stop());
		old_state = curr_state;
		devfreq_update_boosts(b, curr_state);
	}

	return 0;
}

#ifdef CONFIG_DRM_MSM
static int msm_drm_notifier_cb(struct notifier_block *nb, unsigned long action,
			       void *data)
{
	struct df_boost_drv *d = container_of(nb, typeof(*d), msm_drm_notif);
	int i, *blank = ((struct msm_drm_notifier *)data)->data;

	/* Parse framebuffer blank events as soon as they occur */
	if (action != MSM_DRM_EARLY_EVENT_BLANK)
		return NOTIFY_OK;

	/* Boost when the screen turns on and unboost when it turns off */
	for (i = 0; i < DEVFREQ_MAX_GPU; i++) {
		struct boost_dev *b = d->devices + i;

		if (*blank == MSM_DRM_BLANK_UNBLANK_CUST) {
			devfreq_boost_gpu_kick_wake(DEVFREQ_MSM_GPUBW, 1000);
			set_bit(SCREEN_ON, &b->state);
		} else if (*blank == MSM_DRM_BLANK_POWERDOWN_CUST) {
			clear_bit(SCREEN_ON, &b->state);
			clear_bit(WAKE_BOOST, &b->state);
			clear_bit(MAX_BOOST, &b->state);
			clear_bit(FLEX_BOOST, &b->state);
			clear_bit(INPUT_BOOST, &b->state);
			pr_info("Screen off, boosts turned off\n");
			wake_up(&b->boost_waitq);
		}
	}

	return NOTIFY_OK;
}
#else
static int fb_notifier_cb(struct notifier_block *nb, unsigned long action,
			  void *data)
{
	struct df_boost_drv *d = container_of(nb, typeof(*d), fb_notif);
	int i, *blank = ((struct fb_event *)data)->data;

	/* Parse framebuffer blank events as soon as they occur */
	if (action != FB_EARLY_EVENT_BLANK)
		return NOTIFY_OK;

	/* Boost when the screen turns on and unboost when it turns off */
	for (i = 0; i < DEVFREQ_MAX_GPU; i++) {
		struct boost_dev *b = d->devices + i;

		if (*blank == FB_BLANK_UNBLANK) {
			devfreq_boost_kick_wake(DEVFREQ_MSM_CPUBW, 1000);
			set_bit(SCREEN_ON, &b->state);
		} else {
			clear_bit(SCREEN_ON, &b->state);
			clear_bit(WAKE_BOOST, &b->state);
			clear_bit(MAX_BOOST, &b->state);
			clear_bit(FLEX_BOOST, &b->state);
			clear_bit(INPUT_BOOST, &b->state);
			wake_up(&b->boost_waitq);
		}
	}

	return NOTIFY_OK;
}
#endif

static void devfreq_boost_gpu_input_event(struct input_handle *handle,
				      unsigned int type, unsigned int code,
				      int value)
{
	struct df_boost_drv *d = handle->handler->private;
	int i;

	for (i = 0; i < DEVFREQ_MAX_GPU; i++)
		__devfreq_boost_gpu_kick(d->devices + i);
}

static int devfreq_boost_gpu_input_connect(struct input_handler *handler,
				       struct input_dev *dev,
				       const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "devfreq_boost_gpu_handle";

	ret = input_register_handle(handle);
	if (ret)
		goto free_handle;

	ret = input_open_device(handle);
	if (ret)
		goto unregister_handle;

	return 0;

unregister_handle:
	input_unregister_handle(handle);
free_handle:
	kfree(handle);
	return ret;
}

static void devfreq_boost_gpu_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id devfreq_boost_gpu_ids[] = {
	/* Multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) }
	},
	/* Touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) }
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) }
	},
	{ }
};

static struct input_handler devfreq_boost_gpu_input_handler = {
	.event		= devfreq_boost_gpu_input_event,
	.connect	= devfreq_boost_gpu_input_connect,
	.disconnect	= devfreq_boost_gpu_input_disconnect,
	.name		= "devfreq_boost_gpu_handler",
	.id_table	= devfreq_boost_gpu_ids
};

static int __init devfreq_boost_gpu_init(void)
{
	struct df_boost_drv *d = &df_boost_drv_g;
	struct task_struct *thread[DEVFREQ_MAX_GPU];
	int i, ret;

	for (i = 0; i < DEVFREQ_MAX_GPU; i++) {
		struct boost_dev *b = d->devices + i;
		b->wq_i = alloc_workqueue("devfreq_boost_gpu_wq_i", WQ_POWER_EFFICIENT, 0);
		b->wq_f = alloc_workqueue("devfreq_boost_gpu_wq_f", WQ_POWER_EFFICIENT, 0);
		b->wq_m = alloc_workqueue("devfreq_boost_gpu_wq_m", WQ_POWER_EFFICIENT, 0);
		
		thread[i] = kthread_run_low_power(devfreq_boost_thread, b,
						      "devfreq_boost_gpud/%d", i);
		if (IS_ERR(thread[i])) {
			ret = PTR_ERR(thread[i]);
			pr_err("Failed to create kthread, err: %d\n", ret);
			goto stop_kthreads;
		}
		set_bit(SCREEN_ON, &b->state);
	}

	devfreq_boost_gpu_input_handler.private = d;
	ret = input_register_handler(&devfreq_boost_gpu_input_handler);
	if (ret) {
		pr_err("Failed to register input handler, err: %d\n", ret);
		goto stop_kthreads;
	}
#ifdef CONFIG_DRM_MSM
	d->msm_drm_notif.notifier_call = msm_drm_notifier_cb;
	d->msm_drm_notif.priority = INT_MAX-2;
	ret = msm_drm_register_client(&d->msm_drm_notif);
	if (ret) {
		pr_err("Failed to register msm_drm_notifier, err: %d\n", ret);
		goto unregister_handler;
	}
#else
	d->fb_notif.notifier_call = fb_notifier_cb;
	d->fb_notif.priority = INT_MAX-2;
	ret = fb_register_client(&d->fb_notif);
	if (ret) {
		pr_err("Failed to register fb notifier, err: %d\n", ret);
		goto unregister_handler;
	}
#endif

	return 0;

unregister_handler:
	input_unregister_handler(&devfreq_boost_gpu_input_handler);
stop_kthreads:
	while (i--)
		kthread_stop(thread[i]);
	return ret;
}
late_initcall(devfreq_boost_gpu_init);
