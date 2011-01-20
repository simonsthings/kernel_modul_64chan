#define KERNEL31

#include <config/modversions.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>

#include <mach/mcbsp.h>
#include <mach/dma.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Simon Vogt <simonsunimail@gmail.com>");

int mcbspID = 0; /*McBSP1 => id 0*/
module_param(mcbspID, int, 0);
MODULE_PARM_DESC(mcbspID, "The McBSP to use. Starts at 0.");

int wordlength = 32;
module_param(wordlength, int, 32);
MODULE_PARM_DESC(wordlength, "The word length in bits that the serial port should send in. Use 8,16 or 32 bits please.");

int finishDMAcycle = 0;
int cyclecounter = 0;
#define MAXCYCLES 50
u32 fullbuf[64*MAXCYCLES];

/* ACTIVE SETTINGS: See technical reference manual of OMAP3530 for these values. */
static struct omap_mcbsp_reg_cfg simon_regs = {
        .spcr2 = XINTM(3),
        .spcr1 = RINTM(3),
        .rcr2  = 0,
        .rcr1  = RFRLEN1(0) | RWDLEN1(OMAP_MCBSP_WORD_32),  // frame is 1 word. word is 32 bits.
        .xcr2  = 0,
        .xcr1  = XFRLEN1(0) | XWDLEN1(OMAP_MCBSP_WORD_32),
        .srgr1 = FWID(31) | CLKGDV(50),
        .srgr2 = GSYNC | 0/*CLKSM*/ | CLKSP  | FPER(250),// | FSGM, // see pages 129 to 131 of sprufd1.pdf
        .pcr0  = FSXM | 0/*FSRM*/ | CLKXM | CLKRM | FSXP | FSRP | CLKXP | CLKRP,
	.xccr = DXENDLY(1) | XDMAEN ,//| XDISABLE,
	.rccr = RFULL_CYCLE | RDMAEN,// | RDISABLE,
};

void omap_mcbsp_write(void __iomem *io_base, u16 reg, u32 val)
{
	if (cpu_class_is_omap1() || cpu_is_omap2420())
		__raw_writew((u16)val, io_base + reg);
	else
		__raw_writel(val, io_base + reg);
}

int omap_mcbsp_read(void __iomem *io_base, u16 reg)
{
	if (cpu_class_is_omap1() || cpu_is_omap2420())
		return __raw_readw(io_base + reg);
	else
		return __raw_readl(io_base + reg);
}

#define OMAP_MCBSP_READ(base, reg) \
			omap_mcbsp_read(base, OMAP_MCBSP_REG_##reg)
#define OMAP_MCBSP_WRITE(base, reg, val) \
			omap_mcbsp_write(base, OMAP_MCBSP_REG_##reg, val)

static void simon_omap_mcbsp_dump_reg(u8 id)
{
	struct omap_mcbsp *mcbsp;
	getMcBSPDevice(mcbspID,&mcbsp);

	dev_info(mcbsp->dev, "**** McBSP%d regs ****\n", mcbsp->id);
	dev_info(mcbsp->dev, "DRR2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, DRR2));
	dev_info(mcbsp->dev, "DRR:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, DRR));
	dev_info(mcbsp->dev, "DXR2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, DXR2));
	dev_info(mcbsp->dev, "DXR:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, DXR));
	dev_info(mcbsp->dev, "SPCR2: 0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SPCR2));
	dev_info(mcbsp->dev, "SPCR1: 0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SPCR1));
	dev_info(mcbsp->dev, "RCR2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, RCR2));
	dev_info(mcbsp->dev, "RCR1:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, RCR1));
	dev_info(mcbsp->dev, "XCR2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, XCR2));
	dev_info(mcbsp->dev, "XCR1:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, XCR1));
	dev_info(mcbsp->dev, "SRGR2: 0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SRGR2));
	dev_info(mcbsp->dev, "SRGR1: 0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SRGR1));
	dev_info(mcbsp->dev, "PCR0:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, PCR0));
	dev_info(mcbsp->dev, "***********************\n");
	dev_info(mcbsp->dev, "SYSCON:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, SYSCON));
	dev_info(mcbsp->dev, "THRSH1:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, THRSH1));
	dev_info(mcbsp->dev, "THRSH2:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, THRSH2));
	dev_info(mcbsp->dev, "IRQST:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, IRQST));
	dev_info(mcbsp->dev, "IRQEN:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, IRQEN));
	dev_info(mcbsp->dev, "WAKEUPEN:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, WAKEUPEN));
	dev_info(mcbsp->dev, "XCCR:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, XCCR));
	dev_info(mcbsp->dev, "RCCR:  0x%04x\n",
			OMAP_MCBSP_READ(mcbsp->io_base, RCCR));
	dev_info(mcbsp->dev, "***********************\n");
}


/* currently unused: */
static void simon_omap_mcbsp_rx_dma_end_callback(int lch, u16 ch_status, void *data)
{
	/* <*data> is NULL when initialised with omap_request_dma_chain function 
	 * instead of omap_request_dma. So don't use it! */

	printk(KERN_ALERT "The DMA Channels have completed their transfers as was requested. Last transfer's status is %d!\n",ch_status);
}

static void simon_omap_mcbsp_rx_dma_buf1_callback(int lch, u16 ch_status, void *data)
{
	char printtemp[500];
	struct omap_mcbsp *mcbsp_dma_rx;
	//int status=3; // dummy var
	int i;
	int bufbufsize = 64; //128 * 0.25; // number of array elements
	u32* bufbuf1 = *((u32**)data);
	
	//output something:
	//printk(KERN_ALERT "DMA Channel %d has completed its transfer of buffer 1 with status %d!\n",lch,ch_status);

	for (i = 0 ; i<bufbufsize; i++)
	{
		fullbuf[i + cyclecounter * bufbufsize] = bufbuf1[i];
	}

	cyclecounter++;

	if (cyclecounter >= MAXCYCLES)
	{
		// get mcbsp data structure:
		getMcBSPDevice(mcbspID,&mcbsp_dma_rx);

		dev_info(mcbsp_dma_rx->dev, "RX DMA callback : 0x%x\n",OMAP_MCBSP_READ(mcbsp_dma_rx->io_base, SPCR2));

		/* We can free the channels */
		omap_free_dma(lch);
		//mcbsp_dma_rx->dma_rx_lch = -1;

		// tell main thread to continue:
		complete(&mcbsp_dma_rx->rx_dma_completion);

		printk(KERN_ALERT "The first millions of %d values of the transferbuffer bufbuf after reception (in callback!) are: \n",bufbufsize);
		sprintf(printtemp, "receive: \n   ");
		for (i = 0 ; i<min(bufbufsize,1000000); i++)
		{
			sprintf(printtemp, "%s 0x%x,", printtemp,bufbuf1[i]);

			if ((i%16) == 15)
			{
				printk(KERN_ALERT "%s \n",printtemp);
				sprintf(printtemp, "   ");
			}
		}
		printk(KERN_ALERT " end. \n");

	}

}

/* currently unused: */
static void simon_omap_mcbsp_rx_dma_buf2_callback(int lch, u16 ch_status, void *data)
{
	//output something:
	printk(KERN_ALERT "DMA Channel %d has completed its transfer of buffer 2 with status %d!\n",lch,ch_status);
}

/* currently unused: */
static void simon_omap_mcbsp_rx_dma_buf3_callback(int lch, u16 ch_status, void *data)
{
	//output something:
	printk(KERN_ALERT "DMA Channel %d has completed its transfer of buffer 3 with status %d!\n",lch,ch_status);

	if (finishDMAcycle) 
	{
		printk(KERN_ALERT "A request to end the DMA transmission was received! Doing one last round...\n");
		//omap_set_dma_callback(lch,simon_omap_mcbsp_rx_dma_end_callback,data);
		omap_stop_dma(lch);
	}
}


/* currently unused: */
/* This function sets up a DMA ring consisting of 3 buffers and starts it. Currently not in use by adsInput.c */
int simon_omap_mcbsp_recv_buffers(unsigned int id, 
	dma_addr_t buffer1, 
	dma_addr_t buffer2, 
	dma_addr_t buffer3, 
	unsigned int length)
{
	struct omap_mcbsp *mcbsp;
//	int dma_tx_ch;
//	int src_port = 0;
//	int dest_port = 0;
	int deviceRequestlineForDmaChannelsync = 0;
	int status=0;
//	int mychain_id = 0;
	struct omap_dma_channel_params *dmaparams1;
	struct omap_dma_channel_params *dmaparams2;
	struct omap_dma_channel_params *dmaparams3;
	int buf1_dmachannel;
	int buf2_dmachannel;
	int buf3_dmachannel;

	dmaparams1 = kzalloc(sizeof(struct omap_dma_channel_params), GFP_KERNEL);
	if (!dmaparams1) {
		return -ENOMEM;
	}
	dmaparams2 = kzalloc(sizeof(struct omap_dma_channel_params), GFP_KERNEL);
	if (!dmaparams2) {
		return -ENOMEM;
	}
	dmaparams3 = kzalloc(sizeof(struct omap_dma_channel_params), GFP_KERNEL);
	if (!dmaparams3) {
		return -ENOMEM;
	}

	// get mcbsp data structure:
	getMcBSPDevice(mcbspID,&mcbsp);

	deviceRequestlineForDmaChannelsync = mcbsp->dma_rx_sync; // RX


	dmaparams1->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams1->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams1->frame_count=1;	/* number of frames in a element */

	dmaparams1->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams1->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams1->src_start=mcbsp->phys_base + OMAP_MCBSP_REG_DXR;		/* source address : physical */
	dmaparams1->src_ei=0;		/* source element index */
	dmaparams1->src_fi=0;		/* source frame index */

	dmaparams1->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams1->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams1->dst_start=buffer1;		/* source address : physical */
	dmaparams1->dst_ei=0;		/* source element index */
	dmaparams1->dst_fi=0;		/* source frame index */

	dmaparams1->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams1->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams1->src_or_dst_synch=0;	/* source synch(1) or destination synch(0) */

	dmaparams1->ie=0;			/* interrupt enabled */

	dmaparams1->read_prio=0;/* read priority */
	dmaparams1->write_prio=0;/* write priority */

	dmaparams1->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */



	dmaparams2->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams2->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams2->frame_count=1;	/* number of frames in a element */

	dmaparams2->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams2->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams2->src_start=mcbsp->phys_base + OMAP_MCBSP_REG_DXR;		/* source address : physical */
	dmaparams2->src_ei=0;		/* source element index */
	dmaparams2->src_fi=0;		/* source frame index */

	dmaparams2->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams2->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams2->dst_start=buffer2;		/* source address : physical */
	dmaparams2->dst_ei=0;		/* source element index */
	dmaparams2->dst_fi=0;		/* source frame index */

	dmaparams2->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams2->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams2->src_or_dst_synch=0;	/* source synch(1) or destination synch(0) */

	dmaparams2->ie=0;			/* interrupt enabled */

	dmaparams2->read_prio=0;/* read priority */
	dmaparams2->write_prio=0;/* write priority */

	dmaparams2->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */



	dmaparams3->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams3->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams3->frame_count=1;	/* number of frames in a element */

	dmaparams3->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams3->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams3->src_start=mcbsp->phys_base + OMAP_MCBSP_REG_DXR;		/* source address : physical */
	dmaparams3->src_ei=0;		/* source element index */
	dmaparams3->src_fi=0;		/* source frame index */

	dmaparams3->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams3->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams3->dst_start=buffer3;		/* source address : physical */
	dmaparams3->dst_ei=0;		/* source element index */
	dmaparams3->dst_fi=0;		/* source frame index */

	dmaparams3->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams3->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams3->src_or_dst_synch=0;	/* source synch(1) or destination synch(0) */

	dmaparams3->ie=0;			/* interrupt enabled */

	dmaparams3->read_prio=0;/* read priority */
	dmaparams3->write_prio=0;/* write priority */

	dmaparams3->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */


	// Not using the built-in DMA chaining APIs because they reduce functionality and it's not really more difficult to just do the chaining ourselves:
/*	status = omap_request_dma_chain(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_TX" for McBSP1 of (also) OMAP3530
				  "McBSP DMA chaining TX test!", // just some string for the log files
				  simon_omap_mcbsp_tx_dma_end_callback, // the callback function that will be called ewhen the DMA transfer has finished.
				  &mychain_id, // future handle for the chain
				  3, // number of channels this chain shall consist of
				  OMAP_DMA_STATIC_CHAIN, // the chain will not change during transfer (not quite sure about the benefitts of a dynamic chain yet)
				  *dmaparams1); // default params such as destinations...
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channels for chain in McBSP%d TX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d TX DMA on the chain with ID %d\n", mcbsp->id, mychain_id);
*/
	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_TX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 1 !",
				simon_omap_mcbsp_rx_dma_buf1_callback,
				&buffer1,
				&buf1_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf1_dmachannel);


	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_TX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 2 !",
				simon_omap_mcbsp_rx_dma_buf2_callback,
				&buffer2,
				&buf2_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf2_dmachannel);


	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_TX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 3 !",
				simon_omap_mcbsp_rx_dma_buf3_callback,
				&buffer3,
				&buf3_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf3_dmachannel);


	/* Set up each DMA channel with it's parameters! */
	omap_set_dma_params(buf1_dmachannel, dmaparams1);
	omap_set_dma_params(buf2_dmachannel, dmaparams2);
	omap_set_dma_params(buf3_dmachannel, dmaparams3);

	/* Linking! Cycle through the 3 buffers, making a ring DMA transfer! See screen-output.txt! */
	omap_dma_link_lch(buf1_dmachannel, buf2_dmachannel);
	omap_dma_link_lch(buf2_dmachannel, buf3_dmachannel);
	omap_dma_link_lch(buf3_dmachannel, buf1_dmachannel);

	omap_start_dma(buf1_dmachannel);

	return 0;
}


int simon_omap_mcbsp_recv_buffer(unsigned int id, dma_addr_t buffer1, u32* bufbuf1,
				unsigned int length)
{
	struct omap_mcbsp *mcbsp;
	//int dma_rx_ch;
	int src_port = 0;
	int dest_port = 0;
	int sync_dev = 0;
	int status = 3; // dummy value
	int deviceRequestlineForDmaChannelsync = 0;
	char printtemp[500];
	int bufbufsize = 64; //128 * 0.25; // number of array elements
	int i;

	struct omap_dma_channel_params *dmaparams1;
	int buf1_dmachannel;

	dmaparams1 = kzalloc(sizeof(struct omap_dma_channel_params), GFP_KERNEL);
	if (!dmaparams1) {
		return -ENOMEM;
	}

	getMcBSPDevice(mcbspID,&mcbsp);

	deviceRequestlineForDmaChannelsync = mcbsp->dma_rx_sync; // RX


	dmaparams1->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams1->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams1->frame_count=1;	/* number of frames in a element */

	dmaparams1->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams1->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams1->src_start=mcbsp->phys_base + OMAP_MCBSP_REG_DXR;		/* source address : physical */
	dmaparams1->src_ei=0;		/* source element index */
	dmaparams1->src_fi=0;		/* source frame index */

	dmaparams1->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams1->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams1->dst_start=buffer1;		/* source address : physical */
	dmaparams1->dst_ei=0;		/* source element index */
	dmaparams1->dst_fi=0;		/* source frame index */

	dmaparams1->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams1->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams1->src_or_dst_synch=1;	/* source synch(1) or destination synch(0) */

	dmaparams1->ie=0;			/* interrupt enabled */

	dmaparams1->read_prio=0;/* read priority */
	dmaparams1->write_prio=0;/* write priority */

	dmaparams1->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */

	/* chainable: */
	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_TX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 1 !",
				simon_omap_mcbsp_rx_dma_buf1_callback,
				&bufbuf1,
				&buf1_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf1_dmachannel);

	dev_info(mcbsp->dev, "McBSP%d RX DMA on channel %d\n", mcbsp->id,buf1_dmachannel);

	// initialise the inter-thread waiting funtionality:
	init_completion(&mcbsp->rx_dma_completion);

	sync_dev = mcbsp->dma_rx_sync;

	omap_set_dma_transfer_params(buf1_dmachannel,
					OMAP_DMA_DATA_TYPE_S32,
					length >> 1, 1,
					OMAP_DMA_SYNC_ELEMENT,
					sync_dev, dmaparams1->src_or_dst_synch);

	omap_set_dma_src_params(buf1_dmachannel,
				src_port,
				OMAP_DMA_AMODE_CONSTANT,
				mcbsp->phys_base + OMAP_MCBSP_REG_DRR,
				0, 0);

	omap_set_dma_dest_params(buf1_dmachannel,
					dest_port,
					OMAP_DMA_AMODE_POST_INC,
					buffer1,
					0, 0);

	/* Output initial buffer content: */
	printk(KERN_ALERT "The first millions of %d values of the transferbuffer bufbuf before reception (DMA setup function) are: \n",bufbufsize);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<min(bufbufsize,1000000); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,bufbuf1[i]);

		if ((i%16) == 15)
		{
			printk(KERN_ALERT "%s \n",printtemp);
			sprintf(printtemp, "   ");
		}
	}
	printk(KERN_ALERT " end. \n");

	/* Linking: Loop! */
	/* Set up this DMA channel to be linked to itself, thereby forming a one-buffer loop.
	 * Production should use at least two buffers so that one can be filled while the other is
	 * being read from. I just deactivated this (see e.g. simon_omap_mcbsp_recv_buffers() ) for testing. */
	omap_dma_link_lch(buf1_dmachannel, buf1_dmachannel);

	/* Begin the DMA transfers! The first 128 bytes may be wrong as the fifo buffer within the mcbsp port first has to be emptied. */
	omap_start_dma(buf1_dmachannel);

	/* Now wait until the DMA callback function has been called MAXCYCLES times. */
	wait_for_completion(&mcbsp->rx_dma_completion);

	/* Display a snapshot of the transfer buffer bufbuf1 after the transfer: */
	printk(KERN_ALERT "The first millions of %d values of the transferbuffer bufbuf after reception (DMA setup function) are: \n",bufbufsize);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<min(bufbufsize,1000000); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,bufbuf1[i]);

		if ((i%16) == 15)
		{
			printk(KERN_ALERT "%s \n",printtemp);
			sprintf(printtemp, "   ");
		}
	}
	printk(KERN_ALERT " end. \n");

	/* Display the complete data that has been received in those MAXCYCLES dma-transfers.
	 * We could have continued the transfer, but as we are just storing the data in memory, we want to look at it sooner or later! */
	printk(KERN_ALERT "The first millions of %d values of the transferbuffer fullbuf after reception (DMA setup function) are: \n",bufbufsize);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<(cyclecounter*bufbufsize); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,fullbuf[i]);

		if ((i%16) == 15)
		{
			printk(KERN_ALERT "%s \n",printtemp);
			sprintf(printtemp, "   ");
		}
	}
	printk(KERN_ALERT " end. \n");


	return 0;
}

int hello_init(void)
{
	int status = 3;
	int reqstatus = -4;
	u16 invalue16 = 0;
	//u32 value32 = 0;
	int returnstatus = 0;
	u32 outvalue = 0x5CCC333A; // 01011100110011000011001100111010
	u32 outvalue2 = 0x53CA; // 0101 00111100 1010
	int i;
	char printtemp[500];

	struct omap_mcbsp *mcbsp;

	/* Number of elements (values) in each DMA buffer.*/
	int bufbufsize = 64; //128 * 0.25; // number of array elements
	/* Number of bytes needed to store each value: */
	int bytesPerVal = 4; // number of bytes per array element (32bit = 4 bytes, 16bit = 2 bytes)
	/* The pointers to DMA buffers for local use: */
	u32* bufbuf1; // the DMA buffer for DMA channel 1
	u32* bufbuf2; // the DMA buffer for DMA channel 2
	u32* bufbuf3; // the DMA buffer for DMA channel 3

	/* The pointers to the same DMA buffers for use only by DMA controller: */
	dma_addr_t bufbufdmaaddr1;
	dma_addr_t bufbufdmaaddr2;
	dma_addr_t bufbufdmaaddr3;

	/* Allocate memory space for the three buffers and assign the 6 pointers: */
	bufbuf1 = dma_alloc_coherent(NULL, bufbufsize * bytesPerVal /*each u32 value has 4 bytes*/, &bufbufdmaaddr1, GFP_KERNEL);
	if (bufbuf1 == NULL) {pr_err("Unable to allocate DMA buffer 1\n");return -ENOMEM;}
	bufbuf2 = dma_alloc_coherent(NULL, bufbufsize * bytesPerVal /*each u32 value has 4 bytes*/, &bufbufdmaaddr2, GFP_KERNEL);
	if (bufbuf2 == NULL) {pr_err("Unable to allocate DMA buffer 2\n");return -ENOMEM;}
	bufbuf3 = dma_alloc_coherent(NULL, bufbufsize * bytesPerVal /*each u32 value has 4 bytes*/, &bufbufdmaaddr3, GFP_KERNEL);
	if (bufbuf3 == NULL) {pr_err("Unable to allocate DMA buffer 3\n");return -ENOMEM;}

	/* Write dummy values. These should be overwritten later! */
	memset(bufbuf1, 0xbf1A1111, bufbufsize * bytesPerVal );
	memset(bufbuf2, 0xbf1B1111, bufbufsize * bytesPerVal );
	memset(bufbuf3, 0xbf1C1111, bufbufsize * bytesPerVal );




	/* Do GPIO stuff */
	// requesting:
	reqstatus = gpio_request(134, "ADS1258EVM-clockselect");
	printk(KERN_ALERT "Gpio 134 (ADS1258EVM-clockselect) was requested. Return status: %d\n",reqstatus);
	reqstatus = gpio_request(135, "ADS1258EVM-nRESET");
	printk(KERN_ALERT "Gpio 135 (ADS1258EVM-nRESET) was requested. Return status: %d\n",reqstatus);
	reqstatus = gpio_request(136, "ADS1258EVM-nPWDN");
	printk(KERN_ALERT "Gpio 136 (ADS1258EVM-nPWDN) was requested. Return status: %d\n",reqstatus);
	reqstatus = gpio_request(183, "ADS1258EVM-analogPowerMode");
	printk(KERN_ALERT "Gpio 183 (ADS1258EVM-analogPowerMode) was requested. Return status: %d\n",reqstatus);
	// setting:
	status = gpio_direction_output(134,1);
	printk(KERN_ALERT "Setting gpio134 (ADS1258EVM-clockselect) as output, value 1=EXTERNAL clock from BB. Return status: %d\n",status);
	status = gpio_direction_output(135,1);
	printk(KERN_ALERT "Setting gpio135 (ADS1258EVM-nRESET) as output, value 1=no reset, so be active!. Return status: %d\n",status);
	status = gpio_direction_output(136,1);
	printk(KERN_ALERT "Setting gpio136 (ADS1258EVM-nPWDN) as output, value 1=no powerdown, so be active!. Return status: %d\n",status);
	status = gpio_direction_output(183,1);
	printk(KERN_ALERT "Setting gpio183 (ADS1258EVM-analogPowerMode) as output, value 1=unipolar. Return status: %d\n",status);
	/* End of GPIO stuff */



	printk(KERN_ALERT "Starting DMA-McBSP-receive!\n");

	/* Setting IO type */
	status = omap_mcbsp_set_io_type(mcbspID, OMAP_MCBSP_POLL_IO);  // POLL because we don't want to use IRQ and DMA will be set up when needed.
	/* requesting McBSP */	
	reqstatus = omap_mcbsp_request(mcbspID);
	printk(KERN_ALERT "Setting IO type was %d. Requesting McBSP %d returned: %d \n", status, (mcbspID+1), reqstatus);

	/* Continue if the mcbsp was available: */
	if (!reqstatus)
	{
		/* configure McBSP */
		omap_mcbsp_config(mcbspID, &simon_regs);
		printk(KERN_ALERT "Configured McBSP %d registers for raw mode..\n", (mcbspID+1));

		/* start McBSP */
		omap_mcbsp_start(mcbspID);	// kernel 2.6.31 and below have only one argument here!
		printk(KERN_ALERT "Started McBSP %d. \n", (mcbspID+1));

		// Define mcbsp variable: (The kernel file mcbsp.c was changed for this!)
		getMcBSPDevice(mcbspID,&mcbsp);
		printk(KERN_ALERT "### The McBSP base address is 0x%lx\n", mcbsp->phys_base);

		/* setting threshold of mcbsp port buffer (maximum is 128 32bit elements) */
		//simon_omap_mcbsp_dump_reg(mcbspID);
		//omap_mcbsp_set_rx_threshold(mcbspID, 64);
		//simon_omap_mcbsp_dump_reg(mcbspID);

		/* polled SPI mode operations */
		//printk(KERN_ALERT "Now reading data from McBSP %d in SPI mode... \n", (mcbspID+1));
		//status = omap_mcbsp_spi_master_recv_word_poll(mcbspID, &value32);
		//printk(KERN_ALERT "Reading from McBSP %d in SPI mode returned as status: %d and as value: 0x%04x \n", (mcbspID+1), status,value32);
		//printk(KERN_ALERT "Now writing data to McBSP %d in SPI mode... \n", (mcbspID+1));
		//status = omap_mcbsp_spi_master_xmit_word_poll(mcbspID, outvalue);
		//printk(KERN_ALERT "Writing to McBSP %d in SPI mode returned as status: %d \n", (mcbspID+1), status);

		/* polled mcbsp i/o operations */
		printk(KERN_ALERT "Now writing data to McBSP %d (raw & polled)... \n", (mcbspID+1));
		status = omap_mcbsp_pollwrite(mcbspID, outvalue);  // needs changes in mcbsp.c --> kernel patch & recompile
		printk(KERN_ALERT "Writing to McBSP %d (raw, polled mode) returned as status: %d \n", (mcbspID+1), status);
		printk(KERN_ALERT "Now writing 2nd data to McBSP %d (raw & polled)... \n", (mcbspID+1));
		status = omap_mcbsp_pollwrite(mcbspID, outvalue2);  // needs changes in mcbsp.c --> kernel patch & recompile
		printk(KERN_ALERT "Writing to McBSP %d (raw, polled mode) returned as status: %d \n", (mcbspID+1), status);

		printk(KERN_ALERT "Now reading data from McBSP %d (raw & polled)... \n", (mcbspID+1));
		status = omap_mcbsp_pollread(mcbspID, &invalue16);  // needs changes in mcbsp.c --> kernel patch & recompile
		printk(KERN_ALERT "Reading from McBSP %d (raw, polled mode) returned as status: %d and as value 0x%x \n", (mcbspID+1), status,invalue16);

		/* DMA */
		/* Set up, start, end, and diplay the dma transfer: */
		printk(KERN_ALERT "Now reading data from McBSP %d via DMA! \n", (mcbspID+1));
		status = simon_omap_mcbsp_recv_buffer(mcbspID, bufbufdmaaddr1,bufbuf1, bufbufsize * bytesPerVal/2 /* = elem_count in arch/arm/plat-omap/dma.c */); // the dma memory must have been allocated correctly. See above.
		printk(KERN_ALERT "Read from McBSP %d via DMA! Return status: %d \n", (mcbspID+1), status);

		/* Display the contents of the transfer buffer bufbuf1 after the transfer: */
		printk(KERN_ALERT "The first millions of %d values of the transferbuffer bufbuf after reception (init function) are: \n",bufbufsize);
		sprintf(printtemp, "receive: \n   ");
		for (i = 0 ; i<min(bufbufsize,1000000); i++)
		{
			sprintf(printtemp, "%s 0x%x,", printtemp,bufbuf1[i]);

			if ((i%16) == 15)
			{
				printk(KERN_ALERT "%s \n",printtemp);
				sprintf(printtemp, "   ");
			}
		}
		printk(KERN_ALERT " end. \n");

	}
	else 
	   printk(KERN_ALERT "Not attempting to continue because requesting failed.\n");

	return returnstatus;
}

void hello_exit(void)
{
	int i;

	printk(KERN_ALERT "Stopping DMAs...");
	//finishDMAcycle = 1; // only used with background transfers.
	// just count peas for a long time:
	for (i=0;i<10;i++){printk(KERN_ALERT ".");}
	printk(KERN_ALERT "DMAs hopefully stopped?...");

	printk(KERN_ALERT "Stopping McBSP %d...", (mcbspID+1));
	omap_mcbsp_stop(mcbspID);	// kernel 2.6.31 and below have only one argument here!
	printk(KERN_ALERT "Freeing McBSP %d...", (mcbspID+1));
	omap_mcbsp_free(mcbspID);
	printk(KERN_ALERT "done.\n");

	printk(KERN_ALERT "Goodbye, McBSP world\n");
}

module_init(hello_init);
module_exit(hello_exit);
