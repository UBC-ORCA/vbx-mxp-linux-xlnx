#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>
#include <asm/uaccess.h>
#include <linux/mman.h>
#include <linux/mxp.h>
#include <linux/stringify.h>
MODULE_AUTHOR("Joel Vandergriendt");
MODULE_DESCRIPTION("Vectorblox MXP Driver");
MODULE_LICENSE("Proprietary");

#define debug(var) printk(KERN_INFO "%s:%d  %s = %08X \n",__FILE__,__LINE__,#var,(unsigned)(var))



//scratchpad
#define SCRATCHPAD_BASEADDR XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_BASEADDR
#define SCRATCHPAD_HIGHADDR XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_HIGHADDR
#define SCRATCHPAD_SIZE  XPAR_VECTORBLOX_MXP_ARM_0_SCRATCHPAD_KB *1024
//instruction port
#define INSTRUCTION_PORT  XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_INSTR_BASEADDR

#define SCRATCHPAD_MMAP_OFFSET  PAGE_SIZE




#define DRIVER_NAME "MXP"
#define Driver_name "mxp0"
//file operation functions
static int mxp_open(struct inode *, struct file *);
static int mxp_mmap(struct file *, struct vm_area_struct *);
static int mxp_close(struct inode *, struct file *);
static struct file_operations mxp_fops = {
	.owner = THIS_MODULE,
	.open = mxp_open,
	.release = mxp_close,
	.mmap = mxp_mmap,
};

//this magic creates static functions to read
//attributes, they also have to be registered in the __init
//function
static ssize_t store_fake(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){return 0;}
#define MXP_READ_ATTR(name)	  \
	static ssize_t show_mxp_##name(struct device *dev, struct device_attribute *attr,char *buf){ \
		strcpy(buf,__stringify(XPAR_VECTORBLOX_MXP_ARM_0_##name)); \
		return sizeof(__stringify(XPAR_VECTORBLOX_MXP_ARM_0_##name)); \
	} \
	static DEVICE_ATTR(name,0444,show_mxp_##name,store_fake)
MXP_READ_ATTR(DEVICE_ID);
MXP_READ_ATTR(S_AXI_BASEADDR);
MXP_READ_ATTR(S_AXI_HIGHADDR);
MXP_READ_ATTR(VECTOR_LANES);
MXP_READ_ATTR(MAX_MASKED_WAVES);
MXP_READ_ATTR(MASK_PARTITIONS);
MXP_READ_ATTR(SCRATCHPAD_KB);
MXP_READ_ATTR(M_AXI_DATA_WIDTH);
MXP_READ_ATTR(MULFXP_WORD_FRACTION_BITS);
MXP_READ_ATTR(MULFXP_HALF_FRACTION_BITS);
MXP_READ_ATTR(MULFXP_BYTE_FRACTION_BITS);
MXP_READ_ATTR(S_AXI_INSTR_BASEADDR);
MXP_READ_ATTR(ENABLE_VCI);
MXP_READ_ATTR(VCI_LANES);
MXP_READ_ATTR(CLOCK_FREQ_HZ);


//static variables
static dev_t dev_no;
static struct cdev *mxp_cdev = NULL;
static struct class *class_mxp;
static struct device *dev_mxp;
static int __init mxp_init(void)
{
    int err;
    err=alloc_chrdev_region(&dev_no,0,1,DRIVER_NAME);
    if( err){
	    printk(KERN_ERR "Failed to allocate device number\n");
	    return err;
    }

    mxp_cdev = cdev_alloc();
    if( !mxp_cdev ){
	    printk(KERN_ERR "Failed to allocate cdev\n");
	    return 1;
    }
    mxp_cdev->ops = & mxp_fops;
    mxp_cdev->owner = THIS_MODULE;
    err = cdev_add(mxp_cdev,dev_no,1 );
    if (err < 0){
	    printk(KERN_ERR "Failed to add cdev\n");
	    return err;
    }

    class_mxp = class_create(THIS_MODULE, DRIVER_NAME);
    if(IS_ERR( class_mxp)){
	    printk(KERN_ERR "Failed to create class\n");
	    return 1;
    }
    dev_mxp= device_create(class_mxp,NULL,dev_no,NULL, Driver_name );
    device_create_file(dev_mxp,&dev_attr_DEVICE_ID);
    device_create_file(dev_mxp,&dev_attr_S_AXI_BASEADDR);
    device_create_file(dev_mxp,&dev_attr_S_AXI_HIGHADDR);
    device_create_file(dev_mxp,&dev_attr_VECTOR_LANES);
    device_create_file(dev_mxp,&dev_attr_MAX_MASKED_WAVES);
    device_create_file(dev_mxp,&dev_attr_MASK_PARTITIONS);
    device_create_file(dev_mxp,&dev_attr_SCRATCHPAD_KB);
    device_create_file(dev_mxp,&dev_attr_M_AXI_DATA_WIDTH);
    device_create_file(dev_mxp,&dev_attr_MULFXP_WORD_FRACTION_BITS);
    device_create_file(dev_mxp,&dev_attr_MULFXP_HALF_FRACTION_BITS);
    device_create_file(dev_mxp,&dev_attr_MULFXP_BYTE_FRACTION_BITS);
    device_create_file(dev_mxp,&dev_attr_S_AXI_INSTR_BASEADDR);
    device_create_file(dev_mxp,&dev_attr_ENABLE_VCI);
    device_create_file(dev_mxp,&dev_attr_VCI_LANES);
    device_create_file(dev_mxp,&dev_attr_CLOCK_FREQ_HZ);

    if(IS_ERR( dev_mxp)){
	    printk(KERN_ERR "failed to create device\n");
	    return 1;
    }

    //in order to use dma_alloc_coherent, we must set the mask
    //for valid dma bus addresses. The mxp can use all 32 bits,
    //so that is the mask. If the dma engine could only address
    //the bottom 2^24 addresses, then that would be the mask
    if((err=dma_set_coherent_mask(dev_mxp,DMA_BIT_MASK(32)) )){
	    printk(KERN_ERR "FAILED set mask\n");
	    return err;
    }

    printk(KERN_INFO "MXP module Loaded\n");
    return 0;    // Non-zero return means that the module couldn't be loaded.
}

static void __exit mxp_cleanup(void)
{
    printk(KERN_INFO "Cleaning up module.\n");
    cdev_del(mxp_cdev);
    unregister_chrdev_region(dev_no, 1);
}

//map the instruction port if offset is zero,
//map the scratchpad if offset >= PAGE_SIZE
//otherwise throw an error
static int mxp_mmap(struct file * f, struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int retval=0;
	unsigned long size = vma->vm_end - vma->vm_start;
	int pfn;
	vma->vm_flags |= (VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	printk(KERN_DEBUG "mxp mmap");
	if(offset == 0 ){
		//map the instruction port
		printk(KERN_DEBUG "offset == 0 so mapping instruction port\n");
		if( size != PAGE_SIZE ){
			printk(KERN_ERR "Invalid size for mapping instruction port\n");
			retval = -EINVAL;
		}
		pfn = (INSTRUCTION_PORT) >> PAGE_SHIFT;
	}
	else if(offset >=SCRATCHPAD_MMAP_OFFSET){
		//map the scratchpad
		printk(KERN_DEBUG "offset > %d(PAGE_SIZE) so mapping scratchpad\n",(int)PAGE_SIZE);

		if(size> SCRATCHPAD_SIZE){
			//resize to be smaller ... I think this works (test it)
			printk(KERN_ERR "Invalid size for mapping scratchpad\n");
			retval = -EINVAL;
		}
		pfn = (SCRATCHPAD_BASEADDR) >> PAGE_SHIFT;
	}else{
		//rats, foiled again.
		printk(KERN_ERR "Invalid offset for mxp mmap\n");
		retval=-EINVAL;
	}

	if (retval==0 && io_remap_pfn_range(vma, vma->vm_start, pfn, size,
	                                    vma->vm_page_prot)) {
		printk(KERN_ERR
		       "mxp_mmap - failed\n");
		retval = -EAGAIN;
	}

	return retval;
}
static int mxp_open(struct inode *i, struct file *f)
{return 0;}
static int mxp_close(struct inode * i , struct file * f)
{return 0;}


module_init(mxp_init);
module_exit(mxp_cleanup);
