#define KERNEL31

#include <config/modversions.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>

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

int targetIP = 0xC0A83701; //192.168.55.1
module_param(targetIP, int, 0);
MODULE_PARM_DESC(targetIP, "The target UDP IP address. Default is 192.168.55.1, or 0xC0A83701 hex.");

int targetPort = 2002;
module_param(targetPort, int, 0);
MODULE_PARM_DESC(targetPort, "The target port. Default is 2002.");

int packetlengthUDP = 1000;
module_param(packetlengthUDP, int, 0);
MODULE_PARM_DESC(packetlengthUDP, "The packet data length (excl. 28 bytes header) for each UDP packet.");

//int wordlength = 32;
//module_param(wordlength, int, 32);
//MODULE_PARM_DESC(wordlength, "The word length in bits that the serial port should send in. Use 8,16 or 32 bits please.");

//int finishDMAcycle = 0;
//int cyclecounter = 0;
#define MAXCYCLES 50
u32 fullbuf[64*MAXCYCLES];



typedef struct socket *SOCKET;



SOCKET socket(int af, int type, int protocol)
{
        int rc;
        SOCKET socket;

        rc = sock_create_kern(af, type, protocol, &socket);
        if (rc < 0) {
                socket = NULL;
                goto Exit;
        }

      Exit:
        return socket;
}

int bind(SOCKET socket_p, const struct sockaddr *addr, int addrlen)
{
        int rc;

        rc = socket_p->ops->bind(socket_p, (struct sockaddr *)addr, addrlen);

        return rc;
}

int closesocket(SOCKET socket_p)
{
        sock_release(socket_p);

        return 0;
}

int recvfrom(SOCKET socket_p, char *buf, int len, int flags,
             struct sockaddr *from, int *fromlen)
{
        int rc;
        struct msghdr msg;
        struct kvec iov;

        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_name = from;    // will be struct sock_addr
        msg.msg_namelen = *fromlen;
        iov.iov_len = len;
        iov.iov_base = buf;

        rc = kernel_recvmsg(socket_p, &msg, &iov, 1, iov.iov_len, 0);

        return rc;
}

int sendto(SOCKET socket_p, const char *buf, int len, int flags,
           const struct sockaddr *to, int tolen)
{
        int rc;
        struct msghdr msg;
        struct kvec iov;

        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_name = (struct sockaddr *)to;   // will be struct sock_addr
        msg.msg_namelen = tolen;
        msg.msg_flags = 0;
        iov.iov_len = len;
        iov.iov_base = (char *)buf;

        rc = kernel_sendmsg(socket_p, &msg, &iov, 1, len);

        return rc;
}

/* Inspired by http://www.linuxjournal.com/article/7660?page=0,2 */
int network_test_likeglibc(void)
{
    struct sockaddr_in servaddr;
    struct sockaddr targetaddr;
    struct socket *control= NULL;
//    struct socket *data = NULL;
//    struct socket *new_sock = NULL;

    int r = -1;
    char *buf = "Hello this is the BeagleBoard Kernel speaking!";
    //char *response = kmalloc(256, GFP_KERNEL);
    //char *reply = kmalloc(256, GFP_KERNEL);

    printk("Opening a socket for sending '%s' over UDP...\n",buf);

    /* SOCKET */
    control = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    printk("The returned socket of socket() was: %x\n",(unsigned int)&control);

    memset(&servaddr,0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(2002); // port 2002
    servaddr.sin_addr.s_addr = htonl(0xC0A83701); //192.168.55.1

    memset(&targetaddr,0, sizeof(targetaddr));
    targetaddr.sa_family = AF_INET;
    targetaddr.sa_data[0] = 192;
    targetaddr.sa_data[1] = 168;
    targetaddr.sa_data[2] = 55;
    targetaddr.sa_data[3] = 1;

    /* BIND */
    r = bind(control, &targetaddr, sizeof(targetaddr));
    printk("The return status of bind was: %d\n",r);


    /* SENDMSG */
    r = sendto(	control,
		buf,
		sizeof(*buf),
		0,
		&targetaddr,
		sizeof(targetaddr));

    printk("The return status of sendto was: %d\n",r);
    return 0;
}



/* Inspired by http://www.linuxjournal.com/article/7660?page=0,2 */
int network_test(void)
{
	#define BUFSIZE 200

	struct sockaddr_in servaddr;
	struct sockaddr targetaddr;
	struct socket *control= NULL;
	//struct socket *data = NULL;
	//struct socket *new_sock = NULL;
	//struct msghdr *message;
	struct msghdr msg;
	struct iovec iov;

	int r = -1;
	char buf[BUFSIZE] = "Hello this is the BeagleBoard Kernel speaking!";
	char __user *userbuf;
	//char *response = kmalloc(256, GFP_KERNEL);
	//char *reply = kmalloc(256, GFP_KERNEL);

	r = copy_to_user(&userbuf, buf, sizeof(*buf));
	printk("Copying string to user space return status: %d\n",r);


	printk("Opening a socket for sending '%s' over UDP...\n",buf);

	//message = kzalloc(sizeof(struct msghdr), GFP_KERNEL);
	//printk("Allocating the messagt struct gave: %x\n",message);


	/* SOCKET */
	r = sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &control);
	printk("The return status of sock_create() was: %x\n",r);


	printk("The socket.state is:    %x  (1=SS_UNCONNECTED in net.h)\n",control->state);
	printk("The socket.type is:     %d  (2=SOCK_DGRAM in net.h)\n",control->type);
	printk("The socket.flags is:    %lx \n",control->flags);
	printk("The socket.fasync_list: %x\n",(unsigned int) (control->fasync_list));
	//printk("The socket.wait is:     %x\n",(unsigned int) (control->wait));
	printk("The socket.file is:     %x\n",(unsigned int) (control->file));
	printk("The socket.sk is:       %x\n",(unsigned int) (control->sk));
	printk("The socket.ops is:      %x\n",(unsigned int) (control->ops));

	printk("Now (struct socket)control.(struct sock)sk.(struct sock_common)__sk_common.(struct proto)skc_prot fields: \n");
	printk("The control->sk->sk_prot->name is:	%s\n",control->sk->sk_prot->name);
	//printk("The control->sk->sk_protocol is:	%s\n",control->sk->sk_protocol);



	printk("Now proto_ops fields (net.h + af_inet.c)\n");
	printk("The socket.ops.family is:	%x\n",control->ops->family);
	//printk("The socket.ops.owner is:	%x\n",control->ops->owner);
/*    printk("The socket.ops.release() is:	%x\n",control->ops->family);
	printk("The socket.ops.bind() is:	%x\n",control->ops->family);
	printk("The socket.ops.connect() is:	%x\n",control->ops->family);
	printk("The socket.ops.socketpair() is:	%x\n",control->ops->family);
	printk("The socket.ops.accept() is:	%x\n",control->ops->family);
	printk("The socket.ops.getname() is:	%x\n",control->ops->family);
	printk("The socket.ops.poll() is:	%x\n",control->ops->family);
	printk("The socket.ops.ioctl() is:	%x\n",control->ops->family);
	printk("The socket.ops.compat_ioctl() is:	%x\n",control->ops->family);
	printk("The socket.ops.listen() is:	%x\n",control->ops->family);
	printk("The socket.ops.shutdown() is:	%x\n",control->ops->family);
	printk("The socket.ops.setsockopt() is:	%x\n",control->ops->family);
	printk("The socket.ops.getsockopt() is:	%x\n",control->ops->family);
	printk("The socket.ops.compat_setsockopt() is:	%x\n",control->ops->family);
	printk("The socket.ops.compat_getsockopt() is:	%x\n",control->ops->family);
	printk("The socket.ops.sendmsg() is:	%x\n",control->ops->family);
	printk("The socket.ops.recvmsg() is:	%x\n",control->ops->family);
	printk("The socket.ops.mmap() is:	%x\n",control->ops->family);
	printk("The socket.ops.sendpage() is:	%x\n",control->ops->family);
	printk("The socket.ops.splice_read() is:	%x\n",control->ops->family);
*/

	targetaddr.sa_family = AF_INET;
	targetaddr.sa_data[0] = 192;   // port byte 1
	targetaddr.sa_data[1] = 192;   // port byte 2
	targetaddr.sa_data[2] = 192;   // IPv4 byte 1
	targetaddr.sa_data[3] = 192;   // IPv4 byte 2
	targetaddr.sa_data[4] = 192;   // IPv4 byte 3
	targetaddr.sa_data[5] = 192;   // IPv4 byte 4
	// Finding the actually executed function: inet_dgram_connect in af_inet.c --> struct proto udp_prot in net/ipv4/udp.c --> ip4_datagram_connect in net/ipv4/datagram.c:
//	r = control->ops->connect(control, &targetaddr, sizeof(targetaddr), 0);
//	printk("The return status of control->ops->connect() was: %d\n",r);
//	printk("The socket.state is:    %x  (1=SS_UNCONNECTED in net.h)\n\n",control->state);


	memset(&servaddr,0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = 0xC0C0; // htons(2002); // port 49344 or C0 C0 = 192 192
	servaddr.sin_addr.s_addr = 0x0137A8C0; // htonl(0xC0C0C0C0); //192.192.192.192
//	servaddr.sin_addr.s_addr = htonl(0x10738A0C); //192.168.55.1
//	servaddr.sin_addr.s_addr = htonl(0x0137A8C0); //192.168.55.1
//	servaddr.sin_addr.s_addr = htonl(0xC0A83701); //192.168.55.1

	printk ("s_addr = %u  \n", servaddr.sin_addr.s_addr); 

//    r = control->ops->connect( control, 
//			       (struct sockaddr *) &servaddr,
//			       sizeof(servaddr),
//			       O_RDWR);



	printk("The size of 'control' is : %d\n",sizeof(control));
	printk("The size of '*control' is : %d\n",sizeof(*control));
	printk("The size of 'targetaddr' is : %d\n",sizeof(targetaddr));
	printk("The size of 'servaddr' is : %d\n",sizeof(servaddr));
	printk("The size of 'buf' is : %d\n",sizeof(buf));

	/*http://forum.soft32.com/linux2/Linux-kernel-socket-programming-ftopict35718.html*/
	// just testing if we can create an fd:
/*	if (sock_map_fd(sock) < 0) 
	{ 
	printk(" Error mapping socket\n"); 
	return -1; 
	} 

	sock->sk->allocation = GFP_NOIO; 
	msg_iov.iov_base = buf; 
	msg_iov.iov_len = 8; 
	msg_header.msg_name = (struct sockaddr*) &servaddr; 
	msg_header.msg_namelen = sizeof (servaddr); 
	msg_header.msg_iov = &msg_iov; 
	msg_header.msg_iovlen = sizeof (msg_iov); 
	msg_header.msg_control = NULL; 
	msg_header.msg_controllen = 0; 
*/


	msg.msg_control = NULL; // MUST BE NULL for UDP //control;
	msg.msg_controllen = 0; // MUST BE 0 for UDP    //sizeof(*control);
	msg.msg_name = &servaddr;    // will first be struct sock_addr but then casted to sock_addr_in
	msg.msg_namelen = sizeof(servaddr);
//	msg.msg_name = &targetaddr;    // will first be struct sock_addr but then casted to sock_addr_in
//	msg.msg_namelen = sizeof(targetaddr);
        msg.msg_flags = 0;
	iov.iov_base = buf;
	iov.iov_len = BUFSIZE;
	msg.msg_iov = &iov;
	msg.msg_iovlen = sizeof(iov);

	printk("The size of 'iov' is : %d\n",sizeof(iov));
	printk("The size of 'msg' is : %d\n",sizeof(msg));

	/* SENDMSG */
	r = control->ops->sendmsg(	NULL, // callback structure: http://lxr.free-electrons.com/source/include/linux/aio.h?v=2.6.31;a=arm#L54
					control,
			   		&msg, 
					sizeof(msg));
	//r=control->ops->sendmsg(sock, buffer, strlen(buffer), 0, &server, length);
	if (r < 0) {printk("Error during packet send: %d\n",r);}


	printk("The return status of sendto was: %d\n",r);
	return 0;
}

/* Inspired by http://forum.soft32.com/linux2/Linux-kernel-socket-programming-ftopict35718.html */
int network_shuang(void)
{
	#define SIZE 100
	#define PORT 49344
	int r;
	int oldmm;

	struct socket * sock; 
	struct sockaddr_in serv_addr; 

	struct msghdr msg_header; 
	struct iovec msg_iov; 

	unsigned char send_buf[]="Hello this is the BeagleBoard Kernel speaking!\n"; 

	//memset (send_buf, 'u', sizeof(send_buf)); 
	//sprintf(send_buf, "Hello this is the BeagleBoard Kernel speaking!\n");

	memset (&serv_addr, 0, sizeof (serv_addr)); 
	serv_addr.sin_family = AF_INET; 
	serv_addr.sin_addr.s_addr = 0x0137A8C0; //res.word|htonl(0); 
	serv_addr.sin_port = htons (PORT); 

	printk ("The process is %s (pid %i)\n", (char *) current->comm, current->pid); 
	printk ("s_addr = %u\n", serv_addr.sin_addr.s_addr); 

	if (sock_create(AF_INET, SOCK_DGRAM, 0,&sock)<0) 
	{ 
		printk(" Error Creating socket\n"); 
		return -1; 
	} 


//	sock->sk->allocation = GFP_NOIO; 
	msg_iov.iov_base = send_buf; 
	msg_iov.iov_len = sizeof(send_buf); 
	msg_header.msg_name = (struct sockaddr*) &serv_addr; 
	msg_header.msg_namelen = sizeof (serv_addr); 
	msg_header.msg_iov = &msg_iov; 
	msg_header.msg_iovlen = sizeof(msg_iov); 
	msg_header.msg_control = NULL; 
	msg_header.msg_controllen = 0; 

	/* get_fs() and set_fs(..) are extremely important! 
	 * Without them, sockets won't work from kernel space! 
	 * Search "The macro set_fs "... on http://www.linuxjournal.com/article/7660?page=0,2 */
	oldmm = get_fs(); set_fs(KERNEL_DS);
	r = sock_sendmsg(sock, &msg_header, sizeof(send_buf));
	set_fs(oldmm);
	
	/* For error codes see: http://lxr.free-electrons.com/source/include/asm-generic/errno.h */ 
	if (r < 0) printk("Request send Error: %d \n",r); 
	else printk(" Message sent\n"); 

	return 0;

}

/* Other pages of interest: 
http://en.wikipedia.org/wiki/Berkeley_sockets
http://opengroup.org/onlinepubs/007908799/xns/syssocket.h.html (careful: includes are only valid in user space!)
http://kerneltrap.org/node/7491
*/
int hello_init(void)
{
	int returnstatus=0;

	printk(KERN_ALERT "Hello, ethernet UDP through kernel world!\n");

	network_shuang();



	return returnstatus;
}

void hello_exit(void)
{
	printk(KERN_ALERT "Goodbye, ethernet UDP through kernel world.\n");
}

module_init(hello_init);
module_exit(hello_exit);
