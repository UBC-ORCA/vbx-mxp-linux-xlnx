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
#include <linux/vmalloc.h>
MODULE_AUTHOR("Joel Vandergriendt");
MODULE_DESCRIPTION("Device to mmap CMA");
MODULE_LICENSE("Proprietary");

#define DRIVER_NAME "CMA"
#define Driver_name "cma"
#define debug(var) printk(KERN_INFO "%s:%d  %s = %08X \n",__FILE__,__LINE__,#var,(unsigned)(var))
//file operation functions
static int cma_open(struct inode *, struct file *);
static int cma_mmap(struct file *, struct vm_area_struct *);

static struct file_operations cma_fops = {
	.owner = THIS_MODULE,
	.open = cma_open,
	.mmap = cma_mmap,
};

//static variables
static dev_t dev_no;
static struct cdev *cma_cdev = NULL;
static struct class *class_cma;
static struct device *dev_cma;
static int __init cma_init(void)
{
    int err;
    printk(KERN_INFO "\n\n\n\n\n\n\nCMA INIT\n");

    err=alloc_chrdev_region(&dev_no,0,1,DRIVER_NAME);
    if( err){
	    printk(KERN_ERR "Failed to allocate device number\n");
	    return err;
    }

    cma_cdev = cdev_alloc();
    if( !cma_cdev ){
	    printk(KERN_ERR "Failed to allocate cdev\n");
	    return 1;
    }
    cma_cdev->ops = & cma_fops;
    cma_cdev->owner = THIS_MODULE;
    err = cdev_add(cma_cdev,dev_no,1 );
    if (err < 0){
	    printk(KERN_ERR "Failed to add cdev\n");
	    return err;
    }

    class_cma = class_create(THIS_MODULE, DRIVER_NAME);
    if(IS_ERR( class_cma)){
	    printk(KERN_ERR "Failed to create class\n");
	    return 1;
    }
    dev_cma= device_create(class_cma,NULL,dev_no,NULL, Driver_name );
    if(IS_ERR( dev_cma)){
	    printk(KERN_ERR "failed to create device\n");
	    return 1;
    }

    //in order to use dma_alloc_coherent, we must set the mask
    //for valid dma bus addresses. The mxp can use all 32 bits,
    //so that is the mask. If the dma engine could only address
    //the bottom 2^24 addresses, then that would be the mask
    if((err=dma_set_coherent_mask(dev_cma,DMA_BIT_MASK(32)) )){
	    printk(KERN_ERR "FAILED set mask\n");
	    return err;
    }

    return 0;    // Non-zero return means that the module couldn't be loaded.
}
struct vm_private_data{
	void* virt;
	dma_addr_t dma_handle;
	size_t len;
};
static void vm_free(struct vm_area_struct * vma)
{
	struct vm_private_data* pdat = vma->vm_private_data;
	dma_free_coherent(dev_cma,pdat->len,pdat->virt,pdat->dma_handle);
	kfree(pdat);
}

static struct vm_operations_struct vm_ops ={
	.close = vm_free
};
static int cma_mmap(struct file * f, struct vm_area_struct *vma)
{
	int retval=0;
	int pfn;
	dma_addr_t dma_handle;
	void** kvirt;
	size_t len;
	struct vm_private_data* pdat;
	len =vma->vm_end - vma->vm_start;
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	//because this buffer is about to be used by userspace, it is
	//important for it to be sanitized, so use zalloc
	kvirt=dma_zalloc_coherent(dev_cma,len,&dma_handle,GFP_USER);
	if(kvirt == NULL ){
		printk(KERN_ERR
		       "cma_mmap - failed dma alloc\n");
		return -ENOMEM;
	}else{
		//since we have allocated memory, we should also free it eventually.
		//to do this we register a callback in the vma to be called on munmap
		pdat = kmalloc(sizeof(vm_private_data),GFP_KERNEL);
		if(pdat == NULL){
			//we have issues!
			dma_free_coherent(dev_cma,len,kvirt,dma_hadle);
			printk(KERN_ERR
			       "cma_mmap - failed dma alloc\n");
			return -ENOMEM;

		}
		pdat->virt = kvirt;
		pdat->dma_handle = dma_handle;
		pdat->len = len;

		vma->vm_private_data = pdat;
		vma->vm_ops = &vm_ops;
	}

	//The user needs the physical address for this buffer,
	//so return it in the first 4 bytes.
	//-- This is a pretty hacky solution
	kvirt[0]=(void*)dma_handle;

	//do the remap
	pfn = (dma_handle) >> PAGE_SHIFT;
	if( remap_pfn_range(vma, vma->vm_start, pfn, len,
	                    vma->vm_page_prot)) {
		printk(KERN_ERR
		       "cma_mmap - failed to map the instruction memory\n");
		retval = -EAGAIN;
	}

	return retval;
}
static int cma_open(struct inode *i, struct file *f){return 0;}
static void __exit cma_cleanup(void)
{
    printk(KERN_INFO "Cleaning up module.\n");
    cdev_del(cma_cdev);
    unregister_chrdev_region(dev_no, 1);
}

module_init(cma_init);
module_exit(cma_cleanup);
