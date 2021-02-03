/*
 * Abram Fouts
 * 6/1/2020
 * ECE 373
 *
 * Homework 6: Final PCI driver
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
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define DEVCNT 		1
#define DEVNAME 	"Abram_Fouts_HW6"

//LED
#define LEDOFFSET 	0x00000E00
#define LED0ON      0xE     //E in bits 3:0
#define LED0OFF     0xF     //F in bits 3:0
#define LED1ON      0xE00   //E in bits 11:8
#define LED1OFF     0xF00   //F in bits 11:8

//Base Address
#define CTRL 0

//Interrupt/Descriptor stuff -- It's a shocker that these are 4 & 2 bit aligned, jk no it's not
//#define E1000_RX_DESC(R, i) (&(((struct e1000_rx_descriptor *)((R).RXDesc))[i]))

#define ICR 0x000C0 		//Read
#define ICS 0x000C8			//Cause Set
#define IMS 0x000D0			//Mask Set
#define IMC 0x000D8			//Mask Clear

#define DESC_COUNT	0x00000001

#define RCTL 	0x00100		//Receiver Control
#define RDBAL 	0x02800		//BA Lower
#define RDBAH	0x02804		//BA Upper
#define RDLEN 	0x02808		//Size / length
#define RDH		0x02810		//Head
#define RDL		0x02818		//Tail

char driverName[] = DEVNAME;

static struct mydev_dev {
	struct cdev 		my_cdev;
	struct class 		*class;
	dev_t 				mydev_node;
	//struct work_struct 	work;
    int 				syscall_val;
	__u32 				myLED_reg;
	__u32 				led_initial;
	bool 				ledState;
	//struct e1000_rx_ring *rx_ring;

} mydev;

//PCI struct def
static struct mypci_pci {
	struct pci_dev *pdev;
	void *hw_addr;
	struct work_struct 	work;
} mypci_pci;

//PCI ID TABLE
static const struct pci_device_id pci_ID[] = {
	{PCI_DEVICE(0x8086, 0x100e)},
	{},
};

//e1000 rx descr
struct e1000_rx_descriptor {
	__le64 buffer_addr;				//Address of descr
	union {
		__le32 data;
		struct {
			__le16 length;	
			__le16 css;				
		} flags;
	} lower;
	union {
		__le32 data;
		struct {
			__u8 	error;			
			__u8 	status;				
			__le16 	special;			
		} field;
	} upper;
};

struct e1000_rx_buffer {
	void *buf_mem;
	dma_addr_t dma_handle;
};

struct e1000_rx_ring {
	void 						*dma_mem;
	dma_addr_t 					dma_handle;			
	size_t						size;		
	uint32_t					head;		
	uint32_t					tail;	
	struct e1000_rx_buffer 		buffer[16];			//buffer
	struct e1000_rx_descriptor 	rx_desc_buf[16];	//descriptor	
} rx_ring;

//Parameter
static int blink_rate;

static void create_ring(struct pci_dev *pdev) {
	uint32_t ring_conf;
	uint32_t i;
	rx_ring.size = sizeof(struct e1000_rx_descriptor) * 16;	//16 locations for ring
	rx_ring.dma_mem = dma_alloc_coherent(&pdev->dev, rx_ring.size, &rx_ring.dma_handle, GFP_KERNEL);

	//Setup registers upper/lower
	ring_conf = (rx_ring.dma_handle >> 32) & 0xFFFFFFFF;
	iowrite32(ring_conf, mypci_pci.hw_addr + RDBAH);
	ring_conf = (rx_ring.dma_handle & 0xFFFFFFFF);
	iowrite32(ring_conf, mypci_pci.hw_addr + RDBAL);

	iowrite32(0, mypci_pci.hw_addr + RDH);		//Write 0 to head
	iowrite32(15, mypci_pci.hw_addr + RDL);		//Write 15 to tail

	iowrite32(rx_ring.size, mypci_pci.hw_addr + RDLEN);	//Size of reg setup

	//Allocates space for all data
	for(i = 0; i <16; i++){
		rx_ring.buffer[i].dma_handle = dma_map_single(&pdev->dev, rx_ring.buffer[i].buf_mem, 2048, DMA_FROM_DEVICE);
		rx_ring.rx_desc_buf[i].buffer_addr = cpu_to_le64(rx_ring.buffer[i].dma_handle);
	}

	printk(KERN_INFO "Ring created\n");


}

//////////////////////////////////////////////////////////////////////////////
// Interrupt stuff for HW6
static irqreturn_t IRQ_HANDLER(int irq, void *data){
	uint32_t value;

	iowrite32((LED0ON | LED1ON), mypci_pci.hw_addr + LEDOFFSET);
	
	schedule_work(&mypci_pci.work);

	value = ioread32(mypci_pci.hw_addr + ICR);
	printk(KERN_INFO "Interrupt by: 0x%08x\n", value);

	iowrite32(0x10, mypci_pci.hw_addr + IMS);	//enables interrupts
	
	return IRQ_HANDLED;
}

//////////////////////////////////////////////////////////////////////////////
// Blink Function added for HW4
static void mywork(struct work_struct *work){
	printk(KERN_INFO "Inside mywork struct\n");
	uint64_t value;

	rx_ring.head = ioread32(mypci_pci.hw_addr + RDH);
	rx_ring.tail = ioread32(mypci_pci.hw_addr + RDL); 

	msleep(500);		//sleep for 0.5 seconds

	value = (uint64_t)rx_ring.rx_desc_buf[rx_ring.head].buffer_addr;
	printk(KERN_INFO "Head: 0x%llx\n", value);
	value = (uint64_t)rx_ring.rx_desc_buf[rx_ring.tail].buffer_addr;
	printk(KERN_INFO "Tail: 0x%llx\n", value);

	if(rx_ring.tail == 15) {						//Is the tail at the end let it wrap
		iowrite32(0, mypci_pci.hw_addr + RDL);
	} else {
		iowrite32(rx_ring.tail + 1, mypci_pci.hw_addr + RDL);	//increment
	}

	rx_ring.tail = ioread32(mypci_pci.hw_addr + RDL); 			//update tail

	//LED = ioread32(mypci_pci.hw_addr + LEDOFFSET);
	iowrite32((LED1OFF | LED0OFF), mypci_pci.hw_addr + LEDOFFSET);	//Turn off two LED's
}

/*
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
	
	if(blink_rate < 0) {							//Else turn it on, is blink rate 0?
		printk(KERN_ERR "EINVAL\n");
		mod_timer(&blink_t, (HZ/2) + jiffies);		//Arm with the blink rate duty cycle

	} else if (blink_rate == 0) {
		mod_timer(&blink_t, (HZ/2) + jiffies);		//Arm with the blink rate duty cycle

	} else {
		mod_timer(&blink_t, (HZ/blink_rate) + jiffies);		//Arm with the blink rate duty cycle
	}
}
*/

//////////////////////////////////////////////////////////////////////////////
// Open
static int LED_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "successfully opened!\n");

	//mydev.sys_int = 23;
    //user parameter assigns syscall_val
    //mydev.syscall_val = blink_rate;
	
	/*
	if (blink_rate == 0) {
		mod_timer(&blink_t, (HZ/2) + jiffies);
	} else {
		mod_timer(&blink_t, (HZ/blink_rate) + jiffies);
	}
	*/

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Write
static ssize_t LED_read(struct file *file, char __user *buf,
                             size_t len, loff_t *offset)
{
	int ret;
	uint16_t head, tail;
	uint32_t temp;

	/* Get a local kernel buffer set aside */
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

    //Replaced example 5 sys_int with syscall_val
	//printk(KERN_INFO "User got from us %d\n", blink_rate);	//Where the parameter is written to

	head = ioread32(mypci_pci.hw_addr + RDH);	//read head 
	tail = ioread32(mypci_pci.hw_addr + RDL);	//read tail

	printk(KERN_INFO "The user is getting head: %d and tail: %d\n", head, tail);
	temp = tail;
	temp |= (head << 16);
	
	printk(KERN_INFO "The user is getting %d\n", head);

	if (copy_to_user(buf, &temp, sizeof(int))) {
		ret = -EFAULT;
		goto out;
	}
	ret = sizeof(int);
	*offset += sizeof(int);
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

	//DO NOT CARE ABOUT WHAT IS BELOW WE ARE NOT WRITING FROM USERSPACE

	//Verify the value is not negative, 
	if (*kern_buf >= 0)
		blink_rate = *kern_buf;
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
	int error;
	uint64_t mask;

	printk(KERN_INFO "We are in PCI Probe\n");

	error = pci_enable_device_mem(pdev);
		
	if(error)
		return error;

	//select bar for pdev
	mask = pci_select_bars(pdev, IORESOURCE_MEM); //maybe not
	printk(KERN_INFO "The bar mask is %llx \n", mask);
	
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

	pci_set_master(pdev);
	//pci_enable_device(pdev);

	//Setup DMA
	error = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));

	if (error) {
		dev_err(&pdev->dev, "DMA Failed: 0x%x\n", error);
		pci_disable_device(pdev);
		return error;
	}

	//CTRL & RES
	iowrite32((1 << 26), mypci_pci.hw_addr + CTRL);
	mdelay(1);	//Delay between writes like in 372
	iowrite32(0x1A41, mypci_pci.hw_addr + CTRL);
	mdelay(1);
	iowrite32(0x10, mypci_pci.hw_addr + IMS);
	mdelay(1);	
	
	
	create_ring(pdev);

	iowrite32(0x801A, mypci_pci.hw_addr + RCTL);	
	mdelay(1);

	INIT_WORK(&mypci_pci.work, mywork);
	
	error = request_irq(pdev->irq, IRQ_HANDLER, 0, "HW6IRQ", &mypci_pci);

	iowrite32((LED1ON | LED0ON), mypci_pci.hw_addr + LEDOFFSET);

	//We have passed
	mydev.led_initial = ioread32(mypci_pci.hw_addr + LEDOFFSET);
	printk(KERN_INFO "LED_initial = %x\n", mydev.led_initial);

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
//Get this punk driver out of here -- remove
static void pci_remove(struct pci_dev *pdev){
	//Free up DMA
	uint32_t i;
	cancel_work_sync(&mypci_pci.work);
	free_irq(pdev->irq, &mypci_pci);

	for (i = 0; i < 16; i++){
		dma_unmap_single(&pdev->dev, rx_ring.buffer[i].dma_handle, 2048, DMA_TO_DEVICE);
	}

	dma_free_coherent(&pdev->dev, rx_ring.size, rx_ring.dma_mem, rx_ring.dma_handle);

	iounmap(mypci_pci.hw_addr);
	pci_release_selected_regions(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
	//kfree(&mypci_pci);
	pci_disable_device(pdev);

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
	//timer_setup(&blink_t, blink, 0);
	//printk(KERN_INFO "Timer setup\n");

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
	//del_timer_sync(&blink_t);
	
	printk(KERN_INFO "%s module unloaded!\n", DEVNAME);
}

MODULE_AUTHOR("Abram Fouts");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2");
module_init(LED_init);
module_exit(LED_exit);
