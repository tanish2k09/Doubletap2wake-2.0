/*
 * drivers/input/touchscreen/doubletap2wake.c
 *
 *
 * Copyright (c) 2013, Dennis Rassmann <showp1984@gmail.com>
 * Copyright (c) 2015, Vineeth Raj <contact.twn@openmailbox.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/hrtimer.h>
#include <asm-generic/cputime.h>
#include <linux/input/doubletap2wake.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

#ifdef CONFIG_POCKETMOD
#include <linux/pocket_mod.h>
#endif

/* uncomment since no touchscreen defines android touch, do that here */
//#define ANDROID_TOUCH_DECLARED

#define WAKE_HOOKS_DEFINED

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
#include <linux/lcd_notify.h>
#else
#include <linux/earlysuspend.h>
#endif
#endif

/* if Sweep2Wake is compiled it will already have taken care of this */
#ifdef CONFIG_TOUCHSCREEN_SWEEP2WAKE
#define ANDROID_TOUCH_DECLARED
#endif

/* Version, author, desc, etc */
#define DRIVER_AUTHOR "Tanish <tanish2k09.dev@gmail.com>"
#define DRIVER_DESCRIPTION "Doubletap2wake for almost any device"
#define DRIVER_VERSION "2.0"
#define LOGTAG "[doubletap2wake]: "

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPLv3");

/* Tuneables */
#define DT2W_DEBUG         0
#define DT2W_DEFAULT       1

#define DT2W_PWRKEY_DUR   20
#define DT2W_RADIUS       80
#define VIB_STRENGTH      70
#define DT2W_TIME         750
#define DT2W_OFF 		  0
#define DT2W_ON 		  1

/* Wake Gestures */
//#define WAKE_GESTURE		0x0b
//#define TRIGGER_TIMEOUT		50

/////////////////////////////////////////////////////////////////////////////////////////////////////////
// My code :

unsigned int vib_strength = VIB_STRENGTH;
bool revert_area=0;
unsigned int dt2w_distance_x=DT2W_DISTANCE_X;
unsigned int dt2w_distance_y=DT2W_DISTANCE_Y;
unsigned int dt2w_time=DT2W_TIME;
unsigned int left=0;
unsigned int right=720;
unsigned int co_up=0;
unsigned int co_down=1280;
unsigned int Dt2w_regions=0;
//bool dt2w_override_vibration_to_null=true;
unsigned int dt2w_override_taps=3; //******************************************
unsigned int pocket_override_timeout = 800; //
static cputime64_t initial_override_press=0; 
unsigned int vibration_strength_on_pocket_override = 150;	//
static unsigned int current_tap=0;
static int exec_count = true;
/////////////////////////////////////////////////////////////////////////////////////////////////////////

extern struct vib_trigger *vib_trigger;
static struct input_dev *gesture_dev;
//extern int gestures_switch;
extern void set_vibrate(int value);

/* Resources */
int dt2w_switch = DT2W_DEFAULT;
static cputime64_t tap_time_pre = 0;
static int touch_x = 0, touch_y = 0, touch_nr = 0, x_pre = 0, y_pre = 0;
static bool touch_x_called = false, touch_y_called = false, touch_cnt = true;
bool dt2w_scr_suspended = false;
bool in_phone_call = false;
//static unsigned long pwrtrigger_time[2] = {0, 0};
#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
static struct notifier_block dt2w_lcd_notif;
#endif
#endif
static struct input_dev * doubletap2wake_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);
static struct workqueue_struct *dt2w_input_wq;
static struct work_struct dt2w_input_work;

/* PowerKey setter */
/*void doubletap2wake_setdev(struct input_dev * input_device) {
	doubletap2wake_pwrdev = input_device;
	printk(LOGTAG"set doubletap2wake_pwrdev: %s\n", doubletap2wake_pwrdev->name);
}*/

/* Read cmdline for dt2w */
static int __init read_dt2w_cmdline(char *dt2w)
{
	if (strcmp(dt2w, "1") == 0) {
		pr_info("[cmdline_dt2w]: DoubleTap2Wake enabled. | dt2w='%s'\n", dt2w);
		dt2w_switch = 1;
	} else if (strcmp(dt2w, "0") == 0) {
		pr_info("[cmdline_dt2w]: DoubleTap2Wake disabled. | dt2w='%s'\n", dt2w);
		dt2w_switch = 0;
	} else {
		pr_info("[cmdline_dt2w]: No valid input found. Going with default: | dt2w='%u'\n", dt2w_switch);
	}
	return 1;
}
__setup("dt2w=", read_dt2w_cmdline);

/* Wake Gestures */
void gestures_setdev(struct input_dev *input_device)
{
	gesture_dev = input_device;
	return;
}


/* reset on finger release */
static void doubletap2wake_reset(void) {
	exec_count = true;
	touch_nr = 0;
	tap_time_pre = 0;
	x_pre = 0;
	y_pre = 0;
}

/* PowerKey work func */
static void doubletap2wake_presspwr(struct work_struct * doubletap2wake_presspwr_work) {
	if (!mutex_trylock(&pwrkeyworklock))
		return;
	input_event(doubletap2wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(doubletap2wake_pwrdev, EV_SYN, 0, 0);
	msleep(DT2W_PWRKEY_DUR);
	input_event(doubletap2wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(doubletap2wake_pwrdev, EV_SYN, 0, 0);
	msleep(DT2W_PWRKEY_DUR);
	mutex_unlock(&pwrkeyworklock);
	return;
}
static DECLARE_WORK(doubletap2wake_presspwr_work, doubletap2wake_presspwr);

/* PowerKey trigger */
static void doubletap2wake_pwrtrigger(void) {
	/*pwrtrigger_time[1] = pwrtrigger_time[0];
        pwrtrigger_time[0] = jiffies;

	if (pwrtrigger_time[0] - pwrtrigger_time[1] < TRIGGER_TIMEOUT)
		return;*/

	schedule_work(&doubletap2wake_presspwr_work);
	return;
}

static bool calc_within_range(int x_pre, int y_pre, int x_new, int y_new, int radius_max) {
	int calc_radius = ((x_new-x_pre)*(x_new-x_pre)) + ((y_new-y_pre)*(y_new-y_pre)) ;
    if (calc_radius < ((radius_max)*(radius_max)))
        return true;
    return false;
}

/* init a new touch */
static void new_touch(int x, int y) {
	tap_time_pre = ktime_to_ms(ktime_get());
	x_pre = x;
	y_pre = y;
	touch_nr++;
}

/* Doubletap2wake main function */
static void detect_doubletap2wake(int x, int y)
{
	if ((dt2w_switch > 0) && (exec_count) && (touch_cnt)) {
		touch_cnt = false;
		if (touch_nr == 0) {        // This will be true on first touch
			new_touch(x, y);
		} 
		else if (touch_nr == 1)     //This is true on second touch
		{
            // Check tap distance and time condtions
			if ((calc_within_range(x_pre, y_pre,x,y, DT2W_DISTANCE_X) == true) && ((ktime_to_ms(ktime_get())-tap_time_pre) < dt2w_time))
            {
//				touch_nr++;     Here we know that the touch number is going to be 2 and hence >1 so the if statement down below will turn true.
//                              so it is better that we don't wait for the control to go there, and we pwr_on it from here directly
				exec_count = false;
                doubletap2wake_pwrtrigger();  // We queue the screen on first, as it takes more time to do than vibration.
			    set_vibrate(vib_strength);    // While the screen is queued, and is waking, we hammer the vibrator. Minor UX tweak.
			    doubletap2wake_reset();     // Here the touch number is also reset to 0, but the program executes as needed. See yourself.
            }
			else {          //If the second tap isn't close or fast enough, reset previous coords, treat second tap as a separate first tap
				doubletap2wake_reset();
				new_touch(x, y);
			}
		}
	}
}

static void detect_pocket_override_dt2w(void)
{
	if (current_tap == 1)
	{
		initial_override_press = ktime_to_ms(ktime_get());

	}
	else if ((current_tap == (dt2w_override_taps))&&(((ktime_to_ms(ktime_get()))-initial_override_press)<pocket_override_timeout))
	{
		doubletap2wake_pwrtrigger();
		set_vibrate(vibration_strength_on_pocket_override);
		current_tap = 0;
		initial_override_press = 0;
	}
	else if (((ktime_to_ms(ktime_get()))-initial_override_press)>pocket_override_timeout)
	{
		current_tap = 0;
	}
    touch_cnt=false;
	return;
}

static void dt2w_input_callback(struct work_struct *unused) {
	
/*	if (touch_y>1280)
	{
		dt2w_override_vibration_to_null=true;
	}
	else
		dt2w_override_vibration_to_null=false;
*/		
#ifdef CONFIG_POCKETMOD
  	if (device_is_pocketed()){
       if(touch_cnt)
       {
		current_tap++;
		detect_pocket_override_dt2w();
       }
  		return;
  	}
#endif
//avoid button presses being recognized as touches
//Dt2w_on_buttons=0;
//0 = touchscreen only
//1 = button only
//2 = both


// We reach here if device isn't pocketed anymore. We need to make sure that the counter is reset.
current_tap = 0;

if (((Dt2w_regions==1)||(Dt2w_regions==2))&&(touch_y >1280))
    detect_doubletap2wake(touch_x, touch_y);

if (revert_area==0)
{
    if (touch_x>=left && touch_x<=right && touch_y>=co_up && touch_y<=co_down && Dt2w_regions==0)
    {
        detect_doubletap2wake(touch_x, touch_y);
    }
    else if (Dt2w_regions==2)
    {
        if (touch_x>=left && touch_x<=right && touch_y>=co_up && touch_y<=co_down)
            detect_doubletap2wake(touch_x, touch_y);
    }
}

else //Means area should be reverted...
{
    if (Dt2w_regions==0)
    {
        if (touch_x<=left || touch_x>=right || touch_y<=co_up || touch_y>=co_down)
        {
            detect_doubletap2wake(touch_x, touch_y);
        }
    }
    else if (Dt2w_regions==2)
    {
        if (touch_x<=left || touch_x>=right || touch_y<=co_up || touch_y>=co_down)
            detect_doubletap2wake(touch_x, touch_y);
    }
}

	return;
}

static void dt2w_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {
#if DT2W_DEBUG
	pr_info("doubletap2wake: code: %s|%u, val: %i\n",
		((code==ABS_MT_POSITION_X) ? "X" :
		(code==ABS_MT_POSITION_Y) ? "Y" :
		((code==ABS_MT_TRACKING_ID)||
			(code==330)) ? "ID" : "undef"), code, value);
#endif

	if (in_phone_call){
//        dt2w_override_vibration_to_null=false;
		return;
    }

	if (!(dt2w_scr_suspended)){
//        dt2w_override_vibration_to_null=false;	
    	return;
    }
	if (code == ABS_MT_SLOT) {
		doubletap2wake_reset();
		return;
	}

	/*
	 * '330'? Many touch panels are 'broken' in the sense of not following the
	 * multi-touch protocol given in Documentation/input/multi-touch-protocol.txt.
	 * According to the docs, touch panels using the type B protocol must send in
	 * a ABS_MT_TRACKING_ID event after lifting the contact in the first slot.
	 * This should in the flow of events, help us set the necessary doubletap2wake
	 * variable and proceed as per the algorithm.
	 *
	 * This however is not the case with various touch panel drivers, and hence
	 * there is no reliable way of tracking ABS_MT_TRACKING_ID on such panels.
	 * Some of the panels however do track the lifting of contact, but with a
	 * different event code, and a different event value.
	 *
	 * So, add checks for those event codes and values to keep the algo flow.
	 *
	 * synaptics_s3203 => code: 330; val: 0
	 *
	 * Note however that this is not possible with panels like the CYTTSP3 panel
	 * where there are no such events being reported for the lifting of contacts
	 * though i2c data has a ABS_MT_TRACKING_ID or equivalent event variable
	 * present. In such a case, make sure the touch_cnt variable is publicly
	 * available for modification.
	 *
	 */
	if ((code == ABS_MT_TRACKING_ID && value == -1) || (code == 330 && value == 0)) {
		touch_cnt = true;
		return;
	}

	if (code == ABS_MT_POSITION_X) {
		touch_x = value;
		touch_x_called = true;
	}

	if (code == ABS_MT_POSITION_Y) {
		touch_y = value;
		touch_y_called = true;
	}

	if (touch_x_called || touch_y_called) {
		touch_x_called = false;
		touch_y_called = false;
		queue_work_on(0, dt2w_input_wq, &dt2w_input_work);
	}
}

static int input_dev_filter(struct input_dev *dev) {
	if (strstr(dev->name, "touch")||
			strstr(dev->name, "mtk-tpd")) {
		return 0;
	} else {
		return 1;
	}
}

static int dt2w_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) {
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "dt2w";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void dt2w_input_disconnect(struct input_handle *handle) {
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id dt2w_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler dt2w_input_handler = {
	.event		= dt2w_input_event,
	.connect	= dt2w_input_connect,
	.disconnect	= dt2w_input_disconnect,
	.name		= "dt2w_inputreq",
	.id_table	= dt2w_ids,
};

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
static int lcd_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
	case LCD_EVENT_ON_END:
		dt2w_scr_suspended = false;
		break;
	case LCD_EVENT_OFF_END:
		dt2w_scr_suspended = true;
		break;
	default:
		break;
	}

	return 0;
}
#else
static void dt2w_early_suspend(struct early_suspend *h) {
	dt2w_scr_suspended = true;
}

static void dt2w_late_resume(struct early_suspend *h) {
	dt2w_scr_suspended = false;
}

static struct early_suspend dt2w_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = dt2w_early_suspend,
	.resume = dt2w_late_resume,
};
#endif
#endif

/*
 * SYSFS stuff below here
 */
static ssize_t dt2w_doubletap2wake_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_switch);

	return count;
}

static ssize_t dt2w_doubletap2wake_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	/*if (buf[1] == '\n') {
		if (buf[0] == '0') {
			dt2w_switch = 0;
		} else if (buf[0] == '1') {
			dt2w_switch = 1;
		}
	}

	return count;
}

static DEVICE_ATTR(doubletap2wake, (S_IWUSR|S_IRUGO),
	dt2w_doubletap2wake_show, dt2w_doubletap2wake_dump);*/


	unsigned int new_dt2w_switch;

	if (!sscanf(buf, "%du", &new_dt2w_switch))
		return -EINVAL;

	if (new_dt2w_switch == dt2w_switch)
		return count;

	switch (new_dt2w_switch) {
		case DT2W_OFF :
		case DT2W_ON :
			dt2w_switch = new_dt2w_switch;
			/* through 'adb shell' or by other means, if the toggle
			 * is done several times, 0-to-1, 1-to-0, we need to
			 * inform the toggle correctly
			 */
			pr_info("[dump_dt2w]: DoubleTap2Wake toggled. | "
					"dt2w='%d' \n", dt2w_switch);
			return count;
		default:
			return -EINVAL;
	}

	/* We should never get here */
	return -EINVAL;
}

static DEVICE_ATTR(doubletap2wake, 0666,
	dt2w_doubletap2wake_show, dt2w_doubletap2wake_dump);

static ssize_t dt2w_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%s\n", DRIVER_VERSION);

	return count;
}

static ssize_t dt2w_version_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(doubletap2wake_version, (S_IWUSR|S_IRUGO),
	dt2w_version_show, dt2w_version_dump);

static ssize_t vib_strength_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", vib_strength);
}

static ssize_t vib_strength_dump(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return ret;

	if (input < 0 || input > 90)
		input = 20;

	vib_strength = input;

	return count;
}

static DEVICE_ATTR(vib_strength, 0666,
	vib_strength_show, vib_strength_dump);

/*
 * INIT / EXIT stuff below here
 */
#ifdef ANDROID_TOUCH_DECLARED
extern struct kobject *android_touch_kobj;
#else
struct kobject *android_touch_kobj;
EXPORT_SYMBOL_GPL(android_touch_kobj);
#endif

// Some of my code for vibration tunable (starts here) :
/*
static struct kobject *vib_strength_kobj;'

static ssize_t vib_strength_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", vib_strength);
	return count;
}*/






static int __init doubletap2wake_init(void)
{
	int rc = 0;

	doubletap2wake_pwrdev = input_allocate_device();
	if (!doubletap2wake_pwrdev) {
		pr_err("Can't allocate suspend autotest power button\n");
		goto err_alloc_dev;
	}

	input_set_capability(doubletap2wake_pwrdev, EV_KEY, KEY_POWER);
	doubletap2wake_pwrdev->name = "dt2w_pwrkey";
	doubletap2wake_pwrdev->phys = "dt2w_pwrkey/input0";

	rc = input_register_device(doubletap2wake_pwrdev);
	if (rc) {
		pr_err("%s: input_register_device err=%d\n", __func__, rc);
		goto err_input_dev;
	}
	dt2w_input_wq = create_workqueue("dt2wiwq");
	if (!dt2w_input_wq) {
		pr_err("%s: Failed to create dt2wiwq workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&dt2w_input_work, dt2w_input_callback);
	rc = input_register_handler(&dt2w_input_handler);
	if (rc)
		pr_err("%s: Failed to register dt2w_input_handler\n", __func__);

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
	dt2w_lcd_notif.notifier_call = lcd_notifier_callback;
	if (lcd_register_client(&dt2w_lcd_notif) != 0) {
		pr_err("%s: Failed to register lcd callback\n", __func__);
	}
#else
	register_early_suspend(&dt2w_early_suspend_handler);
#endif
#endif

#ifndef ANDROID_TOUCH_DECLARED
	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) {
		pr_warn("%s: android_touch_kobj create_and_add failed\n", __func__);
	}
#endif
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake_version.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake_version\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_vib_strength.attr);
	if (rc)
		pr_warn("%s: sysfs_create_file failed for vib_strength\n", __func__);

err_input_dev:
	input_free_device(doubletap2wake_pwrdev);
err_alloc_dev:
	pr_info(LOGTAG"%s done\n", __func__);
	return 0;
}

static void __exit doubletap2wake_exit(void)
{
#ifndef ANDROID_TOUCH_DECLARED
	kobject_del(android_touch_kobj);
#endif
#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
	lcd_unregister_client(&dt2w_lcd_notif);
#endif
#endif
	input_unregister_handler(&dt2w_input_handler);
	destroy_workqueue(dt2w_input_wq);
	input_unregister_device(doubletap2wake_pwrdev);
	input_free_device(doubletap2wake_pwrdev);
	return;
}

module_init(doubletap2wake_init);
module_exit(doubletap2wake_exit);
