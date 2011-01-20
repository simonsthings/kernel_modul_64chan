#define KERNEL31

#include <config/modversions.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/time.h>
//#include <linux/ktime.h>
//#include <linux/utime.h>

#include <mach/mcbsp.h>
#include <mach/dma.h>


/* For socket etc */
#include <linux/net.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/socket.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Simon Vogt <simonsunimail@gmail.com>");

/* Number of elements (values) in each DMA buffer.*/
#define DMABUFSIZE 50*16 /*16 channels*/  // number of 32bit array elements
#define DMABUFBYTES (DMABUFSIZE*4) // each samples is a 32bit word, so 4 bytes.
#define UDPBUFFACTOR 30 // how many DMA buffers should be stored temporally until the UDP packet has been sent. Treat this as DMABUFSIZE x UDPBUFFACTOR 2D-array.
#define UDPBUFBYTES (DMABUFBYTES*UDPBUFFACTOR) // 1280 bytes = DMABUFSIZE * sizeof(int) * UDPBUFFACTOR 

/* Parameters */
int mcbspPort = 1; /*McBSP1 => id 0*/
module_param(mcbspPort, int, 1);
MODULE_PARM_DESC(mcbspPort, "The McBSP to use. Starts at 1.");

int targetIP = 0xC0A83701; //192.168.55.1
module_param(targetIP, int, 0);
MODULE_PARM_DESC(targetIP, "The target UDP IP address. Default is 192.168.55.1, or 0xC0A83701 hex.");

int targetPort = 49344;
module_param(targetPort, int, 0);
MODULE_PARM_DESC(targetPort, "The target port. Default is 49344.");

int packetlengthUDP = DMABUFSIZE * 10;
module_param(packetlengthUDP, int, 0);
MODULE_PARM_DESC(packetlengthUDP, "The packet data length (excl. 28 bytes header) for each UDP packet.");

/* Globals */
int mcbspID = 0; /*McBSP1 => id 0*/

int finishDMAcycle = 0;

/* Network UDP stuff: */
struct socket *network_socket; 
struct sockaddr_in *network_servaddr; 
struct msghdr *network_message; 
struct iovec  *network_iov; 
u32          *network_databuffer; // Treat this as DMABUFSIZE x UDPBUFFACTOR 2D-array of 32bit values.
int           network_dataoffset = 0; // stores the relative position at which the next write to the udp data array should occur.
int leaveXout = 100;
int leaveXoutCounter = 0;

/* old? */
int cyclecounter = 0;
#define MAXCYCLES 5
u32 fullbuf[DMABUFSIZE*MAXCYCLES];

/* Completion queues. Works similarly to 'synchronized' statement in java. */
//struct completion mcbsp_tx_dma_completion;
struct completion mcbsp_rx_dma_completion;

int buf1_dmachannel;
int buf2_dmachannel;
int buf3_dmachannel;
	
/* For measuring the packet transfer rate: */
struct timespec lasttime;
int snapshots = 20;
double *snapshotpacketrate;
int snapshotspacketcounter = 0;
double actualpacketrate = 0;
int callcounter = 0;

/*** Function Stubs! ***/
int send_network_message(struct msghdr *msg_header, char *databuffer,struct socket *sock);

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

static void computeCallbackRate(int lch)
{
	struct timespec *now;
	struct timespec timesincelastpacket;
	double secondsSinceLastPacket = 0;
	long nanossincelastpacket = 0;
	float onerate = 0;
	int i;

	now = kzalloc(sizeof(struct timespec), GFP_KERNEL);
	if (!now) {return ;}

	getnstimeofday(now);
	//now = current_kernel_time();
	timesincelastpacket = timespec_sub(*now,lasttime);
	lasttime = *now;

	nanossincelastpacket = nanossincelastpacket + timespec_to_ns(&timesincelastpacket);
	printk("dmachan %d: nanossincelastcallback=%li\n",lch,nanossincelastpacket);

	onerate = onerate + nanossincelastpacket;
	onerate = (1000.0*1000.0*1000.0)/onerate;

//	secondsSinceLastPacket = secondsSinceLastPacket + timesincelastpacket.tv_nsec;
//	secondsSinceLastPacket += timesincelastpacket.tv_sec; // + (timesincelastpacket.tv_nsec / (1000 * 1000 * 1000));
//	printk("secondsSinceLastPacket=%d\n",secondsSinceLastPacket);

//	printk("secs=%ld\n",timesincelastpacket.tv_sec);
//	printk("nanos=%ld\n",timesincelastpacket.tv_nsec);


//	*(snapshotpacketrate+(int)5) = (double) nanos;
//	printk("snap5=%lf",snapshotpacketrate[5]);

//	snapshotpacketrate[snapshotspacketcounter++] = onerate;
/*	if (snapshotspacketcounter == snapshots) 
	{
		snapshotspacketcounter = 0;
		actualpacketrate = 0;
		for (i = 0; i < snapshots;i++) // (double snap : snapshotpacketrate)
		{
			actualpacketrate = actualpacketrate + snapshotpacketrate[i];
		}
		actualpacketrate = actualpacketrate / snapshots;
		printk("The transmit rate of the last %d packets has been %lf Hz",snapshots,actualpacketrate);
	}
*/
	//static inline struct timespec timespec_sub(struct timespec lhs,struct timespec rhs)
	//s64 timespec_to_ns(const struct timespec *ts)
	//time_t mytime = time(0);
	//printk("time: %s\n", asctime(localtime(&mytime)));

}


/* currently unused: */
static void simon_omap_mcbsp_rx_dma_end_callback(int lch, u16 ch_status, void *data)
{
	/* <*data> is NULL when initialised with omap_request_dma_chain function 
	 * instead of omap_request_dma. So don't use it! */

	/* Stop the DMA transfers with this channel. Doesn't this mean that they will continue another round? */
	omap_stop_dma(lch);

	printk(KERN_ALERT "The DMA Channels have completed their transfers as was requested. Last transfer's status is %d!\n",ch_status);

	/* tell the thread executing the hello_exit() function to continue: */
	complete(&mcbsp_rx_dma_completion);
}

static void simon_omap_mcbsp_rx_dma_buf1_callback(int lch, u16 ch_status, void *data)
{
	char printtemp[500];
	struct omap_mcbsp *mcbsp_dma_rx;
	//int status=3; // dummy var
	int i;
	int c; // tempstore cyclecounter
	int runfinish = 0;
	u32* bufferkernel;
	//int oldmm;

	if (data == NULL)
	{
		printk(KERN_ALERT " Skipping callback because data is NULL. Check initialisation order? \n");
		return;
	}

	bufferkernel = (u32*)data;

	if (bufferkernel == NULL)
	{
		printk(KERN_ALERT " Skipping callback because bufferkernel is NULL. Check initialisation order? \n");
		return;
	}
	if (!*bufferkernel)
	{
		printk(KERN_ALERT " Skipping callback because *bufferkernel is NULL. Check initialisation order? \n");
		return;
	}

	cyclecounter++;
	c = cyclecounter;

	if (c > 1)
	{
		//for (i = 0 ; i<DMABUFSIZE; i++)
		//{
		//	fullbuf[i + c * DMABUFSIZE] = bufferkernel[i];
		//}

		// willingly loose data until we have a separate thread or so for this:
		//if (leaveXoutCounter++ >= leaveXout)
		//{
			send_network_message(network_message, (char*)bufferkernel, network_socket);
			//leaveXoutCounter=0;
			//computeCallbackRate();

		//}

		
		// increase offset for next dma buffer:
		network_dataoffset += DMABUFBYTES;
		// reset offset if total size of network_databuffer has been reached:
		if (network_dataoffset >= UDPBUFBYTES) network_dataoffset = 0;

printk(KERN_ALERT "Begin memcopy...\n");
		// fill the send buffer with the data from the DMA buffer. So that the dma buffer can now be reused again if the send doesn't take place in time!
		memcpy (network_databuffer+network_dataoffset,bufferkernel,DMABUFBYTES);
printk(KERN_ALERT "Finished memcopy. Begin send_network_message...\n");
		send_network_message(network_message, (char*)(network_databuffer+network_dataoffset), network_socket);
printk(KERN_ALERT "Finished send_network_message \n");

	}

	if (cyclecounter >= MAXCYCLES)
	{
		//printk(KERN_ALERT "Resetting cyclecounter!\n");
		cyclecounter = 2; // needs to be 2 or equivalent.
		//runfinish = 1;

	}

	callcounter++;
	if (callcounter == 1)	
	{
		computeCallbackRate(lch);
		callcounter = 0;
	}
	



	if (finishDMAcycle == 1) 
	{
		runfinish = 1;
	}

	if (runfinish) 
	{
		printk(KERN_ALERT "A request to end the DMA transmission was received! (c=%d) Finishing...\n",c);

		// get mcbsp data structure:
		getMcBSPDevice(mcbspID,&mcbsp_dma_rx);
		if (mcbsp_dma_rx == NULL) {pr_err("Unable to access McBSP device structure!\n");return;}

		dev_info(mcbsp_dma_rx->dev, "RX DMA callback : 0x%x\n",OMAP_MCBSP_READ(mcbsp_dma_rx->io_base, SPCR2));

		// We can stop the channels 
		omap_stop_dma(lch);

		// tell main thread to continue:
		complete(&mcbsp_dma_rx->rx_dma_completion);
		//complete(&mcbsp_rx_dma_completion);


		printk(KERN_ALERT "The first %d of %d values of the transferbuffer bufferkernel after reception (in callback %d!) are: \n",(20*16),DMABUFSIZE,c);
		sprintf(printtemp, "receive: \n %d: ",c);
		for (i = 0 ; i<min(DMABUFSIZE,(20*16)); i++)
		{
			sprintf(printtemp, "%s 0x%x,", printtemp,bufferkernel[i]);

			if ((i%16) == 15)
			{
				printk(KERN_ALERT "%s \n",printtemp);
				sprintf(printtemp, " %d: ",c);
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
		omap_set_dma_callback(lch,simon_omap_mcbsp_rx_dma_end_callback,data);
		//omap_stop_dma(lch);
	}
}


/* currently unused: */
/* This function sets up a DMA ring consisting of 3 buffers and starts it. Currently not in use by adsInput.c */
int simon_omap_mcbsp_recv_buffers(unsigned int id, 
	dma_addr_t buffer1dma, u32* buffer1kernel,
	dma_addr_t buffer2dma, u32* buffer2kernel,
	dma_addr_t buffer3dma, u32* buffer3kernel,
	unsigned int length)
{
	struct omap_mcbsp *mcbsp;
//	int dma_tx_ch;
//	int src_port = 0;
//	int dest_port = 0;
	int deviceRequestlineForDmaChannelsync = 0;
	int status=0;
//	int mychain_id = 0;
	char printtemp[500];
	int i;
	struct omap_dma_channel_params *dmaparams1;
	struct omap_dma_channel_params *dmaparams2;
	struct omap_dma_channel_params *dmaparams3;

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

	printk(KERN_ALERT "buffer1kernel[0]=%u\n",buffer1kernel[0]);
	printk(KERN_ALERT "buffer1kernel[1]=%u\n",buffer1kernel[1]);


	// get mcbsp data structure:
	getMcBSPDevice(mcbspID,&mcbsp);

	deviceRequestlineForDmaChannelsync = mcbsp->dma_rx_sync; // RX


	dmaparams1->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams1->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams1->frame_count=1;	/* number of frames in a element */

	dmaparams1->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams1->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams1->src_start=((mcbsp->phys_base) + OMAP_MCBSP_REG_DRR);		/* source address : physical */
	dmaparams1->src_ei=0;		/* source element index */
	dmaparams1->src_fi=0;		/* source frame index */

	dmaparams1->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams1->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams1->dst_start=buffer1dma;		/* source address : physical */
	dmaparams1->dst_ei=0;		/* source element index */
	dmaparams1->dst_fi=0;		/* source frame index */

	dmaparams1->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams1->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams1->src_or_dst_synch=1;	/* source synch(1) or destination synch(0) */

	dmaparams1->ie=0;			/* interrupt enabled */

	dmaparams1->read_prio=0;/* read priority */
	dmaparams1->write_prio=0;/* write priority */

	dmaparams1->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */



	dmaparams2->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams2->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams2->frame_count=1;	/* number of frames in a element */

	dmaparams2->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams2->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams2->src_start=((mcbsp->phys_base) + OMAP_MCBSP_REG_DRR);		/* source address : physical */
	dmaparams2->src_ei=0;		/* source element index */
	dmaparams2->src_fi=0;		/* source frame index */

	dmaparams2->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams2->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams2->dst_start=buffer2dma;		/* source address : physical */
	dmaparams2->dst_ei=0;		/* source element index */
	dmaparams2->dst_fi=0;		/* source frame index */

	dmaparams2->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams2->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams2->src_or_dst_synch=1;	/* source synch(1) or destination synch(0) */

	dmaparams2->ie=0;			/* interrupt enabled */

	dmaparams2->read_prio=0;/* read priority */
	dmaparams2->write_prio=0;/* write priority */

	dmaparams2->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */



	dmaparams3->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams3->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams3->frame_count=1;	/* number of frames in a element */

	dmaparams3->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams3->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams3->src_start=((mcbsp->phys_base) + OMAP_MCBSP_REG_DRR);		/* source address : physical */
	dmaparams3->src_ei=0;		/* source element index */
	dmaparams3->src_fi=0;		/* source frame index */

	dmaparams3->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams3->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams3->dst_start=buffer3dma;		/* source address : physical */
	dmaparams3->dst_ei=0;		/* source element index */
	dmaparams3->dst_fi=0;		/* source frame index */

	dmaparams3->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams3->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams3->src_or_dst_synch=1;	/* source synch(1) or destination synch(0) */

	dmaparams3->ie=0;			/* interrupt enabled */

	dmaparams3->read_prio=0;/* read priority */
	dmaparams3->write_prio=0;/* write priority */

	dmaparams3->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */

	/* Request DMA channel for buffer 1 */
	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_RX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 1 !",
				simon_omap_mcbsp_rx_dma_buf1_callback,
				&buffer1kernel,
				&buf1_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf1_dmachannel);


	/* Request DMA channel for buffer 2 */
	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_RX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 2 !",
				simon_omap_mcbsp_rx_dma_buf1_callback,
				&buffer2kernel,
				&buf2_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf2_dmachannel);


	/* Request DMA channel for buffer 3 */
	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_RX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 3 !",
				simon_omap_mcbsp_rx_dma_buf1_callback,
				&buffer3kernel,
				&buf3_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf3_dmachannel);


	/* Set up each DMA channel with it's parameters! */
	omap_set_dma_params(buf1_dmachannel, dmaparams1);
//	omap_set_dma_params(buf2_dmachannel, dmaparams2);
//	omap_set_dma_params(buf3_dmachannel, dmaparams3);

	/* Linking! Cycle through the 3 buffers, making a ring DMA transfer! See screen-output.txt! */
//	omap_dma_link_lch(buf1_dmachannel, buf2_dmachannel);
//	omap_dma_link_lch(buf2_dmachannel, buf3_dmachannel);
//	omap_dma_link_lch(buf3_dmachannel, buf1_dmachannel);

//	omap_start_dma(buf1_dmachannel);










	/* Output initial buffer content: */
	printk(KERN_ALERT "The first millions of %d values of the transferbuffer bufbuf before reception (DMA setup function) are: \n",DMABUFSIZE);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<min(DMABUFSIZE,1000000); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,buffer1kernel[i]);

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

	/* Display a snapshot of the transfer buffer buffer1kernel after the transfer: */
	printk(KERN_ALERT "The first millions of %d values of the transferbuffer bufbuf after reception (DMA setup function) are: \n",DMABUFSIZE);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<min(DMABUFSIZE,1000000); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,buffer1kernel[i]);

		if ((i%16) == 15)
		{
			printk(KERN_ALERT "%s \n",printtemp);
			sprintf(printtemp, "   ");
		}
	}
	printk(KERN_ALERT " end. \n");

	/* Display the complete data that has been received in those MAXCYCLES dma-transfers.
	 * We could have continued the transfer, but as we are just storing the data in memory, we want to look at it sooner or later! */
	printk(KERN_ALERT "The first %d of %d values of the transferbuffer fullbuf after reception (DMA setup function) are: \n",(20*16),DMABUFSIZE);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<(cyclecounter*DMABUFSIZE); i++)
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


int simon_omap_mcbsp_recv_buffer(unsigned int id, dma_addr_t buffer1dma, u32* buffer1kernel, dma_addr_t buffer2dma, u32* buffer2kernel, dma_addr_t buffer3dma, u32* buffer3kernel,
				unsigned int length)
{
	struct omap_mcbsp *mcbsp;
	//int dma_rx_ch;
//	int src_port = 0;
//	int dest_port = 0;
//	int sync_dev = 0;
	int status = 3; // dummy value
	int deviceRequestlineForDmaChannelsync = 0;
	char printtemp[500];
	int i;

	struct omap_dma_channel_params *dmaparams1;
	//int buf1_dmachannel;
	struct omap_dma_channel_params *dmaparams2;
	//int buf2_dmachannel;
	struct omap_dma_channel_params *dmaparams3;
	//int buf3_dmachannel;

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

	getMcBSPDevice(mcbspID,&mcbsp);

	deviceRequestlineForDmaChannelsync = mcbsp->dma_rx_sync; // RX

	dmaparams1->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams1->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams1->frame_count=1;	/* number of frames in a element */

	dmaparams1->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams1->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams1->src_start=  ((mcbsp->phys_base) + OMAP_MCBSP_REG_DRR);		/* source address : physical */
	dmaparams1->src_ei=0;		/* source element index */
	dmaparams1->src_fi=0;		/* source frame index */

	dmaparams1->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams1->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams1->dst_start=buffer1dma;		/* source address : physical */
	dmaparams1->dst_ei=0;		/* source element index */
	dmaparams1->dst_fi=0;		/* source frame index */

	dmaparams1->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams1->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams1->src_or_dst_synch=1;	/* source synch(1) or destination synch(0) */

	dmaparams1->ie=0;			/* interrupt enabled */

	dmaparams1->read_prio=0;/* read priority */
	dmaparams1->write_prio=0;/* write priority */

	dmaparams1->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */


	dmaparams2->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams2->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams2->frame_count=1;	/* number of frames in a element */

	dmaparams2->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams2->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams2->src_start=  ((mcbsp->phys_base) + OMAP_MCBSP_REG_DRR);		/* source address : physical */
	dmaparams2->src_ei=0;		/* source element index */
	dmaparams2->src_fi=0;		/* source frame index */

	dmaparams2->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams2->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams2->dst_start=buffer2dma;		/* source address : physical */
	dmaparams2->dst_ei=0;		/* source element index */
	dmaparams2->dst_fi=0;		/* source frame index */

	dmaparams2->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams2->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams2->src_or_dst_synch=1;	/* source synch(1) or destination synch(0) */

	dmaparams2->ie=0;			/* interrupt enabled */

	dmaparams2->read_prio=0;/* read priority */
	dmaparams2->write_prio=0;/* write priority */

	dmaparams2->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */


	dmaparams3->data_type=OMAP_DMA_DATA_TYPE_S32;		/* data type 8,16,32 */
	dmaparams3->elem_count=(length>>1);		/* number of elements in a frame */
	dmaparams3->frame_count=1;	/* number of frames in a element */

	dmaparams3->src_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams3->src_amode=OMAP_DMA_AMODE_CONSTANT;		/* constant, post increment, indexed,double indexed */
	dmaparams3->src_start=  ((mcbsp->phys_base) + OMAP_MCBSP_REG_DRR);		/* source address : physical */
	dmaparams3->src_ei=0;		/* source element index */
	dmaparams3->src_fi=0;		/* source frame index */

	dmaparams3->dst_port=0;		/* Only on OMAP1 REVISIT: Is this needed? */
	dmaparams3->dst_amode=OMAP_DMA_AMODE_POST_INC;		/* constant, post increment, indexed,double indexed */
	dmaparams3->dst_start=buffer3dma;		/* source address : physical */
	dmaparams3->dst_ei=0;		/* source element index */
	dmaparams3->dst_fi=0;		/* source frame index */

	dmaparams3->trigger=deviceRequestlineForDmaChannelsync;		/* trigger attached if the channel is synchronized */
	dmaparams3->sync_mode=OMAP_DMA_SYNC_ELEMENT;		/* sycn on element, frame , block or packet */
	dmaparams3->src_or_dst_synch=1;	/* source synch(1) or destination synch(0) */

	dmaparams3->ie=0;			/* interrupt enabled */

	dmaparams3->read_prio=0;/* read priority */
	dmaparams3->write_prio=0;/* write priority */

	dmaparams3->burst_mode=OMAP_DMA_DATA_BURST_DIS; /* Burst mode 4/8/16 words */

	/* chainable: */
	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_TX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 1 !",
				simon_omap_mcbsp_rx_dma_buf1_callback,
				buffer1kernel,
				&buf1_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf1_dmachannel);

	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_TX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 2 !",
				simon_omap_mcbsp_rx_dma_buf1_callback,
				buffer2kernel,
				&buf2_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf2_dmachannel);

	status = omap_request_dma(deviceRequestlineForDmaChannelsync, // The DMA request line to use; e.g. "OMAP24XX_DMA_MCBSP1_TX" for McBSP1 of (also) OMAP3530
				"McBSP RX test DMA for buffer 3 !",
				simon_omap_mcbsp_rx_dma_buf1_callback,
				buffer3kernel,
				&buf3_dmachannel);
	if (status)
	{
		dev_err(mcbsp->dev, " Unable to request DMA channel for McBSP%d RX.\n",mcbsp->id);
		return -EAGAIN;
	}
	dev_alert(mcbsp->dev, "Requested McBSP%d RX DMA channel %d\n", mcbsp->id, buf3_dmachannel);

	// initialise the inter-thread waiting funtionality:
	init_completion(&mcbsp->rx_dma_completion);
	//init_completion(&mcbsp_rx_dma_completion);



	//sync_dev = mcbsp->dma_rx_sync;

	//omap_set_dma_params(buf1_dmachannel, dmaparams1);

	/* Set up each DMA channel with it's parameters! */
	omap_set_dma_params(buf1_dmachannel, dmaparams1);
	omap_set_dma_params(buf2_dmachannel, dmaparams2);
	omap_set_dma_params(buf3_dmachannel, dmaparams3);


/*	omap_set_dma_transfer_params(buf1_dmachannel,
					dmaparams1->data_type,
					dmaparams1->elem_count, dmaparams1->frame_count,
					dmaparams1->sync_mode,
					dmaparams1->trigger, dmaparams1->src_or_dst_synch);

	omap_set_dma_src_params(buf1_dmachannel,
				dmaparams1->src_port,
				dmaparams1->src_amode,
				dmaparams1->src_start,
				dmaparams1->src_ei, dmaparams1->src_fi);

	omap_set_dma_dest_params(buf1_dmachannel,
					dmaparams1->dst_port,
					dmaparams1->dst_amode,
					dmaparams1->dst_start,
					dmaparams1->dst_ei, dmaparams1->dst_fi);
*/
/*
	omap_set_dma_transfer_params(buf1_dmachannel,
					OMAP_DMA_DATA_TYPE_S32,
					length >> 1, 1,
					OMAP_DMA_SYNC_ELEMENT,
					deviceRequestlineForDmaChannelsync, dmaparams1->src_or_dst_synch);

	omap_set_dma_src_params(buf1_dmachannel,
				src_port,
				OMAP_DMA_AMODE_CONSTANT,
				mcbsp->phys_base + OMAP_MCBSP_REG_DRR,
				0, 0);

	omap_set_dma_dest_params(buf1_dmachannel,
					dest_port,
					OMAP_DMA_AMODE_POST_INC,
					buffer1dma,
					0, 0);
*/

	/* Output initial buffer content: */
	printk(KERN_ALERT "The first %d of %d values of the transferbuffer bufbuf before reception (DMA setup function) are: \n",(20*16),DMABUFSIZE);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<min(DMABUFSIZE,(20*16)); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,buffer1kernel[i]);

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
	//omap_dma_link_lch(buf1_dmachannel, buf1_dmachannel);

	/* Linking! Cycle through the 3 buffers, making a ring DMA transfer! See screen-output.txt! */
	omap_dma_link_lch(buf1_dmachannel, buf2_dmachannel);
	omap_dma_link_lch(buf2_dmachannel, buf3_dmachannel);
	omap_dma_link_lch(buf3_dmachannel, buf1_dmachannel);


	/* Begin the DMA transfers! The first 128 bytes may be wrong as the fifo buffer within the mcbsp port first has to be emptied. */
	omap_start_dma(buf1_dmachannel);

	/* Now wait until the DMA callback function has been called MAXCYCLES times. */
//	wait_for_completion(&mcbsp->rx_dma_completion);
	printk(KERN_ALERT "Not waiting for transfers to end. Continuing init function now!\n");

	/* Display a snapshot of the transfer buffer buffer1kernel after the transfer: */
/*	printk(KERN_ALERT "The first millions of %d values of the transferbuffer bufbuf after reception (DMA setup function) are: \n",DMABUFSIZE);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<min(DMABUFSIZE,1000000); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,buffer1kernel[i]);

		if ((i%16) == 15)
		{
			printk(KERN_ALERT "%s \n",printtemp);
			sprintf(printtemp, "   ");
		}
	}
	printk(KERN_ALERT " end. \n");
*/
	/* Display the complete data that has been received in those MAXCYCLES dma-transfers.
	 * We could have continued the transfer, but as we are just storing the data in memory, we want to look at it sooner or later! */
/*	printk(KERN_ALERT "The first millions of %d values of the transferbuffer fullbuf (c=%d) after reception (DMA setup function) are: \n",(MAXCYCLES*DMABUFSIZE),cyclecounter);
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<(MAXCYCLES*DMABUFSIZE); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,fullbuf[i]);

		if ((i%16) == 15)
		{
			printk(KERN_ALERT "%s \n",printtemp);
			sprintf(printtemp, "   ");
		}
	}
	printk(KERN_ALERT " end. \n");
*/

/*	printk(KERN_ALERT " Doodling... \n");
	for (i=0;i<500;i++)
	{
		printk(KERN_ALERT ".");
	}
	printk(KERN_ALERT "\n");
	printk(KERN_ALERT " end. \n");
*/	
	return 0;
}

int send_network_message(struct msghdr *message_pointer, char *databuffer,struct socket *sock)
{
	int r;
	int oldmm;


	if ( network_iov == NULL)
	{
		printk(KERN_ALERT " Skipping send_network_message because network_iov is NULL. Check initialisation order? \n");
		return 1;
	}
	if ( network_message == NULL)
	{
		printk(KERN_ALERT " Skipping send_network_message because network_message is NULL. Check initialisation order? \n");
		return 1;
	}

	(*network_iov).iov_base = databuffer;
	(*network_iov).iov_len = DMABUFBYTES;
	(*network_message).msg_iov = network_iov;
	(*network_message).msg_iovlen = sizeof(*network_iov);
	(*network_message).msg_name = (struct sockaddr*) network_servaddr;
	(*network_message).msg_namelen = sizeof(*network_servaddr);

	if ( (*network_message).msg_iov == NULL)
	{
		printk(KERN_ALERT " Skipping send_network_message because network_message->msg_iov is NULL. Check initialisation order? \n");
		return 1;
	}
	if ( (*network_message).msg_name == NULL)
	{
		printk(KERN_ALERT " Skipping send_network_message because network_message->msg_name is NULL. Check initialisation order? \n");
		return 1;
	}

/*	if ( network_message->msg_iov->iov_base == NULL)
	{
		printk(KERN_ALERT " Skipping send_network_message because message_pointer->msg_iov->iov_base is NULL. Check initialisation order? \n");
		return 1;
	}
*/
	//message_pointer->msg_iov->iov_base = databuffer;

//	struct msghdr msg_header = *message_pointer; 
//	msg_header.msg_iov.iov_base = databuffer;

	/* get_fs() and set_fs(..) are extremely important! 
	 * Without them, sockets won't work from kernel space! 
	 * Search "The macro set_fs "... on http://www.linuxjournal.com/article/7660?page=0,2 */
	oldmm = get_fs(); set_fs(KERNEL_DS);
	r = sock_sendmsg(sock, network_message, DMABUFBYTES);
	set_fs(oldmm);
	
	/* For error codes see: http://lxr.free-electrons.com/source/include/asm-generic/errno.h */ 
	if (r < 0) printk("Request send Error: %d \n",r); 
	//else printk(" Message sent\n"); 

	return 0;
}

int prepare_network_message(struct msghdr *message_pointer, char *databuffer,struct socket *sock)
{
	//struct socket * sock; 

//	struct msghdr local_network_message = *message_pointer; 
//	struct iovec local_network_iov = *network_iov; 
//	struct sockaddr_in local_network_servaddr = *network_servaddr; 

	//unsigned char *send_buf="Hello this is the BeagleBoard Kernel speaking! Network communication is ready.\n"; 
	//memcpy(databuffer, send_buf, 80); 
	//memset (databuffer, 'u', sizeof(send_buf)); 
/*	sprintf(databuffer, "Hello this is the BeagleBoard Kernel speaking!\n");
	printk(databuffer);

	memset (&local_network_servaddr, 0, sizeof (local_network_servaddr)); 
	local_network_servaddr.sin_family = AF_INET;
	local_network_servaddr.sin_addr.s_addr = 0x0137A8C0; //res.word|htonl(0); 
	local_network_servaddr.sin_port = htons (targetPort); 

	printk ("The process is %s (pid %i)\n", (char *) current->comm, current->pid); 
	printk ("s_addr = %u\n", local_network_servaddr.sin_addr.s_addr); 

	if (sock_create(AF_INET, SOCK_DGRAM, 0,&sock)<0) 
	{ 
		printk(" Error Creating socket\n"); 
		return -1; 
	} 
	local_network_iov.iov_base = databuffer;
	local_network_iov.iov_len = UDPBUFBYTES; // sizeof(send_buf); 
	local_network_message.msg_name = (struct sockaddr*) &local_network_servaddr; 
	local_network_message.msg_namelen = sizeof (local_network_servaddr); 
	local_network_message.msg_iov = &local_network_iov; 
	local_network_message.msg_iovlen = sizeof(local_network_iov); 
	local_network_message.msg_control = NULL; 
	local_network_message.msg_controllen = 0; 
*/

	//*databuffer = *send_buf;

	memset (network_servaddr, 0, sizeof (*network_servaddr)); 
	(*network_servaddr).sin_family = AF_INET;
	(*network_servaddr).sin_addr.s_addr = 0x0137A8C0; //res.word|htonl(0); 
	(*network_servaddr).sin_port = htons (targetPort); 

	printk ("The process is %s (pid %i)\n", (char *) current->comm, current->pid); 
	printk ("s_addr = %u\n", (*network_servaddr).sin_addr.s_addr); 

	if (sock_create(AF_INET, SOCK_DGRAM, 0, &network_socket)<0) 
	{ 
		printk(" Error Creating socket\n"); 
		return -1; 
	} 
	(*network_iov).iov_base = databuffer;
	(*network_iov).iov_len = DMABUFBYTES; // each packet shall contain exactly the results of one dma transfer! (Even though databuffer is much larger)
	(*network_message).msg_name = (struct sockaddr*) network_servaddr;
	(*network_message).msg_namelen = sizeof (*network_servaddr); 
	(*network_message).msg_iov = network_iov; 
	(*network_message).msg_iovlen = sizeof(*network_iov); 
	(*network_message).msg_control = NULL; 
	(*network_message).msg_controllen = 0; 


	sprintf(databuffer, "Hello this is the BeagleBoard Kernel speaking!\n");
	printk(databuffer);


	send_network_message(network_message, databuffer, network_socket);

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

	/* Network stuff: */
//	struct socket init_network_socket; 
//	struct sockaddr_in init_serv_addr; 
//	struct msghdr init_network_message; 
//	struct iovec  init_network_iov; 



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
	bufbuf1 = dma_alloc_coherent(NULL, DMABUFBYTES, &bufbufdmaaddr1, GFP_KERNEL);
	if (bufbuf1 == NULL) {pr_err("Unable to allocate DMA buffer 1\n");return -ENOMEM;}
	bufbuf2 = dma_alloc_coherent(NULL, DMABUFBYTES, &bufbufdmaaddr2, GFP_KERNEL);
	if (bufbuf2 == NULL) {pr_err("Unable to allocate DMA buffer 2\n");return -ENOMEM;}
	bufbuf3 = dma_alloc_coherent(NULL, DMABUFBYTES, &bufbufdmaaddr3, GFP_KERNEL);
	if (bufbuf3 == NULL) {pr_err("Unable to allocate DMA buffer 3\n");return -ENOMEM;}

	/* Write dummy values. These should be overwritten later! */
	memset(bufbuf1, 0xbf1A1111, DMABUFSIZE * bytesPerVal );
	memset(bufbuf2, 0xbf1B1111, DMABUFSIZE * bytesPerVal );
	memset(bufbuf3, 0xbf1C1111, DMABUFSIZE * bytesPerVal );

	memset(fullbuf, 0x00000003, DMABUFSIZE*MAXCYCLES );

	mcbspID = mcbspPort-1;

	printk(KERN_ALERT "Starting DMA-McBSP-receive!\n");

	/* Prepare network! */
	//message_pointer = kzalloc(sizeof(struct msghdr), GFP_KERNEL);
	//if (!message_pointer) {return -ENOMEM;}
	//message_socket = kzalloc(sizeof(struct socket), GFP_KERNEL);
	//if (!message_socket) {return -ENOMEM;}

	network_databuffer = kzalloc(UDPBUFBYTES, GFP_KERNEL);
	if (!network_databuffer) {return -ENOMEM;}
	network_dataoffset = UDPBUFBYTES;

	network_socket = kzalloc(sizeof(struct socket), GFP_KERNEL);
	if (!network_socket) {return -ENOMEM;}
	network_servaddr = kzalloc(sizeof(struct sockaddr_in), GFP_KERNEL);
	if (!network_servaddr) {return -ENOMEM;}
	network_message = kzalloc(sizeof(struct msghdr), GFP_KERNEL);
	if (!network_message) {return -ENOMEM;}
	network_iov = kzalloc(sizeof(struct iovec), GFP_KERNEL);
	if (!network_iov) {return -ENOMEM;}

	prepare_network_message(network_message, (char*)network_databuffer,network_socket);
//	network_message = msg_header;
//	network_socket = sock;

//	network_socket = init_network_socket; 
//	serv_addr = 
//	network_message = 
//	network_iov = 

	snapshotpacketrate = kzalloc(snapshots, GFP_KERNEL);
	if (!snapshotpacketrate) {return -ENOMEM;}

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
	printk(KERN_ALERT "Setting gpio134 (ADS1258EVM-clockselect) as output, value 0=INTERNAL clock via EVM quarz (1=EXTERNAL clock from BB). Return status: %d\n",status);
	status = gpio_direction_output(135,1);
	printk(KERN_ALERT "Setting gpio135 (ADS1258EVM-nRESET) as output, value 1=no reset, so be active!. Return status: %d\n",status);
	status = gpio_direction_output(136,1);
	printk(KERN_ALERT "Setting gpio136 (ADS1258EVM-nPWDN) as output, value 1=no powerdown, so be active!. Return status: %d\n",status);
	status = gpio_direction_output(183,1);
	printk(KERN_ALERT "Setting gpio183 (ADS1258EVM-analogPowerMode) as output, value 1=unipolar. Return status: %d\n",status);
	/* End of GPIO stuff */


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
		printk(KERN_ALERT "Now initiating continuous data read from McBSP %d via DMA! \n", (mcbspID+1));
		//status = simon_omap_mcbsp_recv_buffers(mcbspID, bufbufdmaaddr1,bufbuf1, bufbufdmaaddr2,bufbuf2, bufbufdmaaddr3,bufbuf3, DMABUFSIZE * bytesPerVal/2 /* = elem_count in arch/arm/plat-omap/dma.c */); // the dma memory must have been allocated correctly. See above.
		//status = simon_omap_mcbsp_recv_buffer(mcbspID, bufbufdmaaddr1,bufbuf1, DMABUFSIZE * bytesPerVal/2 /* = elem_count in arch/arm/plat-omap/dma.c */); // the dma memory must have been allocated correctly. See above.
		status = simon_omap_mcbsp_recv_buffer(mcbspID, bufbufdmaaddr1,bufbuf1, bufbufdmaaddr2,bufbuf2, bufbufdmaaddr3,bufbuf3, DMABUFSIZE * bytesPerVal/2 /* = elem_count in arch/arm/plat-omap/dma.c */); // the dma memory must have been allocated correctly. See above.
		printk(KERN_ALERT "Finished initiating continuous data read from McBSP %d via DMA! Return status: %d \n", (mcbspID+1), status);

		printk(KERN_ALERT "The incoming data should now be being pumped out through UDP network packets to IP 0x%x (e.g. 0xC0A83701 = 192.168.55.1) port %u.\n",targetIP,targetPort);

	}
	else 
	   printk(KERN_ALERT "Not attempting to continue because requesting failed.\n");

	return returnstatus;
}

void hello_exit(void)
{
	int i;
	char printtemp[500];
	struct omap_mcbsp *mcbsp;

	/* For checking if the data corruption always only happens when not executing init or exit. */
/*	printk(KERN_ALERT " Doodling... \n");
	for (i=0;i<500;i++)
	{
		printk(KERN_ALERT ".");
	}
	printk(KERN_ALERT "\n");
	printk(KERN_ALERT " end. \n");
*/
	printk(KERN_ALERT "Telling DMA transfers to stop. (c=%d)\n",cyclecounter);
	finishDMAcycle = 1; // only used with background transfers.
	printk(KERN_ALERT "Waiting for transfers to end...\n");
	/* Now wait until the DMA callback end function has been called. */
	getMcBSPDevice(mcbspID,&mcbsp);
	wait_for_completion(&mcbsp->rx_dma_completion);
	//wait_for_completion(&mcbsp_rx_dma_completion);
	printk(KERN_ALERT "DMAs have now been stopped.");

	/* We can free the channels */
	omap_free_dma(buf3_dmachannel);
	omap_free_dma(buf2_dmachannel);
	omap_free_dma(buf1_dmachannel);


	/* Display the complete data that has been received in those MAXCYCLES dma-transfers.
	 * We could have continued the transfer, but as we are just storing the data in memory, we want to look at it sooner or later! */
	printk(KERN_ALERT "The first %d of %d values of the transferbuffer fullbuf (c=%d) after reception (exit function) are: \n",(20*16),(MAXCYCLES*DMABUFSIZE),cyclecounter );
	sprintf(printtemp, "receive: \n   ");
	for (i = 0 ; i<min(MAXCYCLES*DMABUFSIZE,(20*16)); i++)
	{
		sprintf(printtemp, "%s 0x%x,", printtemp,fullbuf[i]);

		if ((i%16) == 15)
		{
			printk(KERN_ALERT "%s \n",printtemp);
			sprintf(printtemp, "   ");
		}
	}
	printk(KERN_ALERT " end. \n");



	printk(KERN_ALERT "Stopping McBSP %d...", (mcbspID+1));
	omap_mcbsp_stop(mcbspID);	// kernel 2.6.31 and below have only one argument here!
	printk(KERN_ALERT "Freeing McBSP %d...", (mcbspID+1));
	omap_mcbsp_free(mcbspID);
	printk(KERN_ALERT "done.\n");

	printk(KERN_ALERT "Goodbye, McBSP world\n");
}

module_init(hello_init);
module_exit(hello_exit);
