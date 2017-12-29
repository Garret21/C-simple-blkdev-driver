#include "blkdriver.h"
static int major;
module_param(major, int, 0);
static int hardsect_size = KERNEL_SECTOR_SIZE;
module_param(hardsect_size, int, 0);
static int ndevices = 1;
module_param(ndevices, int, 0);

static int nsectors;

struct disk_dev {               /* The internal representation of our device.*/
	int size;                    /* Device size in sectors*/
	u8 *data;                    /* The data array */
	spinlock_t lock;             /* For mutual exclusion */
	struct request_queue *queue; /* The device request queue */
	struct gendisk *gd;          /* The gendisk structure */
};

static struct disk_dev *devices;

static int transfer(struct disk_dev *dev, unsigned long sector,
			unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;
	if ((offset + nbytes) > dev->size) {
		ERR("beyond-end write (%ld %ld)\n", offset, nbytes);
		return -EIO;
	}
	if (write)
		memcpy(dev->data + offset, buffer, nbytes);
	else
		memcpy(buffer, dev->data + offset, nbytes);
	return 0;
}

static void make_request(struct request_queue *q)
{
	struct request *req;
	unsigned nr_sectors, sector;
	DBG("entering simple request routine\n");
	req = blk_fetch_request(q);
	while (req) {
		int ret = 0;
		struct disk_dev *dev = req->rq_disk->private_data;
		if (req->cmd_type != REQ_TYPE_FS) {
			ERR("skip non-fs request\n");
			__blk_end_request_all(req, -EIO);
			req = blk_fetch_request(q);
			continue;
		}
		nr_sectors = blk_rq_cur_sectors(req);
		sector = blk_rq_pos(req);
		ret = transfer(dev, sector, nr_sectors,
			req->buffer, rq_data_dir(req));
		if (!__blk_end_request_cur(req, ret))
			req = blk_fetch_request(q);
	}
}

static int my_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	unsigned long sectors = (diskmb * 1024) * 2;
	DBG("getgeo\n");
	geo->heads = 4;
	geo->sectors = 16;
	geo->cylinders = sectors / geo->heads / geo->sectors;
	geo->start = geo->sectors;
	return 0;
};

static int my_ioctl(struct block_device *bdev, fmode_t mode,
				unsigned int cmd, unsigned long arg)
{
	LOG("ioctl cmd=%X\n", cmd);
	switch (cmd) {
	case HDIO_GETGEO: {
		struct hd_geometry geo;
		LOG("ioctl HDIO_GETGEO\n");
		my_getgeo(bdev, &geo);
		if (copy_to_user((void __user *)arg, &geo, sizeof(geo)))
			return -EFAULT;
		return 0;
	}
	default:
		ERR("ioctl unknown command\n");
		return -ENOTTY;
	}
}

static const struct block_device_operations mybdrv_fops = {
	.owner = THIS_MODULE,
	.ioctl = my_ioctl,
	.getgeo = my_getgeo
};

static void setup_device(struct disk_dev *dev, int which)
{
	LOG("setuping virtual device\n");
	memset(dev, 0, sizeof(struct disk_dev));
	dev->size = diskmb * 1024 * 1024;
	dev->data = vmalloc(dev->size);
	if (dev->data == NULL) {
		ERR("vmalloc failure.\n");
		return;
	}
	spin_lock_init(&dev->lock);
	dev->queue = blk_init_queue(make_request, &dev->lock);
	if (dev->queue == NULL)
		goto out_vfree;
	blk_queue_logical_block_size(dev->queue, hardsect_size);
	dev->queue->queuedata = dev;
	dev->gd = alloc_disk(DEV_MINORS);
	if (!dev->gd) {
		ERR("alloc_disk failure\n");
		goto out_vfree;
	}
	dev->gd->major = major;
	dev->gd->minors = DEV_MINORS;
	dev->gd->first_minor = which * DEV_MINORS;
	dev->gd->fops = &mybdrv_fops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf(dev->gd->disk_name, DISK_NAME_LEN - 1,
		 MY_DEVICE_NAME"%c", which + 'a');
	set_capacity(dev->gd, nsectors * (hardsect_size / KERNEL_SECTOR_SIZE));
	add_disk(dev->gd);
	LOG("virtual device setuped\n");
	return;


out_vfree:
	if (dev->data)
		vfree(dev->data);
}

static int __init blk_init(void)
{
	int i;
	nsectors = diskmb * 1024 * 1024 / hardsect_size;
	major = register_blkdev(major, MY_DEVICE_NAME);
	LOG("init driver\n");
	if (major <= 0) {
		ERR("unable to get major number\n");
		return -EBUSY;
	}
	devices = kmalloc(ndevices * sizeof(struct disk_dev), GFP_KERNEL);
	if (devices == NULL)
		goto blkdev_unregister;
	for (i = 0; i < ndevices; i++)
		setup_device(devices + i, i);
	LOG("driver inited\n");
	return 0;

blkdev_unregister:
	unregister_blkdev(major, MY_DEVICE_NAME);
	return -ENOMEM;
}

static void blk_exit(void)
{
	int i;
	LOG("turning off driver\n");
	for (i = 0; i < ndevices; i++) {
		struct disk_dev *dev = devices + i;
		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if (dev->queue)
			blk_cleanup_queue(dev->queue);
		if (dev->data)
			vfree(dev->data);
	}
	unregister_blkdev(major, MY_DEVICE_NAME);
	kfree(devices);
	LOG("driver turned off\n");
}

module_init(blk_init);
module_exit(blk_exit);
