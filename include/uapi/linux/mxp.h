//FROM xparameters.h
/******************************************************************/

#define XPAR_VECTORBLOX_MXP_NUM_INSTANCES 1

#define XPAR_VECTORBLOX_MXP_ARM_0_DEVICE_ID 0
#define XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_BASEADDR 0xB0000000
#define XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_HIGHADDR 0xB000FFFF
#define XPAR_VECTORBLOX_MXP_ARM_0_VECTOR_LANES 2
#define XPAR_VECTORBLOX_MXP_ARM_0_MAX_MASKED_WAVES 128
#define XPAR_VECTORBLOX_MXP_ARM_0_MASK_PARTITIONS 1
#define XPAR_VECTORBLOX_MXP_ARM_0_SCRATCHPAD_KB 64
#define XPAR_VECTORBLOX_MXP_ARM_0_M_AXI_DATA_WIDTH 64
#define XPAR_VECTORBLOX_MXP_ARM_0_MULFXP_WORD_FRACTION_BITS 16
#define XPAR_VECTORBLOX_MXP_ARM_0_MULFXP_HALF_FRACTION_BITS 15
#define XPAR_VECTORBLOX_MXP_ARM_0_MULFXP_BYTE_FRACTION_BITS 4
#define XPAR_VECTORBLOX_MXP_ARM_0_S_AXI_INSTR_BASEADDR 0x40000000
#define XPAR_VECTORBLOX_MXP_ARM_0_ENABLE_VCI 0
#define XPAR_VECTORBLOX_MXP_ARM_0_VCI_LANES 1


#define XPAR_VECTORBLOX_MXP_ARM_0_CLOCK_FREQ_HZ 100000000

struct vbx_mxp_t{
	void* scratchpad_addr;
	size_t scratchpae_size;
	size_t dma_alignement_bytes;
	size_t scratchpad_alignement_bytes;
	size_t vector_lanes;
	char vci_enabled;
	size_t vci_lanes;
	size_t mask_partitions;
	size_t max_masked_vector_length;
	uint8_t fxp_word_frac_bits;
	uint8_t fxp_half_frac_bits;
	uint8_t fxp_byte_frac_bits;
	void* instr_port_addr;
};

#define MXP_IOCTL_SP_BASE 1
#define MXP_IOCTL_SP_SIZE 2
#define MXP_IOCTL_SHARED_ALLOC 3
