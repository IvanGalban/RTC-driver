/* This file holds the definitions used to deal with devices. It consists in
 * two parts: the client view and the driver view.
 *
 * The client view is the one using the devices for reading, writing, doing
 * ioctl, etc. Basically, they'll use a dev_(block|char)_device_t structure
 * which will contain a pointer to operations to be performed on the given
 * device. These operations MUST receive a pointer to the device structure as
 * first argument (remember C is not object-oriented). So, basically, a typical
 * workflow would be:
 *  1. get the device.
 *    dev_block_device_t *blkdev;
 *    dev_get_block_device(dev, &blkdev);
 *  2. perform some operation (e.g. read the first 1000 bytes).
 *    char buf[1024];
 *    blkdev->ops->read(blkdev, buf, 0, 1000);
 *
 * The driver view is the one actually implementing the operations. It MUST
 * comply with the interface set at dev_(block|char)_device_operations_t. A
 * device driver can register devices by using
 * dev_register_(block|char)_device().
 *
 * IMPORTANT NOTICE: Use the dev_(block|char)_device_t structures as read-only.
 * NEVER EVER change a value in them. Use the functions to do so, otherwise
 * inconsistencies will start flooding the system and we'll all drown.
 * Drivers can and should modify these structures if they need to.
 *
 * Char and block devices use separated namespaces, thus the MAJORs repeat.
 * That's why we have char and block versions of everything instead of mixing
 * everything in a single device concept. And, well, the differences between
 * both types is more explicitely stated.
 *
 * TODO: Provide a generic layer to cache data.
 */

#ifndef __DEVICES_H__
#define __DEVICES_H__

#include <typedef.h>
#include <vfs.h>

/*****************************/
/*   Device identification   */
/*****************************/

#define DEV_MAKE_DEV(major, minor)  ((dev_t)((((major) & 0x00ff) << 8) | \
                                             ((minor) & 0x00ff)))

/* Below are the relevant major device numbers used in buhos. These numbers
 * are set to match Linux device numbers whenever possible. Refer to
 * http://www.lanana.org/docs/device-list/ for the whole Linux device list.
 * Minors are defined in the corresponding driver modules, but they'll also
 * follow, whenever possible, the Linux numbers. */

#define DEV_UNNAMED_MAJOR       0

/* Block devices */
#define DEV_IDE0_MAJOR          3     /* block */
#define DEV_IDE1_MAJOR         22     /* block */

/* Char devices */
#define DEV_MEM_MAJOR           1     /* char  */
#define DEV_TTY_MAJOR           4     /* char  */
#define DEV_FB_MAJOR           29     /* char  */

/****************************************************************************/
/* VFS API ******************************************************************/
/****************************************************************************/
#define DEV_FS_NAME             "devfs"
#define DEV_FS_MAJOR            DEV_UNNAMED_MAJOR
#define DEV_FS_MINOR            2
#define DEV_FS_DEVID            DEV_MAKE_DEV(DEV_FS_MAJOR, DEV_FS_MINOR)

/******************/
/*   Client API   */
/******************/

/* Device access modes. When requesting access to a device, a mode must be
 * specified. */
typedef u16   dev_mode_t;

#define DEV_MODE_O_READ       0x0001  /* Open device for reading. */
#define DEV_MODE_O_WRITE      0x0002  /* Open device for writting. */
#define DEV_MODE_O_EXCL       0x0004  /* Open device exclusively. */
#define DEV_MODE_O_DIRECT     0x0008  /* Don't cache the device data. */

/* Block devices API */
/* Requests access to a block device. */
int dev_blk_open(dev_t, int);
/* Release a block device from use. */
int dev_blk_release(dev_t);
int dev_blk_read(dev_t, char *, off_t, size_t);
int dev_blk_write(dev_t, char *, off_t, size_t);
int dev_blk_flush(dev_t);
int dev_blk_ioctl(dev_t, u32, void *);

int dev_chr_open(dev_t, int);
int dev_chr_release(dev_t);
int dev_chr_read(dev_t, char *, size_t);
int dev_chr_write(dev_t, char *, size_t);
int dev_chr_flush(dev_t);
int dev_chr_ioctl(dev_t, u32, void *);



#define DEV_MODE_CAN_READ     0x0001  /* Device is readable.  */
#define DEV_MODE_CAN_WRITE    0x0002  /* Device is writtable. */
#define DEV_MODE_DIRECT_IO    0x0004  /* Device should use no caching. */



/* Let's just provide the typedef before the actual definition to avoid the
 * cyclic reference problem. */
typedef struct dev_block_device             dev_block_device_t;
typedef struct dev_block_device_operations  dev_block_device_operations_t;
typedef struct dev_char_device              dev_char_device_t;
typedef struct dev_char_device_operations   dev_char_device_operations_t;

/*********************/
/*   Block devices   */
/*********************/

/* Remember to NEVER EVER change anything in this structure as a client. */
struct dev_block_device {
  dev_t devid;                          /* Dev ID (MAJOR and MINOR) */
  int count;                            /* Reference count. */
  dev_mode_t mode;                      /* Current open mode. */
  size_t sector_size;                   /* Sector size in bytes. */
  size_t sectors;                      /* Total sectors. */

  dev_block_device_operations_t *ops;   /* Operations. */
};

/* block device operations */
struct dev_block_device_operations {
  /* Used to request access to the device. */
  int (* open) (dev_block_device_t *, dev_mode_t);
  /* Used to release the device. */
  int (* release) (dev_block_device_t *);
  /* Read some blocks. */
  int (* read) (dev_block_device_t *, char *, off_t, size_t);
  /* Write some blocks. */
  int (* write) (dev_block_device_t *, char *, off_t, size_t);
  /* Flush cached data. */
  int (* flush) (dev_block_device_t *);
  /* Do ioctl. */
  int (* ioctl) (dev_block_device_t *, u32, void *);
  /* And much more to come eventually... I guess. */
};


/* Register block device */
int dev_register_block_device(dev_block_device_t *);

/* Remove block device */
int dev_remove_block_device(dev_t);

/* Get block device */
dev_block_device_t * dev_get_block_device(dev_t);

/*****************************************************************************/
/*   Char devices (old, deprecated)                                          */
/*****************************************************************************/

/* Again, being a client DON'T ALTER THIS STRUCTURE. */
struct dev_char_device {
  dev_t                           devid;  /* Dev ID (MAJOR and MINOR) */
  char                          * name;   /* Name used to register the file. */
  vfs_file_operations_t           fops;   /* File operations. */
  /* Deprecated fields. */
  int count;                            /* Reference count. */
  dev_char_device_operations_t *ops;    /* Operations. */

};

/* char device operations */
struct dev_char_device_operations {
  /* Used to request access to the device. */
  int (* open) (dev_char_device_t *, dev_mode_t);
  /* Used to release the device. */
  int (* release) (dev_char_device_t *);
  /* Read a byte. */
  int (* read) (dev_char_device_t *, char *);
  /* Write a byte. */
  int (* write) (dev_char_device_t *, char *);
  /* Do ioctl. */
  int (* ioctl) (dev_char_device_t *, u32, void *);
};


/* Register char device */
int dev_register_char_device(dev_char_device_t *);

/* Remove char device */
int dev_remove_char_device(dev_t);

/* Get char device */
dev_char_device_t * dev_get_char_device(dev_t);


/*****************************************************************************/
/* VFS based API *************************************************************/
/*****************************************************************************/

/* Registers a char device. */
int dev_register_char_dev(dev_t devid,
                          char *name,
                          vfs_file_operations_t *ops);

/* Unregisters a char device. */
int dev_unregister_char_dev(dev_t devid);

/* Assign default file operations to a device when opened. */
int dev_set_char_operations(vfs_vnode_t *node, vfs_file_t *filp);

/******************************/
/*  Subsystem initialization  */
/******************************/

/* This is for the kernel main code only. */
int dev_init();

#endif
