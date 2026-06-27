/*
 * mem_rw.c — 内核模块: 跨进程内存读写 (Linux 6.6)
 * 
 * 创建 /dev/mem_rw 字符设备，通过 ioctl 实现跨进程读写
 * 使用 get_user_pages_remote + kmap_local_page (6.6 安全 API)
 *
 * 用法:
 *   insmod mem_rw.ko
 *   用户态: ioctl(fd, MEM_RW_READ, &req) / ioctl(fd, MEM_RW_WRITE, &req)
 *
 * 仅供学习研究
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
MODULE_AUTHOR("ImGuiOverlay");
MODULE_DESCRIPTION("Cross-process memory read/write via kernel module");

#define DEVICE_NAME "mem_rw"
#define CLASS_NAME  "mem_rw_class"

// ioctl 命令定义
#define MEM_RW_MAGIC 'M'
#define MEM_RW_READ  _IOWR(MEM_RW_MAGIC, 1, struct mem_rw_req)
#define MEM_RW_WRITE _IOW(MEM_RW_MAGIC, 2, struct mem_rw_req)

struct mem_rw_req {
    pid_t pid;          // 目标进程 PID
    uint64_t addr;      // 目标虚拟地址
    uint64_t size;      // 读写大小 (最大 4096)
    uint64_t data[512]; // 数据缓冲区 (4096 bytes)
};

static int major_number;
static struct class *mem_rw_class = NULL;
static struct device *mem_rw_device = NULL;

// 获取进程的 task_struct
static struct task_struct *get_task_by_pid(pid_t pid)
{
    struct task_struct *task;
    struct pid *pid_struct;

    pid_struct = find_get_pid(pid);
    if (!pid_struct)
        return NULL;

    task = get_pid_task(pid_struct, PIDTYPE_PID);
    put_pid(pid_struct);
    return task;
}

// 核心函数: 读取目标进程内存
// Linux 6.6: 使用 get_user_pages_remote + kmap_local_page
static int read_process_memory(struct task_struct *task, uint64_t addr,
                                void *buf, size_t size)
{
    struct mm_struct *mm;
    unsigned long uaddr = (unsigned long)addr;
    unsigned long offset;
    struct page *page;
    void *kaddr;
    int ret = -EFAULT;

    if (!task || !buf || size == 0 || size > 4096)
        return -EINVAL;

    mm = get_task_mm(task);
    if (!mm)
        return -ESRCH;

    mmap_read_lock(mm);

    // 检查地址是否在进程的 VMA 内 (安全检查)
    // 跳过检查也可，但加一层保护
    // struct vm_area_struct *vma = find_vma(mm, uaddr);
    // if (!vma || uaddr < vma->vm_start) {
    //     mmap_read_unlock(mm);
    //     mmput(mm);
    //     return -EFAULT;
    // }

    // pin 用户页 (等同于 get_user_pages_remote)
    // FOLL_FORCE: 绕过只读保护（读操作其实不需要）
    ret = pin_user_pages_remote(mm, uaddr, 1,
                                 FOLL_FORCE | FOLL_WRITE,  // 读也加 FOLL_FORCE 确保拿得到
                                 &page, NULL, NULL);
    mmap_read_unlock(mm);
    mmput(mm);

    if (ret != 1)
        return -EFAULT;

    offset = uaddr & ~PAGE_MASK;
    kaddr = kmap_local_page(page);

    // 复制数据
    memcpy(buf, kaddr + offset, size);

    kunmap_local(kaddr);
    set_page_dirty_lock(page);
    put_page(page);

    return (int)size;
}

// 核心函数: 写入目标进程内存
static int write_process_memory(struct task_struct *task, uint64_t addr,
                                 const void *buf, size_t size)
{
    struct mm_struct *mm;
    unsigned long uaddr = (unsigned long)addr;
    unsigned long offset;
    struct page *page;
    void *kaddr;
    int ret;

    if (!task || !buf || size == 0 || size > 4096)
        return -EINVAL;

    mm = get_task_mm(task);
    if (!mm)
        return -ESRCH;

    mmap_read_lock(mm);
    ret = pin_user_pages_remote(mm, uaddr, 1,
                                 FOLL_FORCE | FOLL_WRITE,
                                 &page, NULL, NULL);
    mmap_read_unlock(mm);
    mmput(mm);

    if (ret != 1)
        return -EFAULT;

    offset = uaddr & ~PAGE_MASK;
    kaddr = kmap_local_page(page);

    memcpy(kaddr + offset, buf, size);

    kunmap_local(kaddr);
    set_page_dirty_lock(page);
    put_page(page);

    return (int)size;
}

// ioctl 处理
static long mem_rw_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct mem_rw_req req;
    struct task_struct *task;
    int ret;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
        return -EFAULT;

    if (req.size == 0 || req.size > 4096)
        return -EINVAL;

    task = get_task_by_pid(req.pid);
    if (!task)
        return -ESRCH;

    switch (cmd) {
    case MEM_RW_READ:
        ret = read_process_memory(task, req.addr, req.data, req.size);
        if (ret > 0) {
            // 回传数据给用户态
            if (copy_to_user((void __user *)arg, &req, sizeof(req)))
                ret = -EFAULT;
        }
        break;
    case MEM_RW_WRITE:
        ret = write_process_memory(task, req.addr, req.data, req.size);
        break;
    default:
        ret = -ENOTTY;
    }

    put_task_struct(task);
    return ret;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = mem_rw_ioctl,
};

static int __init mem_rw_init(void)
{
    printk(KERN_INFO "mem_rw: 初始化内核模块\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "mem_rw: 注册字符设备失败\n");
        return major_number;
    }

    mem_rw_class = class_create(CLASS_NAME);
    if (IS_ERR(mem_rw_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(mem_rw_class);
    }

    mem_rw_device = device_create(mem_rw_class, NULL,
                                   MKDEV(major_number, 0),
                                   NULL, DEVICE_NAME);
    if (IS_ERR(mem_rw_device)) {
        class_destroy(mem_rw_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(mem_rw_device);
    }

    printk(KERN_INFO "mem_rw: 设备 /dev/%s 已创建 (major=%d)\n",
           DEVICE_NAME, major_number);
    return 0;
}

static void __exit mem_rw_exit(void)
{
    device_destroy(mem_rw_class, MKDEV(major_number, 0));
    class_destroy(mem_rw_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "mem_rw: 模块已卸载\n");
}

module_init(mem_rw_init);
module_exit(mem_rw_exit);
