/*
 * syna_tcm.c — Synaptics TCM Touchscreen Firmware Driver
 *
 * 设备名伪装为触摸屏固件驱动，通过 ioctl 实现跨进程内存读写
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/pid.h>
#include <linux/highmem.h>
#include <linux/version.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Synaptics Inc.");
MODULE_DESCRIPTION("Synaptics TCM Touchscreen Firmware Driver");

#define DEVICE_NAME "sztch0"
#define CLASS_NAME  "syna_tcm"

// ioctl: 保持与 driver_rt.h 兼容 (0x800 init / 0x801 read / 0x802 write / 0x803 module_base)
#define OP_INIT 0x800
#define OP_READ 0x801
#define OP_WRITE 0x802
#define OP_MOD 0x803

struct COPY_MEMORY { pid_t pid; uintptr_t addr; void* buf; size_t size; };
struct MODULE_BASE  { pid_t pid; char name[256]; uintptr_t base; };

static int major_number;
static struct class *dev_class = NULL;
static struct device *dev_device = NULL;

static struct task_struct *get_task_by_pid(pid_t pid)
{
    struct pid *pid_struct = find_get_pid(pid);
    if (!pid_struct) return NULL;
    struct task_struct *task = get_pid_task(pid_struct, PIDTYPE_PID);
    put_pid(pid_struct);
    return task;
}

static int read_process_memory(struct task_struct *task, uint64_t addr,
                                void *buf, size_t size)
{
    if (!task || !buf || size == 0 || size > 4096) return -EINVAL;
    struct mm_struct *mm = get_task_mm(task);
    if (!mm) return -ESRCH;

    unsigned long uaddr = (unsigned long)addr;
    mmap_read_lock(mm);
    struct page *page;
    int ret = pin_user_pages_remote(mm, uaddr, 1, FOLL_FORCE | FOLL_WRITE, &page, NULL, NULL);
    mmap_read_unlock(mm);
    mmput(mm);
    if (ret != 1) return -EFAULT;

    unsigned long offset = uaddr & ~PAGE_MASK;
    void *kaddr = kmap_local_page(page);
    memcpy(buf, kaddr + offset, size);
    kunmap_local(kaddr);
    set_page_dirty_lock(page);
    put_page(page);
    return (int)size;
}

static int write_process_memory(struct task_struct *task, uint64_t addr,
                                 const void *buf, size_t size)
{
    if (!task || !buf || size == 0 || size > 4096) return -EINVAL;
    struct mm_struct *mm = get_task_mm(task);
    if (!mm) return -ESRCH;

    unsigned long uaddr = (unsigned long)addr;
    mmap_read_lock(mm);
    struct page *page;
    int ret = pin_user_pages_remote(mm, uaddr, 1, FOLL_FORCE | FOLL_WRITE, &page, NULL, NULL);
    mmap_read_unlock(mm);
    mmput(mm);
    if (ret != 1) return -EFAULT;

    unsigned long offset = uaddr & ~PAGE_MASK;
    void *kaddr = kmap_local_page(page);
    memcpy(kaddr + offset, buf, size);
    kunmap_local(kaddr);
    set_page_dirty_lock(page);
    put_page(page);
    return (int)size;
}

static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct COPY_MEMORY req;
    if (copy_from_user(&req, (void __user *)arg, sizeof(req))) return -EFAULT;
    if (req.size == 0 || req.size > 4096) return -EINVAL;

    struct task_struct *task = get_task_by_pid(req.pid);
    if (!task) return -ESRCH;

    int ret;
    char kbuf[4096];
    switch (cmd) {
    case OP_READ:
        ret = read_process_memory(task, req.addr, kbuf, req.size);
        if (ret > 0 && copy_to_user(req.buf, kbuf, req.size)) ret = -EFAULT;
        break;
    case OP_WRITE:
        if (copy_from_user(kbuf, req.buf, req.size)) { ret = -EFAULT; break; }
        ret = write_process_memory(task, req.addr, kbuf, req.size);
        break;
    default:
        ret = -ENOTTY;
    }
    put_task_struct(task);
    return ret;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = dev_ioctl,
};

static int __init dev_init(void)
{
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) return major_number;

    dev_class = class_create(CLASS_NAME);
    if (IS_ERR(dev_class)) { unregister_chrdev(major_number, DEVICE_NAME); return PTR_ERR(dev_class); }

    dev_device = device_create(dev_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(dev_device)) {
        class_destroy(dev_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(dev_device);
    }
    return 0;
}

static void __exit dev_exit(void)
{
    device_destroy(dev_class, MKDEV(major_number, 0));
    class_destroy(dev_class);
    unregister_chrdev(major_number, DEVICE_NAME);
}

module_init(dev_init);
module_exit(dev_exit);
