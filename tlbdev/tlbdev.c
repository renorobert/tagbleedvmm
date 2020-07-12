#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include "profile.h"

MODULE_LICENSE("Dual MPL/GPL");

#define DEVICE_NAME     "tlbdev"

static struct cdev cdev;
static int dev_major;
static struct class *chardev;

static long device_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations fops = {
        .unlocked_ioctl = device_ioctl
};

static int dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
        add_uevent_var(env, "DEVMODE=%#o", 0666);
        return 0;
}

// based on revanc
uint64_t profile_access_vmexit(void)
{
	uint64_t past, now;

	data_barrier();
	/* IRET */
	sync_core();	
	past = rdtscp();
	data_barrier();

	/* VMEXIT */
	wrmsrl(MSR_IA32_TSC_DEADLINE);	

	data_barrier();
	now = rdtscp();
	code_barrier();
	data_barrier();

	return now - past;
}

static void evict_l1_tlb_set(size_t set)
{
	size_t index, i;
	volatile char *eviction, *p = tlb_cache.dtlb;

	for (i = 0; i < 4; ++i) {
		index = (set + (i * 16)) << 12;
		eviction = (char *)((size_t) p | index);
		*eviction = 0x5A;
	}
}

static void evict_l1_tlb_all(void)
{
	size_t set;

	for (set = 0; set < 128; set++) {
		evict_l1_tlb_set(set);
	}
}

static void evict_l2_tlb_set(size_t set)
{
	size_t index, i;
	volatile char *eviction, *p = tlb_cache.stlb;

	for (i = 0; i < 4; ++i) {
		index = (set + (i * 128)) << 12;
		eviction = (char *)((size_t) p | index);
		*eviction = 0x5A;
	}
}

static long tlb_eviction(void *arg)
{
	size_t x, set, round, timing;
	size_t nrounds = 1000;

	for (set = 0; set < 128; set++) {
		for (round = 0; round < nrounds; round++) {
			for (x = 0; x < 16; x++) 
				wrmsrl(MSR_IA32_TSC_DEADLINE);
			evict_l1_tlb_all();
			evict_l2_tlb_set(set);
			timing = profile_access_vmexit();
			trace_printk("%lu,%lu\n", set, timing);
		}
	}

	return 0;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        work_on_cpu(1, tlb_eviction, NULL);

        return 0;
}


char *align_page_address(char *address, size_t align)
{
	uint64_t target = (uint64_t) address;
	uint64_t aligned;

	aligned = target + (align - (target & (align - 1)));

	return (char *)aligned;
}

static int alloc_eviction_buffers(void)
{

	tlb_cache.dtlb_ptr = vmalloc(16 * 1024 * 1024);

	if (!tlb_cache.dtlb_ptr) return -ENOMEM;

	tlb_cache.stlb_ptr = vmalloc(16 * 1024 * 1024);

	if (!tlb_cache.stlb_ptr) return -ENOMEM;
	
	tlb_cache.dtlb = align_page_address(tlb_cache.dtlb_ptr, 0x400000);
	tlb_cache.stlb = align_page_address(tlb_cache.stlb_ptr, 0x400000);

	return 0;
}

static void free_eviction_buffers(void)
{
	vfree(tlb_cache.dtlb_ptr);
	vfree(tlb_cache.stlb_ptr);
}

static void init_device(void)
{
        int err;
        dev_t dev;

        err = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);

        dev_major = MAJOR(dev);

        chardev = class_create(THIS_MODULE, DEVICE_NAME);
        chardev->dev_uevent = dev_uevent;

        cdev_init(&cdev, &fops);
        cdev.owner = THIS_MODULE;

        cdev_add(&cdev, MKDEV(dev_major, 0), 1);

        device_create(chardev, NULL, MKDEV(dev_major, 0), NULL, DEVICE_NAME);
}

static int __init dev_init(void)
{
	init_device();

	alloc_eviction_buffers();

	return 0;
}

static void __exit dev_exit(void)
{
	device_destroy(chardev, MKDEV(dev_major, 0));
        class_unregister(chardev);
        class_destroy(chardev);
        unregister_chrdev_region(MKDEV(dev_major, 0), MINORMASK);

	free_eviction_buffers();
}

module_init(dev_init);
module_exit(dev_exit);

