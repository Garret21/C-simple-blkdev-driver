#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/version.h>

MODULE_LICENSE("Dual BSD/GPL");

#define KERNEL_SECTOR_SIZE    512
#define MY_DEVICE_NAME "vbdev"
#define DEV_MINORS	16

static int diskmb = 100;	/*virtual device capacity*/
module_param_named(size, diskmb, int, 0);

#define ERR(...) pr_err("ERR: "__VA_ARGS__)
#define LOG(...) pr_info("LOG: "__VA_ARGS__)
#define DBG(...) pr_debug("DBG: "__VA_ARGS__)
