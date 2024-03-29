#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define BCM2708_PERI_BASE 0x3f000000
#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000)
#define GPIO_REGION_SIZE 0x3c
#define GPSET_OFFSET 0x1c
#define GPCLR_OFFSET 0x28
#define GPLEV_OFFSET 0x34

#define GPIO_DEFAULT 3
#define NUM_GPIOS 28

#define HIGH 1
#define LOW 0
#define MODULE_NAME "simple-led"
#define NUM_DEVICES 1
#define BUF_SIZE 2

/*
 * Device file operations
 */
static ssize_t
led_read(struct file *filep, char __user *buf, size_t len, loff_t *off);

static ssize_t
led_write(struct file *filep, const char __user *buf, size_t len, loff_t *off);

static int led_open(struct inode *inodep, struct file *filep);
static int led_release(struct inode *inodep, struct file *filep);

/*
 * Helper functions
 */
static void save_gpio_func_select(void);
static void restore_gpio_func_select(void);
static void pin_direction_output(void);
static void set_pin(void);
static void unset_pin(void);
static void read_pin(void);

static dev_t devt;
static struct cdev led_cdev;
static struct class *led_class;
static struct device *led_device;

/* 
 * iomap will point to the first GPIO register. Since this is a void pointer,
 * incrementing it will increment the address by one byte.
 * Strictly speaking, such arithmetic is not defined in the C standard; it is
 * allowed by GCC by means of an extension.
 * See: https://gcc.gnu.org/onlinedocs/gcc/Pointer-Arith.html
 */
static void __iomem *iomap;
static int func_select_reg_offset;
static int func_select_bit_offset;
static char pin_value[BUF_SIZE] = "";
static int func_select_initial_val;

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.release = led_release,
	.read = led_read,
	.write = led_write,
};

/* Only one process at a time will be allowed to work with the LED */
static DEFINE_MUTEX(led_mutex);

static int gpio_num = GPIO_DEFAULT;
module_param(gpio_num, int, S_IRUGO);
MODULE_PARM_DESC(gpio_num, "The gpio where the LED is connected (default = 6)");

static int __init init_led(void)
{
	iomap = ioremap(GPIO_BASE, GPIO_REGION_SIZE);

	func_select_reg_offset = 4 * (gpio_num / 10);
	func_select_bit_offset = (gpio_num % 10) * 3;

	save_gpio_func_select();
	pin_direction_output();

	mutex_init(&led_mutex);
	pr_info("%s: Module loaded\n", MODULE_NAME);
	goto out;

remap_err:
	device_destroy(led_class, devt);
dev_err:
	class_unregister(led_class);
	class_destroy(led_class);
class_err:
	cdev_del(&led_cdev);
cdev_err:
	unregister_chrdev_region(devt, NUM_DEVICES);
out:
	return ret;
}

static void __exit exit_led(void)
{
	unset_pin();
	restore_gpio_func_select();
	mutex_destroy(&led_mutex);
	iounmap(iomap);
	device_destroy(led_class, devt);
	class_unregister(led_class);
	class_destroy(led_class);
	cdev_del(&led_cdev);
	unregister_chrdev_region(devt, NUM_DEVICES);
	pr_info("%s: Module unloaded\n", MODULE_NAME);
}

static int led_open(struct inode *inodep, struct file *filep)
{
	mutex_lock(&led_mutex);
	return 0;
}

static int led_release(struct inode *inodep, struct file *filep)
{
	mutex_unlock(&led_mutex);
	return 0;
}

static ssize_t
led_read(struct file *filep, char __user *buf, size_t len, loff_t *off)
{
	//int err;

	if (!access_ok(VERIFY_WRITE, buf, len)) {
		pr_err("%s: Cannot access user buffer for writing\n",
			MODULE_NAME);
		return -EFAULT;
	}

	read_pin();
	pr_info("%s: led_read = %s\n", MODULE_NAME, pin_value);

	//err = copy_to_user(buf, pin_value, BUF_SIZE);
	// if (err)
	// 	return -EFAULT;

	return 0;
}

static ssize_t
led_write(struct file *filep, const char __user *buf, size_t len, loff_t *off)
{
	char kbuf[BUF_SIZE];
	//int err;

	//err = copy_from_user(kbuf, buf, BUF_SIZE);
	kbuf[1] = '\0';

	if (strcmp(kbuf, "0") == 0) {
		unset_pin();
	} else {
		set_pin();
	}

	return BUF_SIZE;
}

static void save_gpio_func_select(void)
{
	int val;

	val = ioread32(iomap + func_select_reg_offset);
	func_select_initial_val = (val >> func_select_bit_offset) & 7;
}

static void restore_gpio_func_select(void)
{
	int val;

	val = ioread32(iomap + func_select_reg_offset);
	val &= ~(7 << func_select_bit_offset);
	val |= func_select_initial_val << func_select_bit_offset;
	iowrite32(val, iomap + func_select_reg_offset);
}

static void pin_direction_output(void)
{
	int val;

	val = ioread32(iomap + func_select_reg_offset);
	val &= ~(6 << func_select_bit_offset);
	val |= 1 << func_select_bit_offset;
	iowrite32(val, iomap + func_select_reg_offset);
}

static void set_pin(void)
{
	iowrite32(1 << gpio_num, iomap + GPSET_OFFSET);
}

static void unset_pin(void)
{
	iowrite32(1 << gpio_num, iomap + GPCLR_OFFSET);
}

static void read_pin(void)
{
	int val;
	val = ioread32(iomap + GPLEV_OFFSET);
	val = (val >> gpio_num) & 1;
	pin_value[0] = val ? '1' : '0';
}

module_init(init_led);
module_exit(exit_led);

MODULE_AUTHOR("Filip Kolev");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("Very simple driver to control a LED using ioremap()");