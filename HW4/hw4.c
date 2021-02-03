/*
 * Abram Fouts
 * 5/16/2020
 * ECE 373
 *
 * Homework 4 : Blinking LED on a timer
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/timekeeping.h>
#include <linux/timer.h>


#define DEVCNT 		1
#define DEVNAME 	"Abram_Fouts_HW4"
#define LEDOFFSET 	0x00000E00
#define LEDOFF		0xE
#define LEDON		0xF

char driverName[] = DEVNAME;

static struct mydev_dev {
	struct cdev my_cdev;
	struct class *class;
	dev_t mydev_node;
    int syscall_val;
	__u32 myLED_reg;
	__u32 led_initial;
	bool ledState;
} mydev;

//static dev_t mydev_node;

//PCI struct def
static struct mypci_pci {
	struct pci_dev *pdev;
	void *hw_addr;

} mypci_pci;

//PCI ID TABLE
static const struct pci_device_id pci_ID[] = {
	{PCI_DEVICE(0x8086, 0x100e)},
	{},
};

static int blink_rate = 2;
module_param(blink_rate, int, S_IRUSR | S_IWUSR);	//Parameter

struct timer_list blink_t;		//Timer

//////////////////////////////////////////////////////////////////////////////
// Blink Function added for HW4
void blink(struct timer_list *list) {
	//mydev.syscall_val is the integer that stores the parameter and is written to

	if (mydev.ledState){									//If blink is on
		mydev.ledState = 0;									//Turn it off
		iowrite32(LEDOFF, (mypci_pci.hw_addr + LEDOFFSET));	//Write that shit

	} else {
		mydev.ledState = 1;									//Turn it off
		iowrite32(LEDON, (mypci_pci.hw_addr + LEDOFFSET));	//Write that shit
	}
	
	if(mydev.syscall_val < 0) {							//Else turn it on, is blink rate 0?
		printk(KERN_ERR "EINVAL\n");
		mod_timer(&blink_t, (HZ/2) + jiffies);		//Arm with the blink rate duty cycle

	} else if (mydev.syscall_val == 0) {
		mod_timer(&blink_t, (HZ/2) + jiffies);		//Arm with the blink rate duty cycle

	} else {
		mod_timer(&blink_t, (HZ/mydev.syscall_val) + jiffies);		//Arm with the blink rate duty cycle
	}
}

//////////////////////////////////////////////////////////////////////////////
// Open
static int LED_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "successfully opened!\n");

	//mydev.sys_int = 23;
    //user parameter assigns syscall_val
    mydev.syscall_val = blink_rate;
	
	if (mydev.syscall_val == 0) {
		mod_timer(&blink_t, (HZ/2) + jiffies);
	} else {
		mod_timer(&blink_t, (HZ/mydev.syscall_val) + jiffies);
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Write
static ssize_t LED_read(struct file *file, char __user *buf,
                             size_t len, loff_t *offset)
{
	/* Get a local kernel buffer set aside */
	int ret;
    printk(KERN_INFO "We are in LED_read\n");

	//Get the LED REG value, 0xE00 from part 1 of HW3
	mydev.myLED_reg = ioread32(mypci_pci.hw_addr + LEDOFFSET);	//Mask all bits except the ones to change

	if (*offset >= sizeof(int))
		return 0;

	/* Make sure our user wasn't bad... */
	if (!buf) {
		ret = -EINVAL;
		goto out;
	}

	if (copy_to_user(buf, &mydev.syscall_val, sizeof(int))) {
		ret = -EFAULT;
		goto out;
	}
	ret = sizeof(int);
	*offset += sizeof(int);

	/* Good to go, so printk the thingy */
    //Replaced example 5 sys_int with syscall_val
	printk(KERN_INFO "User got from us %d\n", mydev.syscall_val);	//Where the parameter is written to

out:
	return ret;
}

//////////////////////////////////////////////////////////////////////////////
//Write
static ssize_t LED_write(struct file *file, const char __user *buf,
                              size_t len, loff_t *offset)
{
	/* Have local kernel memory ready */
	char *kern_buf;
	int ret;

    printk(KERN_INFO "We are in LED_write\n");

	/* Make sure our user isn't bad... */
	if (!buf) {
		ret = -EINVAL;
		goto out;
	}

	/* Get some memory to copy into... */
	kern_buf = kmalloc(len, GFP_KERNEL);

	/* ...and make sure it's good to go */
	if (!kern_buf) {
		ret = -ENOMEM;
		goto out;
	}

	/* Copy from the user-provided buffer */
	if (copy_from_user(kern_buf, buf, len)) {
		/* uh-oh... */
		ret = -EFAULT;
		goto mem_out;
	}

	ret = len;

	//Verify the value is not negative, 
	if (*kern_buf >= 0)
		mydev.syscall_val = *kern_buf;
	else {
		printk(KERN_INFO "ERROR: EINVAL\n");
		return -EINVAL; //BAD INPUT
	}
	/* print what userspace gave us */
	printk(KERN_INFO "Userspace wrote \"%d\" to us\n", *kern_buf);	//May need to cast
	
	//printk(KERN_INFO "Userspace wrote \"%s\" to us\n", kern_buf);

	//sleep for two seconds or 2000 m seconds
	msleep(2000);

mem_out:
	kfree(kern_buf);
out:
	return ret;
}

//////////////////////////////////////////////////////////////////////////////
//Let's get probing like we're ET
static int pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent) {
	
	resource_size_t mmio_start, mmio_len;
	unsigned long mask;

	printk(KERN_INFO "We are in PCI Probe\n");

	//select bar for pdev
	mask = pci_select_bars(pdev, IORESOURCE_MEM); //maybe not
	printk(KERN_INFO "The bar mask is %lx \n", mask);
	
	// Reserve
	if(pci_request_selected_regions(pdev, mask, driverName)) {
		printk(KERN_ERR "Failed to select region\n");
		pci_release_selected_regions(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
	}

	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);

	if (!(mypci_pci.hw_addr = ioremap(mmio_start, mmio_len))) {
		printk(KERN_INFO "I/O Remapping Failed\n");
		iounmap(mypci_pci.hw_addr);
		pci_release_selected_regions(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
	}

	pci_enable_device(pdev);

	//We have passed
	mydev.led_initial = ioread32(mypci_pci.hw_addr + LEDOFFSET);	//May need to change to add
	printk(KERN_INFO "LED_initial = %x\n", mydev.led_initial);

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
//Get this punk driver out of here -- remove
static void pci_remove(struct pci_dev *pdev){
	iounmap(mypci_pci.hw_addr);
	pci_release_selected_regions(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
	printk(KERN_INFO "PCI Driver Removed\n");
}

//////////////////////////////////////////////////////////////////////////////
/* File operations for our device */
static struct file_operations mydev_fops = {
	.owner = THIS_MODULE,
	.open = LED_open,
	.read = LED_read,
	.write = LED_write,
};

//////////////////////////////////////////////////////////////////////////////
//Set up the pci driver similar to file operations
static struct pci_driver mypci_driver = {
	.name = "LED Driver",
	.id_table = pci_ID,
	.probe = pci_probe,
	.remove = pci_remove,	
};

//////////////////////////////////////////////////////////////////////////////
// Init
static int __init LED_init(void)
{
	printk(KERN_INFO "%s module loading... syscall_val=%d\n", DEVNAME, mydev.syscall_val);

	if (alloc_chrdev_region(&mydev.mydev_node, 0, DEVCNT, DEVNAME)) {
		printk(KERN_ERR "alloc_chrdev_region() failed!\n");
		return -1;
	}

	printk(KERN_INFO "Allocated %d devices at major: %d\n", DEVCNT, MAJOR(mydev.mydev_node));

	/* Initialize the character device and add it to the kernel */
	cdev_init(&mydev.my_cdev, &mydev_fops);
	mydev.my_cdev.owner = THIS_MODULE;

	if (cdev_add(&mydev.my_cdev, mydev.mydev_node, DEVCNT)) {
		printk(KERN_ERR "cdev_add() failed!\n");

		/* clean up chrdev allocation */
		unregister_chrdev_region(mydev.mydev_node, DEVCNT);

		return -1;
	}

	//Do the PCI driver stuff here
	if (pci_register_driver(&mypci_driver)){
		printk(KERN_INFO "PCI reg failed\n");
		pci_unregister_driver(&mypci_driver);			//clean up
		unregister_chrdev_region(mydev.mydev_node, DEVCNT);	//clean up

		return -1;
	}

	//Mknod class
	if((mydev.class = class_create(THIS_MODULE, "ece_led")) == NULL){	//Class section
		printk(KERN_ERR "Failed to create class\n");
		printk(KERN_INFO "Failed to create class\n");
		class_destroy(mydev.class);
	}

	if(device_create(mydev.class, NULL, mydev.mydev_node, NULL, "ece_led") == NULL){
		printk(KERN_ERR "Failed to create device\n");
		printk(KERN_INFO "Failed to create device\n");
		device_destroy(mydev.class, mydev.mydev_node);
	}
	printk(KERN_INFO "Node created\n");
	
	//Timer setup
	timer_setup(&blink_t, blink, 0);
	printk(KERN_INFO "Timer setup\n");
	
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
//Exit
static void __exit LED_exit(void)
{

	//clean up device
	device_destroy(mydev.class, mydev.mydev_node);

	//Clean up class
	class_destroy(mydev.class);

	//Bye bye PCI
	pci_unregister_driver(&mypci_driver);	//clean up

	/* destroy the cdev */
	cdev_del(&mydev.my_cdev);

	/* clean up the devices */
	unregister_chrdev_region(mydev.mydev_node, DEVCNT);

	//delete timer
	del_timer_sync(&blink_t);

	printk(KERN_INFO "%s module unloaded!\n", DEVNAME);
}

MODULE_AUTHOR("Abram Fouts");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2");
module_init(LED_init);
module_exit(LED_exit);
