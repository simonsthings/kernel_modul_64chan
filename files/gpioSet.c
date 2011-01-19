#include <config/modversions.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>

//#include <linux/sched.h>
//#include <linux/workqueue.h>
//#include <linux/interrupt.h>	/* We want an interrupt */
//#include <asm/io.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Simon Vogt <simonsunimail@gmail.com>");

int gpio133 = 1; /* on */
module_param(gpio133, int, 1);
MODULE_PARM_DESC(gpio133, "The output value of gpio 133 (Pin 15 of BB header). Default is 1 (high).");
int gpio134 = 1; /* on */
module_param(gpio134, int, 1);
MODULE_PARM_DESC(gpio134, "The output value of gpio 134 (Pin 13 of BB header). Default is 1 (high).");
int gpio135 = 1; /* on */
module_param(gpio135, int, 1);
MODULE_PARM_DESC(gpio135, "The output value of gpio 135 (Pin 11 of BB header). Default is 1 (high).");
int gpio136 = 1; /* on */
module_param(gpio136, int, 1);
MODULE_PARM_DESC(gpio136, "The output value of gpio 136 (Pin 9 of BB header). Default is 1 (high).");
int gpio137 = 1; /* on */
module_param(gpio137, int, 1);
MODULE_PARM_DESC(gpio137, "The output value of gpio 137 (Pin 7 of BB header). Default is 1 (high).");
int gpio138 = 1; /* on */
module_param(gpio138, int, 1);
MODULE_PARM_DESC(gpio138, "The output value of gpio 138 (Pin 5 of BB header). Default is 1 (high).");
int gpio139 = 1; /* on */
module_param(gpio139, int, 1);
MODULE_PARM_DESC(gpio139, "The output value of gpio 139 (Pin 3 of BB header). Default is 1 (high).");
int gpio168 = 1; /* on */
module_param(gpio168, int, 1);
MODULE_PARM_DESC(gpio168, "The output value of gpio 168 (Pin 24 of BB header). Default is 1 (high).");
int gpio183 = 1; /* on */
module_param(gpio183, int, 1);
MODULE_PARM_DESC(gpio183, "The output value of gpio 183 (Pin 23 of BB header). Default is 1 (high).");


int gpioset_init(void)
{
	int status = 0;
	int reqstatus = -4;
	//int value = 0;
	int returnstatus = 3;



	printk(KERN_ALERT "Requesting GPIOs on BeagleBoard expansion header!\n");

	/* Do GPIO stuff */
	// requesting:
	reqstatus = gpio_request(133, "myGPIO-133");
	printk(KERN_ALERT "Gpio 133 was requested and the answer is: %d\n",reqstatus);
	reqstatus = gpio_request(134, "myGPIO-134");
	printk(KERN_ALERT "Gpio 134 was requested and the answer is: %d\n",reqstatus);
	reqstatus = gpio_request(135, "myGPIO-135");
	printk(KERN_ALERT "Gpio 135 was requested and the answer is: %d\n",reqstatus);
	reqstatus = gpio_request(136, "myGPIO-136");
	printk(KERN_ALERT "Gpio 136 was requested and the answer is: %d\n",reqstatus);
	reqstatus = gpio_request(137, "myGPIO-137");
	printk(KERN_ALERT "Gpio 137 was requested and the answer is: %d\n",reqstatus);
	reqstatus = gpio_request(138, "myGPIO-138");
	printk(KERN_ALERT "Gpio 138 was requested and the answer is: %d\n",reqstatus);
	reqstatus = gpio_request(139, "myGPIO-139");
	printk(KERN_ALERT "Gpio 139 was requested and the answer is: %d\n",reqstatus);
	reqstatus = gpio_request(168, "myGPIO-168");
	printk(KERN_ALERT "Gpio 168 was requested and the answer is: %d\n",reqstatus);
	reqstatus = gpio_request(183, "myGPIO-183");
	printk(KERN_ALERT "Gpio 183 was requested and the answer is: %d\n",reqstatus);

	// setting:
	status = gpio_direction_output(133,gpio133);
	printk(KERN_ALERT "Setting gpio133 as output %d. Return status: %d\n",gpio133,status);
	status = gpio_direction_output(134,gpio134);
	printk(KERN_ALERT "Setting gpio134 as output %d. Return status: %d\n",gpio134,status);
	status = gpio_direction_output(135,gpio135);
	printk(KERN_ALERT "Setting gpio135 as output %d. Return status: %d\n",gpio135,status);
	status = gpio_direction_output(136,gpio136);
	printk(KERN_ALERT "Setting gpio136 as output %d. Return status: %d\n",gpio136,status);
	status = gpio_direction_output(137,gpio137);
	printk(KERN_ALERT "Setting gpio137 as output %d. Return status: %d\n",gpio137,status);
	status = gpio_direction_output(138,gpio138);
	printk(KERN_ALERT "Setting gpio138 as output %d. Return status: %d\n",gpio138,status);
	status = gpio_direction_output(139,gpio139);
	printk(KERN_ALERT "Setting gpio139 as output %d. Return status: %d\n",gpio139,status);
	status = gpio_direction_output(168,gpio168);
	printk(KERN_ALERT "Setting gpio168 as output %d. Return status: %d\n",gpio168,status);
	status = gpio_direction_output(183,gpio183);
	printk(KERN_ALERT "Setting gpio183 as output %d. Return status: %d\n",gpio183,status);
	/* End of GPIO stuff */



	return returnstatus;
}

void gpioset_exit(void)
{
	printk(KERN_ALERT "Freeing GPIO 133...");
	gpio_free(133);
	printk(KERN_ALERT "Freeing GPIO 134...");
	gpio_free(134);
	printk(KERN_ALERT "Freeing GPIO 135...");
	gpio_free(135);
	printk(KERN_ALERT "Freeing GPIO 136...");
	gpio_free(136);
	printk(KERN_ALERT "Freeing GPIO 137...");
	gpio_free(137);
	printk(KERN_ALERT "Freeing GPIO 138...");
	gpio_free(138);
	printk(KERN_ALERT "Freeing GPIO 139...");
	gpio_free(139);
	printk(KERN_ALERT "Freeing GPIO 168...");
	gpio_free(168);
	printk(KERN_ALERT "Freeing GPIO 183...");
	gpio_free(183);
	printk(KERN_ALERT "done.\n");

	printk(KERN_ALERT "Goodbye, GPIOs.\n");
}

module_init(gpioset_init);
module_exit(gpioset_exit);
