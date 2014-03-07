/*
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * not as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does not
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include "compat.h"

#define RAMFS_MAGIC 0x858458f6

struct inode *ramfs_get_inode(struct super_block *sb, struct inode *dir,
                umode_t mode, dev_t dev);
extern struct dentry *ramfs_mount(struct file_system_type *fs_type,
                int flags, const char *dev_name, void *data);

extern int __init init_ramfs_fs(void);

int ramfs_fill_super(struct super_block *sb, void *data, int silent);

#define RAMFS_DEFAULT_MODE	0755

static struct super_operations ramfs_ops;
static struct inode_operations ramfs_dir_inode_operations;

static struct backing_dev_info ramfs_backing_dev_info = {
        .name		= "ramfs",
        .ra_pages	= 0,	/* No readahead */
        .capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK |
                BDI_CAP_MAP_DIRECT | BDI_CAP_MAP_COPY |
                BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP,
};

struct address_space_operations ramfs_aops = {
        .readpage	= simple_readpage,
        .write_begin	= simple_write_begin,
        .write_end	= simple_write_end,
        .set_page_dirty = __set_page_dirty_no_writeback,
};

struct file_operations ramfs_file_operations = {
        .read		= do_sync_read,
        .aio_read	= generic_file_aio_read,
        .write		= do_sync_write,
        .aio_write	= generic_file_aio_write,
        .mmap		= generic_file_mmap,
        .fsync		= noop_fsync,
        .splice_read	= generic_file_splice_read,
        .splice_write	= generic_file_splice_write,
        .llseek		= generic_file_llseek,
};

struct inode_operations ramfs_file_inode_operations = {
        .setattr	= simple_setattr,
        .getattr	= simple_getattr,
};

struct inode *ramfs_get_inode(struct super_block *sb,  struct inode *dir, umode_t mode, dev_t dev)
{
        struct inode * inode = new_inode(sb);

        if (inode) {
                inode->i_ino = get_next_ino();
                inode_init_owner(inode, dir, mode);
                inode->i_mapping->a_ops = &ramfs_aops;
                inode->i_mapping->backing_dev_info = &ramfs_backing_dev_info;
                mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
                mapping_set_unevictable(inode->i_mapping);
                inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
                switch (mode & S_IFMT) {
                default:
                        init_special_inode(inode, mode, dev);
                        break;
                case S_IFREG:
                        inode->i_op = &ramfs_file_inode_operations;
                        inode->i_fop = &ramfs_file_operations;
                        break;
                case S_IFDIR:
                        inode->i_op = &ramfs_dir_inode_operations;
                        inode->i_fop = &simple_dir_operations;

                        /* directory inodes start off with i_nlink == 2 (for "." entry) */
                        inc_nlink(inode);
                        break;
                case S_IFLNK:
                        inode->i_op = &page_symlink_inode_operations;
                        break;
                }
        }
        return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
ramfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
        struct inode * inode = ramfs_get_inode(dir->i_sb, dir, mode, dev);
        int error = -ENOSPC;

        if (inode) {
                d_instantiate(dentry, inode);
                dget(dentry);	/* Extra count - pin the dentry in core */
                error = 0;
                dir->i_mtime = dir->i_ctime = CURRENT_TIME;
        }
        return error;
}

static int ramfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
        int retval = ramfs_mknod(dir, dentry, mode | S_IFDIR, 0);
        if (!retval)
                inc_nlink(dir);
        return retval;
}

static int ramfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
        (void) excl;
        return ramfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
        struct inode *inode;
        int error = -ENOSPC;

        inode = ramfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
        if (inode) {
                int l = strlen(symname)+1;
                error = page_symlink(inode, symname, l);
                if (!error) {
                        d_instantiate(dentry, inode);
                        dget(dentry);
                        dir->i_mtime = dir->i_ctime = CURRENT_TIME;
                } else
                        iput(inode);
        }
        return error;
}

static struct inode_operations ramfs_dir_inode_operations = {
        .create		= ramfs_create,
        .lookup		= simple_lookup,
        .link		= simple_link,
        .unlink		= simple_unlink,
        .symlink	= ramfs_symlink,
        .mkdir		= ramfs_mkdir,
        .rmdir		= simple_rmdir,
        .mknod		= ramfs_mknod,
        .rename		= simple_rename,
};

static struct super_operations ramfs_ops = {
        .statfs		= simple_statfs,
        .drop_inode	= generic_delete_inode,
        .show_options	= generic_show_options,
};

struct ramfs_mount_opts {
        umode_t mode;
};

struct ramfs_fs_info {
        struct ramfs_mount_opts mount_opts;
};

int ramfs_fill_super(struct super_block *sb, void *data, int silent)
{
        struct ramfs_fs_info *fsi;
        struct inode *inode;

        (void) silent;
        save_mount_options(sb, data);

        fsi = kzalloc(sizeof(struct ramfs_fs_info), GFP_KERNEL);
        sb->s_fs_info = fsi;
        if (!fsi)
                return -ENOMEM;

        fsi->mount_opts.mode = 0040755;

        sb->s_maxbytes		= MAX_LFS_FILESIZE;
        sb->s_blocksize		= PAGE_CACHE_SIZE;
        sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
        sb->s_magic		= RAMFS_MAGIC;
        sb->s_op		= &ramfs_ops;
        sb->s_time_gran		= 1;

        inode = ramfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
        sb->s_root = d_make_root(inode);
        if (!sb->s_root)
                return -ENOMEM;

        return 0;
}

struct dentry *ramfs_mount(struct file_system_type *fs_type,
                int flags, const char *dev_name, void *data)
{
        (void) dev_name;
        return mount_nodev(fs_type, flags, data, ramfs_fill_super);
}

static void ramfs_kill_sb(struct super_block *sb)
{
        kfree(sb->s_fs_info);
        kill_litter_super(sb);
}

static struct file_system_type ramfs_fs_type = {
        .name		= "ramfs",
        .mount		= ramfs_mount,
        .kill_sb	= ramfs_kill_sb,
        .fs_flags	= FS_USERNS_MOUNT,
};

int __init init_ramfs_fs(void)
{
        static unsigned long once;
        int err;

        if (test_and_set_bit(0, &once))
                return 0;

        err = bdi_init(&ramfs_backing_dev_info);
        if (err)
                return err;

        err = register_filesystem(&ramfs_fs_type);
        if (err)
                bdi_destroy(&ramfs_backing_dev_info);

        return err;
}

void exit_ramfs_fs(void)
{
        printk("[ramfs] exit\n");
}

module_init(init_ramfs_fs);
module_exit(exit_ramfs_fs);
