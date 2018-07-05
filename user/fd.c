/** \file user/fd.c
 *
 * Implement operations on file descriptor.
 */

#include "lib.h"
#include "fd.h"
#include <mmu.h>
#include <env.h>

#define debug 0

/** Maximum number of file descriptor.
 */
#define MAXFD 32
/** Base address of the data regions of files opend by a process.
 */
#define FILEBASE 0x60000000
/** Base address of file descriptor table owned by a process.
 */
#define FDTABLE (FILEBASE-PDMAP)

/** Convert index of a Fd to its address.
 */
#define INDEX2FD(i)	(FDTABLE+(i)*BY2PG)
/** Convert index of a Fd to its data region's address.
 */
#define INDEX2DATA(i)	(FILEBASE+(i)*PDMAP)

/** Array of device descriptors.
 */
static struct Dev *devtab[] =
{
	&devfile,
	&devcons,
	&devpipe,
	0
};

/** Find a device by dev_id.
 *
 * If succeed, set *dev pointing the Dev of the device and return `0`,
 * otherwise, return `-#E_INVAL`.
 *
 * @param[in] dev_id Device id.
 * @param[out] dev Address of the pointor to the result device.
 */
int
dev_lookup(int dev_id, struct Dev **dev)
{
	int i;
	//writef("dev_lookup()VVVVVVVVVVVVVVVVVVVVVV\n");
	for (i=0; devtab[i]; i++)
		if (devtab[i]->dev_id == dev_id) {
			*dev = devtab[i];
			return 0;
		}
	writef("[%08x] unknown device type %d\n", env->env_id, dev_id);
	return -E_INVAL;
}

/** Allocate a free file descriptor.
 *
 * The file descriptor who has the smallest index among free file descriptors
 * will be allocated. If such one exists, set *fd pointing to the address of
 * the page for the file descriptor, otherwise, return `-#E_MAX_OPEN`.
 *
 * \note The function __does not__ allocate a page to the file descriptor. It
 * is caller's responsibility.
 *
 * @param[out] fd Address of the pointor to the result file descriptor.
 */
int
fd_alloc(struct Fd **fd)
{
	// Your code here.
	//
	// Find the smallest i from 0 to MAXFD-1 that doesn't have
	// its fd page mapped.  Set *fd to the fd page virtual address.
	// (Do not allocate a page.  It is up to the caller to allocate
	// the page.  This means that if someone calls fd_alloc twice
	// in a row without allocating the first page we return, we'll
	// return the same page the second time.)
	// Return 0 on success, or an error code on error.
	u_int va;
	u_int fdno;
//writef("enter fd_alloc\n");	
	for(fdno = 0;fdno < MAXFD;fdno++)
	{
		va = INDEX2FD(fdno);
		if(((* vpd)[va/PDMAP] & PTE_V)==0)
		{
			*fd = va;
			return 0;
		}
//writef("fd_alloc:va = %x\n",va);
		if(((* vpt)[va/BY2PG] & PTE_V)==0)		//the fd is not used
		{
			*fd = va;
			return 0;
		}
	}
	//user_panic("fd_alloc not implemented");
	return -E_MAX_OPEN;
}

/** Close a file by unmapping the page for Fd of it.
 *
 * \note The function just close the file 'locally'. It is caller's
 * responsbility to do a device dependent close operation.
 *
 * @param[in] fd Address of the Fd to be closed.
 */
void
fd_close(struct Fd *fd)
{
	syscall_mem_unmap(0, (u_int)fd);
}

/** Find a currently used Fd by its index.
 *
 * If fdnum is greater than #MAXFD, or the file descriptor is not used now,
 * return `-#E_INVAL`.
 *
 * @param[in] fdnum Index of the Fd.
 * @param[out] fd Address of the pointor to the result Fd.
 */
int
fd_lookup(int fdnum, struct Fd **fd)
{
	// Your code here.
	// 
	// Check that fdnum is in range and mapped.  If not, return -E_INVAL.
	// Set *fd to the fd page virtual address.  Return 0.
	u_int va;
	
	if(fdnum >=MAXFD)
		return -E_INVAL;

	va = INDEX2FD(fdnum);
	if(((* vpt)[va/BY2PG] & PTE_V)!=0)		//the fd is used
	{
		*fd = va;
		return 0;
	}
	//user_panic("fd_lookup not implemented");
	return -E_INVAL;
}

/** Return the address of the begining of the data region of the Fd.
 *
 * \note If fd is not 4K-aligned, the address of the Fd where fd is in its 4K memory
 * region will be returned.
 *
 * \pre #FDTABLE <= fd < #FILEBASE
 *
 * @param[in] fd Address of a Fd.
 */
u_int
fd2data(struct Fd *fd)
{
	return INDEX2DATA(fd2num(fd));
}

/** Return the index of a Fd specified by its address.
 *
 * \note If fd is not 4K-aligned, the address of the Fd where fd is in its 4K memory
 * region will be returned.
 *
 * @param[in] fd Address of a Fd.
 */
int
fd2num(struct Fd *fd)
{
	return ((u_int)fd - FDTABLE)/BY2PG;
}

/** Return the address of a Fd specified by its index.
 *
 * \pre 0 <= fd < #MAXOPEN
 *
 * @param[in] fd Index of a Fd.
 */
int
num2fd(int fd)
{
	return fd*BY2PG+FDTABLE;
}

/** Close the file described by a Fd specified by its index.
 *
 * The function closes the file descriptor and calls a device dependent
 * function --actually, it is an interface.-- to notify the device the close
 * operation.
 *
 * Return a number to indicate the result.
 *
 * @param[in] fdnum Index of a Fd.
 */
int
close(int fdnum)
{
	int r;
	struct Dev *dev;
	struct Fd *fd;

	if ((r = fd_lookup(fdnum, &fd)) < 0
	||  (r = dev_lookup(fd->fd_dev_id, &dev)) < 0)
		return r;
	r = (*dev->dev_close)(fd);
	fd_close(fd);
	return r;
}

/** Close all files the process currently opens.
 */
void
close_all(void)
{
	int i;

	for (i=0; i<MAXFD; i++)
		close(i);
}

/** Duplicate a Fd specified by its index.
 *
 * The function first close the file represented by newfdnum, and then map
 * newfd's memory regions to the same as oldfdnum's.
 *
 * On success, return newfdnum.
 *
 * If some error occur, return an error code and all memory regions newfdnum
 * maps will be unmapped.
 *
 * \pre 0 <= oldfnum, newfdnum < MAXOPEN
 *
 * \pre oldfnum != newfdnum
 *
 * @param[in] oldfdnum The index of the Fd to be copied.
 * @param[in] newfdnum The index of the Fd to be overwrote.
 */
int
dup(int oldfdnum, int newfdnum)
{
	int i, r;
	u_int ova, nva, pte;
	struct Fd *oldfd, *newfd;
	//writef("dup comes 1;\n");
	if ((r = fd_lookup(oldfdnum, &oldfd)) < 0)
		return r;
	//writef("dup comes 2;\n");
	newfd = (struct Fd*)INDEX2FD(newfdnum);
	ova = fd2data(oldfd);
	nva = fd2data(newfd);
	close(newfdnum);

//writef("dup comes 2.5;\n");
	if ((* vpd)[PDX(ova)]) {
		for (i=0; i<PDMAP; i+=BY2PG) {
			pte = (* vpt)[VPN(ova+i)];
			if(pte&PTE_V) {
				// should be no error here -- pd is already allocated
				if ((r = syscall_mem_map(0, ova+i, 0, nva+i, pte&(PTE_V|PTE_R|PTE_LIBRARY))) < 0)
					goto err;
			}
		}
	}
	if ((r = syscall_mem_map(0, (u_int)oldfd, 0, (u_int)newfd, ((*vpt)[VPN(oldfd)])&(PTE_V|PTE_R|PTE_LIBRARY))) < 0)
		goto err;
//writef("dup comes 3;\n");
	return newfdnum;

err:
//writef("dup comes 4;\n");
	syscall_mem_unmap(0, (u_int)newfd);
	for (i=0; i<PDMAP; i+=BY2PG)
		syscall_mem_unmap(0, nva+i);
	return r;
}

/** Read at most n bytes from a file specified by its descriptor's index.
 *
 * Call a device dependent function, actually an interface, to read n bytes and
 * save it to buf.
 *
 * Return an int indicating the result. If it is non-negative, means the number
 * of the bytes the function reads, otherwise, means that some error occurs.
 *
 * @param[in] fdnum Index of the file to be read.
 * @param[out] buf Address of the buffer to store result.
 * @param[in] n Maximum number of bytes to be read.
 */
int
read(int fdnum, void *buf, u_int n)
{
	int r;
	struct Dev *dev;
	struct Fd *fd;
	//writef("read() come 1 %x\n",(int)env);
	if ((r = fd_lookup(fdnum, &fd)) < 0
	||  (r = dev_lookup(fd->fd_dev_id, &dev)) < 0)
		return r;
	//writef("read() come 2 %x\n",(int)env);
	if ((fd->fd_omode & O_ACCMODE) == O_WRONLY) {
		writef("[%08x] read %d -- bad mode\n", env->env_id, fdnum); 
		return -E_INVAL;
	}
	//writef("read() come 3 %x\n",(int)env);
	r = (*dev->dev_read)(fd, buf, n, fd->fd_offset);
	if (r >= 0)
		fd->fd_offset += r;
	//writef("read() come 4 %x\n",(int)env);
	return r;
}

/** Read exactly n bytes from a file specified by its descriptor's index.
 *
 * Continual read the file via read(), until an error occurs or 0 bytes were
 * read in a turn.
 *
 * If no error occurs, return an int representing the actual number of bytes
 * read, otherwise, return an error code.
 *
 * \note The function is designed to handle devices behaving like char devices,
 * such as a console.
 *
 * \warning Although some error occur, buf may contain some bytes which have
 * been read.
 *
 * @param[in] fdnum Index of the file to be read.
 * @param[out] buf Address of the buffer to store result.
 * @param[in] n Maximum number of bytes to be read.
 */
int
readn(int fdnum, void *buf, u_int n)
{
	int m, tot;

	for (tot=0; tot<n; tot+=m) {
		m = read(fdnum, (char*)buf+tot, n-tot);
		//writef("m:%d\n", m);
		if (m < 0)
			return m;
		if (m == 0)
			break;
	}
	return tot;
}

/** Write n bytes to a file specified by its descriptor's index.
 *
 * Call a device dependent function, actually an interface, to wrtie n bytes
 * from buf to a file.
 *
 * Return an int indicating the result. If it is non-negative, means the number
 * of the bytes the function writes, otherwise, means that some error occurs.
 *
 * @param[in] fdnum Index of the file to be wrote.
 * @param[out] buf Address of the buffer to be flushed.
 * @param[in] n Maximum number of bytes to be wrote.
 */
int
write(int fdnum, const void *buf, u_int n)
{
	int r;
	struct Dev *dev;
	struct Fd *fd;
	//writef("write comes 1\n");
	if ((r = fd_lookup(fdnum, &fd)) < 0
	||  (r = dev_lookup(fd->fd_dev_id, &dev)) < 0)
		return r;
//writef("write comes 2\n");
	if ((fd->fd_omode & O_ACCMODE) == O_RDONLY) {
		writef("[%08x] write %d -- bad mode\n", env->env_id, fdnum);
		return -E_INVAL;
	}
//writef("write comes 3\n");
	if (debug) writef("write %d %p %d via dev %s\n",
		fdnum, buf, n, dev->dev_name);
	r = (*dev->dev_write)(fd, buf, n, fd->fd_offset);
	//writef("write return\n");
	if (r > 0)
		fd->fd_offset += r;
//writef("write comes 4\n");
	return r;
}

/** Set the offset of a file specified by the index of its file descriptor.
 *
 * On success, return `0`, otherwise, return an error code.
 *
 * \pre 0 <= offset < file's size
 *
 * @param[in] fdnum Index of a Fd structure.
 * @param[in] offset New offset.
 */
int
seek(int fdnum, u_int offset)
{
	int r;
	struct Fd *fd;
	//writef("seek() come 1 %x\n",(int)env);
	if ((r = fd_lookup(fdnum, &fd)) < 0)
		return r;
	//writef("seek() come 2 %x\n",(int)env);
	fd->fd_offset = offset;
	//writef("seek() come 3 %x\n",(int)env);
	return 0;
}


/** Read an open file's status.
 *
 * Call a device dependent function, actually an interface, to get the status
 * information. Return an int indicating the result status.
 *
 * *stat will be initialized first.
 *
 * @param[in] fdnum Index of the file whose state to be read.
 * @param[out] stat Address of a Stat structure storing the result.
 */
int fstat(int fdnum, struct Stat *stat)
{
	int r;
	struct Dev *dev;
	struct Fd *fd;

	if ((r = fd_lookup(fdnum, &fd)) < 0
	||  (r = dev_lookup(fd->fd_dev_id, &dev)) < 0)
		return r;
	stat->st_name[0] = 0;
	stat->st_size = 0;
	stat->st_isdir = 0;
	stat->st_dev = dev;
	return (*dev->dev_stat)(fd, stat);
}

/** Read an file's status.
 *
 * The function completes the task by doing the followings:
 *
 * 1. %Open the file with read-only access via open().
 * 2. Get the file's status via fstat().
 * 3. Close it via close().
 *
 * The function returns an int indicating the result status.
 *
 * @param[in] fdnum Index of the file whose state to be read.
 * @param[out] stat Address of a Stat structure storing the result.
 */
int
stat(const char *path, struct Stat *stat)
{
	int fd, r;

	if ((fd = open(path, O_RDONLY)) < 0)
		return fd;
	r = fstat(fd, stat);
	close(fd);
	return r;
}

