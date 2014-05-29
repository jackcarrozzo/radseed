#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h> 
#include <linux/fs.h>   
#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/ioport.h>
#include <linux/interrupt.h> 
#include <linux/irq.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/jiffies.h>
#include <linux/kobject.h> 
#include <linux/sysfs.h> 
#include <linux/wait.h>
#include <asm/io.h> 
#include <asm/signal.h>
#include "radseed.h"

#define DEVNAME 							"radseed" 	// name in /proc/devices, modules
#define PROC_EVENTS_NAME 			"radevents" // /proc/radevents
#define PROC_EVENTTIME_NAME 	"radlast"		// /proc/radlast
#define SYSFS_GROUP_NAME 			"radseed"		// /sys/kernel/radseed/
#define SYSFS_EVENTS_NAME 		events			// /sys/kernel/radseed/events
#define SYSFS_EVENTTIME_NAME 	last				// /sys/kernel/radseed/last
#define SYSFS_TRIGGER_NAME 		trigger			// /sys/kernel/radseed/trigger

// PPT1 but allow addr to be passed as a param
// defaults:   addr   irq
//             0x378   7
//             0x278   2
//             0x3bc   5
unsigned long ppt_base=0x378;
int ppt_irq=7;
module_param(ppt_base, long, S_IRUGO);
module_param(ppt_irq, int, S_IRUGO);

// these two globals keep our stats
static unsigned long events=0;
static unsigned long eventtime=0;

// wrapper for registering an event
inline void handle_event() {
  events++; 
  eventtime=jiffies; // only using 32 bit here (nabs LSB for 64 bit archs)
}

irqreturn_t rad_interrupt(int irq, void *dev_id, struct pt_regs *regs) {
	add_interrupt_randomness(ppt_irq, IRQ_NONE); 
	handle_event();
	return IRQ_HANDLED;
}

static int proc_events_show(struct seq_file *m, void *v) {
  seq_printf(m, "%lu\n", events);
  return 0;
}

static int proc_eventtime_show(struct seq_file *m, void *v) {
  seq_printf(m, "%lu\n", eventtime);
  return 0;
}

static int proc_events_open(struct inode *inode, struct file *file) {
  return single_open(file, proc_events_show, NULL);
}

static int proc_eventtime_open(struct inode *inode, struct file *file) {
  return single_open(file, proc_eventtime_show, NULL);
}

static const struct file_operations proc_events_fops = {
  .owner = THIS_MODULE,
  .open = proc_events_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};

static const struct file_operations proc_eventtime_fops = {
  .owner = THIS_MODULE,
  .open = proc_eventtime_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};

static ssize_t sysfs_events_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
	return sprintf(buf,"%lu\n",events);
}

static ssize_t sysfs_eventtime_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
  return sprintf(buf,"%lu\n",eventtime);
}

static ssize_t sysfs_trigger_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
	handle_event();
  return sprintf(buf,"1\n");
}

// we use this for all sysfs entries
static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,
                           const char *buf, size_t count) {
	return count; //ignore writes (return success)
}

static struct kobj_attribute sysfs_events_attribute = 
	__ATTR(SYSFS_EVENTS_NAME, 0644, sysfs_events_show, sysfs_store);

static struct kobj_attribute sysfs_eventtime_attribute =
  __ATTR(SYSFS_EVENTTIME_NAME, 0644, sysfs_eventtime_show, sysfs_store);

static struct kobj_attribute sysfs_trigger_attribute =
  __ATTR(SYSFS_TRIGGER_NAME, 0644, sysfs_trigger_show, sysfs_store);

static struct attribute *attrs[] = {
	&sysfs_events_attribute.attr,
	&sysfs_eventtime_attribute.attr,
	&sysfs_trigger_attribute.attr,
	NULL
};

static struct attribute_group attr_group = { .attrs = attrs };
static struct kobject *radseed_kobj;

static inline void remove_proc_entries() {
	remove_proc_entry(PROC_EVENTS_NAME,NULL);
	remove_proc_entry(PROC_EVENTTIME_NAME,NULL);
}

static int __init radseed_init(void) {
	int ret;

	//TODO: the exception handling flow could be cleaner

	proc_create(PROC_EVENTS_NAME, 0, NULL, &proc_events_fops);
	proc_create(PROC_EVENTTIME_NAME, 0, NULL, &proc_eventtime_fops);

	radseed_kobj=kobject_create_and_add(SYSFS_GROUP_NAME, kernel_kobj);
	if (!radseed_kobj) {
		printk(KERN_INFO "%s failed to create kobject.", DEVNAME);
		remove_proc_entries();
		return -ENOMEM;
	}

	ret=sysfs_create_group(radseed_kobj,&attr_group);
	if (ret) {
		printk(KERN_ALERT "%s failed to create sysfs group: %d",DEVNAME,ret);
		kobject_put(radseed_kobj);
		remove_proc_entries();
		return ret;
	}

	ret=request_irq(ppt_irq,(irq_handler_t) rad_interrupt,IRQ_NONE,DEVNAME,NULL);
	if (ret) {
		printk(KERN_ALERT "%s: unable to get IRQ %d: %d\n",DEVNAME,ppt_irq,ret);
		kobject_put(radseed_kobj);
    remove_proc_entries();
    return ret;
	} 
	
	// enable ints on the port (ACK, pin 10)
	outb(0x10,ppt_base+2);
	
	return 0;
}

static void __exit radseed_exit(void) {
	// disable interrupts 
	outb(0x00,ppt_base+2);
	free_irq(ppt_irq,NULL);

	// clean up fs entries 
	remove_proc_entries();
	kobject_put(radseed_kobj); 
}

module_init(radseed_init);
module_exit(radseed_exit);
MODULE_AUTHOR ("Jack Carrozzo");
MODULE_LICENSE("Dual BSD/GPL");

