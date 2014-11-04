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
#include <linux/mxp.h>

MODULE_AUTHOR("Joel Vandergriendt");
MODULE_DESCRIPTION("Vectorblox MXP Driver");
MODULE_LICENSE("Proprietary");

#define debug(var) printk(KERN_INFO "%s:%d  %s = %d \n",__FILE__,__LINE__,#var,(unsigned)(var))



//scratchpad
#define SCRATCHPAD_BASEADDR XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_BASEADDR
#define SCRATCHPAD_HIGHADDR XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_HIGHADDR
//instruction port
#define INSTRUCTION_PORT  XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_INSTR_BASEADDR

#define SCRATCHPAD_MMAP_OFFSET  PAGE_SIZE




#define DRIVER_NAME "MXP"
#define Driver_name "mxp"
//file operation functions
static int mxp_open(struct inode *, struct file *);
static int mxp_mmap(struct file *, struct vm_area_struct *);
static long mxp_ioctl(struct file *, unsigned int, unsigned long);
static ssize_t mxp_read(struct file *, char __user *, size_t, loff_t *);

static struct file_operations mxp_fops = {
	.owner = THIS_MODULE,
	.open = mxp_open,
	.mmap = mxp_mmap,
	.read = mxp_read,
	.unlocked_ioctl = mxp_ioctl
};

//static variables
static dev_t dev_no;
static struct cdev *mxp_cdev = NULL;
static struct class *class_mxp;
static struct device *dev_mxp;
static int __init mxp_init(void)
{
    int err;
    printk(KERN_INFO "\n\n\n\n\n\n\nMXP INIT\n");

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



    printk(KERN_INFO "MXP Driver Loaded\n");
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


	if(offset == 0 ){
		//map the instruction port
		printk(KERN_DEBUG "offset == 0 so mapping instruction port\n");
		if( size > PAGE_SIZE ){
			printk(KERN_ERR "Invalid size for mapping instruction port\n");
			retval = -EINVAL;
		}
		pfn = (INSTRUCTION_PORT) >> PAGE_SHIFT;
	}
	else if(offset >=SCRATCHPAD_MMAP_OFFSET){
		//map the scratchpad
		printk(KERN_DEBUG "offset > %d(PAGE_SIZE) so mapping scratchpad\n",(int)PAGE_SIZE);
		pfn = (SCRATCHPAD_BASEADDR) >> PAGE_SHIFT;
	}else{
		//rats, foiled again.
		printk(KERN_ERR "Invalid offset for mxp mmap\n");
		retval=-EINVAL;
	}
	if (retval==0 && io_remap_pfn_range(vma, vma->vm_start, pfn, size,
	                                    vma->vm_page_prot)) {
		printk(KERN_ERR
		       "mxp_mmap - failed to map the instruction memory\n");
		retval = -EAGAIN;
	}
	return retval;
}
static int mxp_open(struct inode *i, struct file *f)
{
	//don't do anything on open.
	printk(KERN_INFO "device opened\n");
	return 0;
}
static ssize_t mxp_read(struct file * f, char __user * b, size_t s, loff_t * o)
{
	return 0;
}

#define PAD_SIZE_TO_PAGE(sz) ((((sz)+(PAGE_SIZE-1)) >> PAGE_SHIFT)<<PAGE_SHIFT)
struct shared_alloc_t{
	size_t len; /*in/output */
	void* phys; /*output*/
	void* virt; /*output*/
};
static long mxp_ioctl_shared_alloc(struct shared_alloc_t* param_ptr)
{
	struct shared_alloc_t param;
	dma_addr_t dma_handle;
	int* virt;
	size_t len;
	int retval;
	if(copy_from_user(&param,param_ptr,sizeof(param))){
		retval = -EACCES;
		goto err;
	}

	param.len=PAD_SIZE_TO_PAGE(param.len);

	//change this to not zero the memory... I think
	virt=dma_zalloc_coherent(dev_mxp,param.len,&dma_handle,GFP_USER);
	if(!virt){
		retval=-ENOMEM;
		goto err;
	}
	virt[0]=0xABCDEF;
	debug(virt[0]);
	param.virt=(void*)virt;
	param.phys=(void*)dma_handle;
	if(copy_to_user(param_ptr,&param,sizeof(param))){
		retval=-EACCES;
		goto err;
	}
	return 0;
 err:
	if(virt){
		dma_free_coherent(dev_mxp,len, virt, dma_handle);
	}
	printk(KERN_ERR "MXP_IOCTL_SHARED_ALLOC - failed\n");
	return retval;

}
static long mxp_ioctl(struct file * f, unsigned int cmd, unsigned long  param)
{
	/*int remap_pfn_range(struct vm_area_struct *vma,
	  unsigned long virt_addr, unsigned long pfn,
	  unsigned long size, pgprot_t prot);*/
	void* base_addr;
	switch(cmd){
	case MXP_IOCTL_SP_BASE:
		base_addr=(void*)SCRATCHPAD_BASEADDR;
		return copy_to_user((void**)param,&base_addr,sizeof(void*)) ?
			-EACCES:0;
	case MXP_IOCTL_SHARED_ALLOC:
		return mxp_ioctl_shared_alloc((struct shared_alloc_t*)param);
	default:
		return -EINVAL;
	}


}
module_init(mxp_init);
module_exit(mxp_cleanup);
