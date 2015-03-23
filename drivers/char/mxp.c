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
#include <linux/stringify.h>
#include <linux/ctype.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of.h>

MODULE_AUTHOR("Joel Vandergriendt");
MODULE_DESCRIPTION("Vectorblox MXP Driver");
MODULE_LICENSE("GPL");

#define debug(var) printk(KERN_INFO "%s:%d  %s = %08X \n",__FILE__,__LINE__,#var,(unsigned)(var))
#define debugx(var) printk(KERN_INFO "%s:0x%X  %s = %08X \n",__FILE__,__LINE__,#var,(unsigned)(var))


//scratchpad

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


//static variables
static dev_t dev_no;
static struct cdev *mxp_cdev = NULL;
static struct class *class_mxp;
static struct device *dev_mxp;
static struct resource scratchpad_rsc;
static struct resource instr_port_rsc;


//space to store a maximum of 32 attributes
//each with a maximum name length of 32
#define ATTRIBUTE_NAME_LEN 32
#define MAX_ATTRIBUTES 320

struct mxp_device_attribute{
	struct device_attribute dev_attr;
	char name[ATTRIBUTE_NAME_LEN];
	int value;
};
static struct mxp_device_attribute mxp_attributes[MAX_ATTRIBUTES];

static ssize_t show_mxp_attr(struct device *dev, struct device_attribute *attr,char *buf)
{
	int i;
	int val=-1;
	for(i=0;i<PAGE_SIZE;i++){
		buf[i]=0;
	}
	for(i=0;i<MAX_ATTRIBUTES;i++){
		if(0==strncmp(mxp_attributes[i].name,attr->attr.name,ATTRIBUTE_NAME_LEN)){
			val=mxp_attributes[i].value;
			break;
		}
	}
	sprintf(buf,"0x%x",val);
	return strlen(buf)+1;
}
#define DT_PROP_PREFIX "vblx,"
static int mxp_of_probe(struct platform_device* pdev)
{
	int err;
	int rc = 0;
	int i=0;
	char c;
	struct device_node* dnode=pdev->dev.of_node;
	struct property* pp;
	printk(KERN_ERR "mxp_probe\n");

	//Parse device tree nodes
	rc = of_address_to_resource(dnode, 1, &scratchpad_rsc);
	if  (rc || !request_mem_region(scratchpad_rsc.start,
	                               resource_size(&scratchpad_rsc),
	                               Driver_name)) {
			printk(KERN_ERR "Failed to reserve scratchpad address range\n");
	}
	rc = of_address_to_resource(dnode, 0, &instr_port_rsc);

	if  (rc || !request_mem_region(instr_port_rsc.start,
	                               resource_size(&instr_port_rsc),
	                               Driver_name)) {
			printk(KERN_ERR "Failed to reserve instr_port address range\n");
	}
	printk(KERN_ERR "scratchpad (%p,%p) %x\n",
	       scratchpad_rsc.start,scratchpad_rsc.end,resource_size(&scratchpad_rsc));

	printk(KERN_ERR "instr_port (%p,%p) %x\n",
	       instr_port_rsc.start,instr_port_rsc.end,resource_size(&instr_port_rsc));

	//create character_device
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

	for_each_property_of_node(dnode, pp){
		const char* p_name=pp->name;
		char* cptr;
		int p_val=be32_to_cpup(pp->value);
		struct device_attribute *dev_attr=&mxp_attributes[i].dev_attr;
		printk(KERN_ERR "MXP PARAMETER %s=0x%x\n",p_name,p_val);
		//ignore the property if it doesn't begin with the DT_PROP_PREFIX
		if(strstr(p_name,DT_PROP_PREFIX)!=p_name)
			continue;
		//copy, then convert to uppercase, skip prefix
		cptr=p_name+sizeof(DT_PROP_PREFIX)-1;
		strncpy(mxp_attributes[i].name,cptr,ATTRIBUTE_NAME_LEN);
		cptr=mxp_attributes[i].name;

		do{
			*cptr=toupper(*cptr);
		}while(*cptr++);

		dev_attr->attr.name=mxp_attributes[i].name;
		dev_attr->attr.mode=0444;
		dev_attr->show=show_mxp_attr;
		dev_attr->store=NULL;

		mxp_attributes[i].value=p_val;
		device_create_file(dev_mxp,dev_attr);
		i++;
	}


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
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if(offset == 0 ){
		//map the instruction port
		printk(KERN_DEBUG "offset == 0 so mapping instruction port\n");
		if( size != PAGE_SIZE ){
			printk(KERN_ERR "Invalid size for mapping instruction port\n");
			retval = -EINVAL;
		}
		pfn = (instr_port_rsc.start) >> PAGE_SHIFT;

	}
	else if(offset ==SCRATCHPAD_MMAP_OFFSET){
		//map the scratchpad
		printk(KERN_DEBUG "offset == %d(PAGE_SIZE) so mapping scratchpad\n",(int)PAGE_SIZE);

		if(size != resource_size(&scratchpad_rsc)){
			//resize to be smaller ... I think this works (test it)
			printk(KERN_ERR "Invalid size for mapping scratchpad must be %d bytes\n",resource_size(&scratchpad_rsc));
			retval = -EINVAL;
		}
		pfn = (scratchpad_rsc.start) >> PAGE_SHIFT;
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
	}else{
		printk(KERN_INFO "mmap Success mapped virt=%p phys=%p len=%d\n",
		       vma->vm_start,pfn<<PAGE_SHIFT,size);
	}

	return retval;
}
static int mxp_open(struct inode *i, struct file *f)
{return 0;}
static int mxp_close(struct inode * i , struct file * f)
{return 0;}

static struct of_device_id vectorblox_of_match[]  = {
	{ .compatible = "vectorblox.com,vectorblox-mxp-1.0",},
	{}
};

MODULE_DEVICE_TABLE(of, vectorblox_of_match);

static struct platform_driver vectorblox_mxp_driver = {
	.probe=mxp_of_probe,
	.driver={
		.owner = THIS_MODULE,
		.name = "vectorblox-mxp",
		.of_match_table = vectorblox_of_match,
	},
};

static int __init mxp_init(void){
	printk(KERN_ERR "mxp_init\n");

	return platform_driver_register(&vectorblox_mxp_driver);
}

module_init(mxp_init);
module_exit(mxp_cleanup);
