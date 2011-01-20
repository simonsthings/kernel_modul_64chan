/*
 * Skeleton Linux Kernel Module
 *
 * PUBLIC DOMAIN
 */

#include <linux/module.h>
#include <linux/kernel.h>

int init_module(void)
{
	printk("Module init called\n");

	// return sucsess
	return 0;
}


void cleanup_module(void)
{
	printk("Module cleanup called\n");
}
