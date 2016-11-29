/* We'll have two different trees. The first is to hold the actual filesystem
 * entities, i.e. filesytem types, superblocks, vnodes and open files.
 *
 *                    +-----+-----+---------------------------------------
 *  filesystem types: | ft1 | ft2 |
 *                    +--|--+--|--+---------------------------------------
 *                       |     +-----+
 *                       +------+    |
 *                       |      |    |
 *                    +--|--+---|-+--|--+---------------------------------
 *  superblocks:      | sb1 | sb2 | sb3 |
 *                    +--|--+-----+-----+---------------------------------
 *                       |
 *                       +-----+-----+
 *                       |     |     |
 *                    +--|--+--|--+--|--+---------------------------------
 *  vnodes:           | vn1 | vn2 | vn3 |
 *                    +--|--+--|--+--|--+---------------------------------
 *                       |     |     |
 *                       |     |     +-----+
 *                       |     |     |     |
 *                    +--|--+--|--+--|--+--|--+---------------------------
 *  open files:       | of1 | of2 | of3 | of4 |
 *                    +-----+-----+-----+-----+---------------------------
 *
 * All filesystem types will be stored, whether there is a superblock of this
 * type or not. The same will happen to superblocks, regardless of whether
 * they are mounted or not and whether there are vnodes belonging to it in use
 * or not. However, vnodes will only be stored when there is at least an open
 * file referencing it, meaning we won't cache vnodes and we will release them
 * as soon as their reference counter reaches zero.
 *
 * Then we'll have another tree, the dentries tree, which we will use to
 * hold paths and which will work as a path cache. Since superblocks will
 * always be present dentries will hold pointers to the superblocks they belong
 * to and, in case of being a mountpoint, to the superblocks they are the
 * mountpoint of. However, since vnodes are only present when they are in use,
 * dentries will hold only the vnode number.
 *
 *                     *sb1  (vn1) (vn2) (vn3) (vn4)
 *                       |     |     |     |     |
 *                    +--|--+--|--+--|--+--|--+--|--+---------------------
 *  dentries:         | dt1 | dt2 | dt3 | dt4 | dt5 |
 *                    +--|--+-|-|-+-|---+-|-|-+-|---+---------------------
 *                       |    | |   |     | |   |
 *                       +<---+ |   |     | +<--+
 *                       +<-----|---+     |
 *                              +<--------+
 */

#include <vfs.h>
#include <list.h>
#include <mem.h>
#include <fs/rootfs.h>
#include <string.h>
#include <errors.h>


#define VFS_MAX_FILES         1024
#define VFS_DEFAULT_BLK_SIZE  1024

/* Open files. */
static list_t vfs_files;

/* Head of the whole VFS tree. */
static vfs_dentry_t * vfs_root_dentry;

/*****************************************************************************/
/* Filesystem types **********************************************************/
/*****************************************************************************/

/* Filesystem types. All registered filesystem types will be here. They are
 * identified by their names (e.g. "rootfs", "devfs", "minix"). */
static list_t vfs_fs_types;

/* Comparison function for fs_types. */
static int vfs_fs_types_cmp(void *item, void *ft_name) {
  return ! strcmp(((vfs_fs_type_t *)item)->ro.ft_name, (char *)ft_name);
}

/* Filesystem type lookup. */
static vfs_fs_type_t * vfs_fs_type_lookup(char *name) {
  return (vfs_fs_type_t *)list_find(&vfs_fs_types,
                                    vfs_fs_types_cmp,
                                    name);
}

/* Allocates a new filesystem type with default values. */
static vfs_fs_type_t * vfs_fs_type_alloc(char *name) {
  vfs_fs_type_t * ft;

  ft = (vfs_fs_type_t *)kalloc(sizeof(vfs_fs_type_t));
  if (ft == NULL)
    return NULL;

  if (list_add(&vfs_fs_types, ft) == -1) {
    kfree(ft);
    set_errno(E_NOMEM);
    return NULL;
  }

  ft->ro.ft_name = (char *)kalloc(strlen(name) + 1);
  if (ft->ro.ft_name == NULL) {
    list_find_del(&vfs_fs_types, vfs_fs_types_cmp, &(ft->ro.ft_name));
    kfree(ft);
    set_errno(E_NOMEM);
    return NULL;
  }
  strcpy(ft->ro.ft_name, name);

  ft->ft_ops.ft_get_sb = NULL;
  ft->ft_ops.ft_kill_sb = NULL;

  return ft;
}

/* Removes a filesystem type. */
static int vfs_fs_type_dealloc(vfs_fs_type_t *ft) {
  /* Remove it from the list. */
  list_find_del(&vfs_fs_types, vfs_fs_types_cmp, &(ft->ro.ft_name));

  /* Free the name. */
  if (ft->ro.ft_name != NULL) {
    kfree(ft->ro.ft_name);
  }

  /* Free it. */
  kfree(ft);
  return 0;
}

/* Registers a filesystem type. */
int vfs_fs_type_register(char *name, vfs_fs_type_config_t config) {
  vfs_fs_type_t *ft;

  ft = vfs_fs_type_lookup(name);
  if (ft != NULL) {
    set_errno(E_EXIST);
    return -1;
  }

  ft = vfs_fs_type_alloc(name);
  if (ft == NULL) {
    /* errno was set by _alloc(). */
    return -1;
  }

  if (config(ft) == -1) {
    vfs_fs_type_dealloc(ft);
    set_errno(E_IO);
    return -1;
  }

  return 0;
}

/*****************************************************************************/
/* Superblocks ***************************************************************/
/*****************************************************************************/

/* Superblocks. All registered superblocks will be here. They will be
 * identified by the device id of the device holding the filesystem. */
static list_t vfs_sbs;

/* Superblock comparison function. */
static int vfs_sb_cmp(void *item, void *devid) {
  return ((vfs_sb_t *)item)->ro.sb_devid == *((dev_t *)devid);
}

/* Looks a superblock up. */
static vfs_sb_t * vfs_sb_lookup(dev_t devid) {
  return (vfs_sb_t *)list_find(&vfs_sbs, vfs_sb_cmp, &devid);
}

/* Creates a superblock and adds it to the list. */
static vfs_sb_t * vfs_sb_alloc(dev_t devid) {
  vfs_sb_t *sb;

  sb = (vfs_sb_t *)kalloc(sizeof(vfs_sb_t));
  if (sb == NULL)
    return NULL;

  if (list_add(&vfs_sbs, sb) == -1) {
    kfree(sb);
    return NULL;
  }

  /* Set default values. */
  sb->sb_blocksize = VFS_DEFAULT_BLK_SIZE;
  sb->sb_blocks = 0;
  sb->sb_max_bytes = 0;
  sb->sb_flags = VFS_SB_F_UNUSED;

  sb->sb_ops.delete_vnode = NULL;
  sb->sb_ops.read_vnode = NULL;
  sb->sb_ops.write_vnode = NULL;
  sb->sb_ops.delete_vnode = NULL;
  sb->sb_ops.mount = NULL;
  sb->sb_ops.unmount = NULL;

  sb->sb_root_vno = 0;
  sb->private_data = NULL;

  sb->ro.sb_devid = devid;
  sb->ro.sb_fs_type = NULL;   /* It will be set when the discovery works. */

  sb->ro.sb_mnt = NULL;

  return sb;
}

/* Deallocs a superblock. */
static int vfs_sb_dealloc(vfs_sb_t *sb) {
  /* Tell the filesystem type the superblock is about to be released. */
  if (sb->ro.sb_fs_type->ft_ops.ft_kill_sb != NULL) {
    if (sb->ro.sb_fs_type->ft_ops.ft_kill_sb(sb) == -1) {
      set_errno(E_IO);
      return -1;
    }
  }

  /* Remove it from the list of superblocks. */
  list_find_del(&vfs_sbs, vfs_sb_cmp, &(sb->ro.sb_devid));

  /* Release the memory. */
  kfree(sb);

  return 0;
}

/*****************************************************************************/
/* Dentries ******************************************************************/
/*****************************************************************************/

/* Dentries. All dentries will be stored here. When paths are traversed
 * dentries are added to the list, but their reference counters are only
 * modified when vnodes are allocated or deallocated. Dentries are not
 * strictly necessary, they exist only for performance purposes, so there is
 * no real problem to release them even when there are vnodes pointed by them.
 * However, with mountpoints the situation is different since dentries are
 * the only way we use to keep track of mounted filesystems. Thus, dentries
 * acting as mountpoint can not be evicted until the superblock they are the
 * mountpoint of is unmonted. This lets us simply use a traditional array
 * instead of a slower list. We keep a counter for every entry just to know
 * what are the best entries to evict when there's no room for a new dentry.
 * We identify empty spaces by checking the d_name field to be NULL because
 * all dentries must have a name. And the eviction algorithm is Least
 * Frecuently Used (LFU), which I know is not the best but is simple to
 * implement. */
#define VFS_MAX_DENTRIES        100
static vfs_dentry_t vfs_dentries[VFS_MAX_DENTRIES]; /* Dentries are 24 bytes
                                                     * long, thus this will
                                                     * make the kernel just
                                                     * 2400 bytes larger. */

/* Resets a dentry to a initial, empty state. */
static void vfs_dentry_reset(vfs_dentry_t *dentry) {
  if (dentry->d_name != NULL)
    kfree(dentry->d_name);

  memset(dentry, 0, sizeof(vfs_dentry_t));
}

/* Allocates a dentry in the cache. */
static vfs_dentry_t * vfs_dentry_get(vfs_dentry_t *parent, char *name) {
  int i;
  int lowest_count, lowest_index;
  vfs_dentry_t *d;

  /* Look for the wanted dentry and in the meantime compute where to store a
   * new dentry in case we need it. */
  for (i = 0, lowest_index = -1; i < VFS_MAX_DENTRIES; i ++) {
    /* If the slot if free this is the one we'll be using in case we don't
     * find the dentry. */
    if (vfs_dentries[i].d_name == NULL) {
      lowest_index = i;
      lowest_count = 0; /* Nothing will be smaller than zero. */
      continue;
    }
    /* This is the wanted dentry, it exists. */
    if (vfs_dentries[i].ro.d_parent == parent &&
        strcmp(vfs_dentries[i].d_name, name) == 0) {
      /* Increment the counter. */
      vfs_dentries[i].ro.d_count ++;
      return vfs_dentries + i;
    }
    /* This is not the one we want but it is a mountpoint, thus it is
     * untouchable. */
    if (vfs_dentries[i].ro.d_mnt_sb != NULL) {
      continue;
    }
    /* This is just normal dentry. Check whether it has the lowest reference
     * counter. */
    if (lowest_index == -1 || vfs_dentries[i].ro.d_count < lowest_count) {
      lowest_index = i;
      lowest_count = vfs_dentries[i].ro.d_count;
    }
  }

  /* If we got here the entry was not found, thus we need to create a new
   * one. */
  if (lowest_index == -1) {
    /* What? Do we really reached the VFS_MAX_DENTRIES mountpoints? How? */
    set_errno(E_LIMIT);
    return NULL;
  }

  /* Ok, set the dentry. */
  d = vfs_dentries + lowest_index;
  vfs_dentry_reset(d);

  d->d_name = (char *)kalloc(strlen(name) + 1);
  if (d->d_name == NULL) {
    set_errno(E_NOMEM);
    return NULL;
  }
  strcpy(d->d_name, name);
  d->ro.d_parent = parent;

  /* Set the superblock this dentry belongs to. */
  if (d->ro.d_parent == NULL) {
    /* This is "/". */
    d->ro.d_sb = NULL;
  }
  else if (d->ro.d_parent->ro.d_mnt_sb == NULL) {
    /* Parent dentry is not a mountpoint, therefore we belong to the same
     * superblock. */
    d->ro.d_sb = d->ro.d_parent->ro.d_sb;
  }
  else {
    /* Parent is a mountpoint, therefore this entry's superblock is the
     * mounted by parent. */
     d->ro.d_sb = d->ro.d_parent->ro.d_mnt_sb;
  }

  /* And since someone requested it, it's count is 1, right? */
  d->ro.d_count = 1;

  return d;
}


/* When a superblock is unmounted all dentries belonging to this superblock
 * must be removed. */
static int vfs_dentry_unmount_sb(vfs_sb_t *sb) {
  int i;

  /* First, check whether we can unmount the superblock. */
  for (i = 0; i < VFS_MAX_DENTRIES; i ++) {
    if (vfs_dentries[i].d_name == NULL) {
      continue;
    }
    if (vfs_dentries[i].ro.d_mnt_sb != NULL &&
        vfs_dentries[i].ro.d_sb == sb) {
      return -1;
    }
  }

  /* Then do the unmount. */
  for (i = 0; i < VFS_MAX_DENTRIES; i ++) {
    if (vfs_dentries[i].d_name == NULL) {
      continue;
    }
    if (vfs_dentries[i].ro.d_sb == sb) {
      vfs_dentry_reset(vfs_dentries + i);
    }
  }

  return 0;
}

/*****************************************************************************/
/* vnodes ********************************************************************/
/*****************************************************************************/

/* We won't use a static approach for vnodes because vnodes are larger
 * structures. However, we must put some limits here. */
#define VFS_MAX_VNODES        1024

/* VNodes. */
static list_t vfs_vnodes;

/* vnode key in the cache. */
typedef struct vfs_vnode_key {
  vfs_sb_t    * v_sb;   /* Superblock this vnode belongs to. */
  int           v_no;   /* vnode number. */
} vfs_vnode_key_t;

/* vnodes comparison function to be used by the list. */
static int vfs_vnodes_cmp(void *item, void *key) {
  vfs_vnode_t *n;
  vfs_vnode_key_t *k;

  n = (vfs_vnode_t *)item;
  k = (vfs_vnode_key_t *)key;

  return n->v_no == k->v_no && n->ro.v_sb == k->v_sb;
}

/* vnodes lookup. */
static vfs_vnode_t * vfs_vnode_lookup(vfs_sb_t *sb, int v_no) {
  vfs_vnode_key_t k;
  k.v_sb = sb;
  k.v_no = v_no;
  return list_find(&vfs_vnodes, vfs_vnodes_cmp, &k);
}

/* Creates an empty vnode, not registered in the cache. We need this because
 * the create operation requires a vnode with no vnode number set, thus we
 * must provide a sort of temporary vnode which is not yet registered in the
 * cache. */
static vfs_vnode_t * vfs_vnode_prealloc(vfs_sb_t *sb) {
  vfs_vnode_t *v;

  v = (vfs_vnode_t *)kalloc(sizeof(vfs_vnode_t));
  if (v == NULL) {
    set_errno(E_NOMEM);
    return NULL;
  }

  v->v_no = 0;
  v->v_mode = 0;
  v->v_size = 0;
  v->v_dev = FILE_NODEV;

  /* TODO: Provide generic implementations if applicable. */
  v->v_iops.lookup = NULL;
  v->v_iops.create = NULL;
  v->v_iops.mkdir = NULL;
  v->v_iops.mknod = NULL;

  v->v_fops.open = NULL;
  v->v_fops.release = NULL;
  v->v_fops.flush = NULL;
  v->v_fops.read = NULL;
  v->v_fops.write = NULL;
  v->v_fops.lseek = NULL;
  v->v_fops.ioctl = NULL;

  v->ro.v_sb = sb;
  v->ro.v_count = 0;

  /* Since this node has no vnode number the filesystem might not be ready
   * to set private_data, that's why we just set it to NULL. Later superblock
   * operations like create or read will fill this field. */
  v->private_data = NULL;

  return v;
}

/* Registers a vnode in the vnode cache. */
static int vfs_vnode_alloc(vfs_vnode_t *node) {
  return list_add(&vfs_vnodes, node);
}

/* Destroys the vnode and takes it out the vnodes list if it was already there.
 * This allows destroying a node that has only been preallocated. */
static int vfs_vnode_dealloc(vfs_vnode_t *node) {
  vfs_vnode_t *n;
  vfs_vnode_key_t k;

  /* Try to remove the node from the list. */
  k.v_no = node->v_no;
  k.v_sb = node->ro.v_sb;
  n = list_find_del(&vfs_vnodes, vfs_vnodes_cmp, &k);

  /* Just double check we didn't screw something somewhere sometime. */
  if (n != NULL && n != node) {
    set_errno(E_CORRUPT);
    return -1;
  }

  kfree(node);
  return 0;
}

/* Acquires a vnode. */
static int vfs_vnode_acquire(vfs_vnode_t *node) {
  node->ro.v_count ++;
  return 0;
}

/* Releases a vnode. If the reference counter reaches zero the node will be
 * destroyed. */
static int vfs_vnode_release(vfs_vnode_t *node) {
  node->ro.v_count --;

  /* Ok, this one has to be destroyed. */
  if (node->ro.v_count < 1) {
    /* Tell the superblock this node is being destroyed. */
    if (node->ro.v_sb->sb_ops.destroy_vnode(node->ro.v_sb, node) == -1) {
      set_errno(E_IO);
      return -1;
    }
    /* And destroy it. */
    return vfs_vnode_dealloc(node);
  }
  return 0;
}

/* Load a vnode from cache or from superblock. This adds the node to the list
 * of nodes and acquires it. */
static vfs_vnode_t * vfs_vnode_get_or_read(vfs_sb_t *sb, int vno) {
  vfs_vnode_t *n;

  /* Look the node in the cache. */
  n = vfs_vnode_lookup(sb, vno);
  /* If not there, create it. */
  if (n == NULL) {
    /* Reserve space for the node. */
    n = vfs_vnode_prealloc(sb);
    if (n == NULL) {
      return NULL;
    }

    /* Set the required fields to ask the superblock to find it. */
    n->v_no = vno;
    n->ro.v_sb = sb;

    /* Ask the superblock to load it. */
    if (sb->sb_ops.read_vnode(sb, n) == -1) {
      vfs_vnode_dealloc(n);
      return NULL;
    }

    /* Register tne node in the cache. */
    if (vfs_vnode_alloc(n) == -1) {
      sb->sb_ops.destroy_vnode(sb, n);
      vfs_vnode_dealloc(n);
      return NULL;
    }
  }

  /* Acquire it. */
  vfs_vnode_acquire(n);
  return n;
}

/* vnodes comparison function to be used by the list when looking for a vnode
 * belonging to a given superblock. */
static int vfs_vnodes_sb_only_cmp(void *item, void *sb) {
  vfs_vnode_t *n;
  vfs_sb_t *s;

  n = (vfs_vnode_t *)item;
  s = (vfs_sb_t *)sb;

  return n->ro.v_sb == s;
}

/* Only checks whether there are vnodes left beloging the to-be-unmounted
 * superblock. */
static int vfs_vnode_unmount_sb(vfs_sb_t *sb) {
  if (list_find(&vfs_vnodes, vfs_vnodes_sb_only_cmp, sb) == NULL)
    return 0;
  return -1;
}



/*****************************************************************************/
/* Lookup ********************************************************************/
/*****************************************************************************/

/* Inspects a dentry looking for a name inside it. */
static vfs_dentry_t * vfs_lookup_in_dentry(vfs_dentry_t *dir, char *name) {
  vfs_vnode_t *dir_node;
  vfs_dentry_t *obj;
  int err;

  /* First, scan the dentry cache in case it's already loaded. */
  obj = vfs_dentry_get(dir, name);
  if (obj == NULL) {
    set_errno(E_LIMIT);
    return NULL;
  }

  /* If a dentry was fount then it has a node number, whether it be a normal
   * dentry or a mountpoint because mountpoints must exist as directories in
   * their own superblocks. */
  if (obj->d_vno != 0) {
    return obj;
  }

  /* From now on obj is a new dentry, so we can safely reset it when we need
   * it no more. */

  /* Load the node associated with the parent dentry. */
  if (dir->ro.d_mnt_sb == NULL) {
    /* A normal dentry can directly use the dentry vno. */
    dir_node = vfs_vnode_get_or_read(dir->ro.d_sb, dir->d_vno);
  }
  else {
    /* A mounpoint must use the mounted superblock and its root vno. */
    dir_node = vfs_vnode_get_or_read(dir->ro.d_mnt_sb,
                                     dir->ro.d_mnt_sb->sb_root_vno);
  }
  /* If the node doesn't exist just quit. */
  if (dir_node == NULL) {
    vfs_dentry_reset(obj);
    set_errno(E_CORRUPT);
    return NULL;
  }

  /* Check the node is a directory. */
  if (FILE_TYPE(dir_node->v_mode) != FILE_TYPE_DIRECTORY) {
    vfs_dentry_reset(obj);
    vfs_vnode_release(dir_node);
    set_errno(E_NODIR);
    return NULL;
  }

  /* Ask the node to lookup a dentry with the given name. */
  if (dir_node->v_iops.lookup(dir_node, obj) == -1) {
    err = get_errno();
    vfs_dentry_reset(obj);
    vfs_vnode_release(dir_node);
    set_errno(err);
    return NULL;
  }

  /* It seems everything's fine, so we need the vnode no more. */
  vfs_vnode_release(dir_node);
  return obj;
}

/* Looks up a path. */
static vfs_dentry_t * vfs_lookup(char *path) {
  char *tmp_path, *path_comp;
  vfs_dentry_t *obj;

  /* TODO: Do some sanitation to paths. We assume path starts with "/". */

  /* Clone the path to work with it but not not altering the path received. */
  tmp_path = (char *)kalloc(strlen(path) + 1);
  if (tmp_path == NULL) {
    set_errno(E_NOMEM);
    return NULL;
  }

  /* Load root vnode. */
  obj = vfs_root_dentry;
  path_comp = strtok(tmp_path, '/');

  /* Go down until you find a the objective. */
  while (path_comp != NULL) {
    obj = vfs_lookup_in_dentry(obj, path_comp);
    /* Not found or error. */
    if (obj == NULL) {
      kfree(tmp_path);
      return NULL;
    }
    path_comp = strtok(NULL, '/');
  }

  /* TODO: Here. */

  return NULL;
}

/*****************************************************************************/
/* Modules API ***************************************************************/
/*****************************************************************************/

/* Initialize the vfs. */
int vfs_init() {
  list_init(&vfs_fs_types);
  list_init(&vfs_sbs);
  list_init(&vfs_vnodes);
  list_init(&vfs_files);
  memset(&vfs_dentries, 0, sizeof(vfs_dentry_t) * VFS_MAX_DENTRIES);

  vfs_root_dentry = NULL;

  return 0;
}

/*****************************************************************************/
/* Public VFS API ************************************************************/
/*****************************************************************************/
int vfs_mount(dev_t devid, char *path, char *fs_type) {
  vfs_fs_type_t *ft;
  vfs_vnode_t *n;
  vfs_sb_t *sb;
  vfs_dentry_t *d;

  /* Check the validity of the path to be used as mountpoint. */

  if (vfs_root_dentry == NULL) {
    /* Nothing is mounted on "/", therefore nothing is mounted. */
    if (strcmp(path, "/") != 0) {
      /* Trying to mount something not in "/" with no mounted "/". */
      set_errno(E_NOROOT);
      return -1;
    }
    else {
      /* Trying to mount something is "/" when "/" is not mounted. */
      d = vfs_dentry_get(NULL, "/");
      if (d == NULL) {
        set_errno(E_NOMEM);
        return -1;
      }
    }
  }
  else {
    /* There exists something mounted on "/". */
    if (strcmp(path, "/") != 0) {
      /* Find the dentry this path resolves to because it is not "/". */
      d = vfs_lookup(path);
      if (d == NULL) {
        set_errno(E_NOENT);
        return -1;
      }
      /* Check this dentry is not already a mountpoint. */
      if (d->ro.d_mnt_sb != NULL) {
        set_errno(E_ACCESS);  /* TODO: Eventually let's remove this stupid
                               *       restriction. */
        return -1;
      }
      /* Get the node associated to this dentry. */
      n = vfs_vnode_get_or_read(d->ro.d_sb, d->d_vno);
      if (n == NULL) {
        set_errno(E_CORRUPT);
        return -1;
      }
      /* Check it's type: we can only allow mounting partitions on
       * directories. */
      if (FILE_TYPE(n->v_mode) != FILE_TYPE_DIRECTORY) {
        set_errno(E_NODIR);
        return -1;
      }
      /* If we got here, d is a valid mountpoint. */
    }
    else {
      /* We want to remount "/". */
      /* TODO: Next step is to do this. */
      set_errno(E_NOTIMP);
      return -1;
    }
  }

  /* Get the filesystem type. */
  ft = vfs_fs_type_lookup(fs_type);
  if (ft == NULL) {
    vfs_dentry_reset(d);
    set_errno(E_NOKOBJ);
    return -1;
  }

  /* Check the superblock is not already mounted. */
  sb = vfs_sb_lookup(devid);
  if (sb != NULL) {
    vfs_dentry_reset(d);
    set_errno(E_MOUNTED);
    return -1;
  }

  /* Prepare a superblock. */
  sb = vfs_sb_alloc(devid);
  if (sb == NULL) {
    vfs_dentry_reset(d);
    set_errno(E_NOMEM);
    return -1;
  }

  /* Probe the filesystem type to handle this device. */
  if (ft->ft_ops.ft_get_sb(sb) == -1) {
    vfs_sb_dealloc(sb);
    vfs_dentry_reset(d);
    set_errno(E_INVFS);
    return -1;
  }

  /* Link together the fs_type and its sb. */
  sb->ro.sb_fs_type = ft;

  /* Attempt mount. */
  if (sb->sb_ops.mount(sb) == -1) {
    ft->ft_ops.ft_kill_sb(sb);
    vfs_sb_dealloc(sb);
    vfs_dentry_reset(d);
    set_errno(E_IO);
    return -1;
  }

  /* Link dentry and mountpoint. */
  d->ro.d_mnt_sb = sb;

  /* TODO: This is not necessarily the best approach. */
  if (d->ro.d_parent == NULL) {
    vfs_root_dentry = d;
  }

  return 0;
}

/* Unmounts a mounted superblock. */
int vfs_sb_unmount(vfs_sb_t *sb) {
  vfs_dentry_t *mp;

  /* Check it is mounted. */
  if (!VFS_SB_IS_MOUNTED(sb)) {
    set_errno(E_NOTMOUNTED);
    return -1;
  }

  /* Tell the vnodes table to unmount. It will fail if there is still a open
   * file there. */
  if (vfs_vnode_unmount_sb(sb) == -1) {
    set_errno(E_CORRUPT);
    return -1;
  }

  /* Tell the dentries cache to invalidate all dentries belonging to sb. Notice
   * that the dentry pointing to is doesn't belong to sb but to the parent sb.
   * Thus it doesn't need to be invalidated because it exists as a directory
   * in its own superblock. */
  if (vfs_dentry_unmount_sb(sb) == -1) {
    set_errno(E_BUSY);
    return -1;
  }

  /* Inform the superblock it will be unmounted. */
  if (sb->sb_ops.unmount(sb) == -1) {
    set_errno(E_IO);
    return -1;
  }

  /* Get the mountpoint and remove the mountpoint field. */
  mp = sb->ro.sb_mnt;
  mp->ro.d_mnt_sb = NULL;

  /* Unmount the sb.  */
  sb->sb_flags = VFS_SB_F_UNUSED;
  sb->ro.sb_mnt = NULL;

  return 0;
}
