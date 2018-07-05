/** \file user/fd.h
 *
 * Defines some macros and structures related to user level file system.
 */
#ifndef _USER_FD_H_
#define _USER_FD_H_ 1

#include <types.h>
#include <fs.h>

// pre-declare for forward references
struct Fd;
struct Stat;
struct Dev;

struct Dev
{
	int dev_id;
	char *dev_name;
	int (*dev_read)(struct Fd*, void*, u_int, u_int);
	int (*dev_write)(struct Fd*, const void*, u_int, u_int);
	int (*dev_close)(struct Fd*);
	int (*dev_stat)(struct Fd*, struct Stat*);
	int (*dev_seek)(struct Fd*, u_int);
};

/** File descriptor.
 *
 * Manage the device id where the file is on, the offset and the open mode.
 */
struct Fd
{
	/** Device id where the file is on.
	 */
	u_int fd_dev_id;
	/** Offset of the file.
	 *
	 * Indicate the location at which an operation should begin.
	 */
	u_int fd_offset;
	/** %Open mode indicating which operation can be done on the file.
	 */
	u_int fd_omode;
};

struct Stat
{
	char st_name[MAXNAMELEN];
	u_int st_size;
	u_int st_isdir;
	struct Dev *st_dev;
};

/** Simple wrap of Fd and File, with file id.
 */
struct Filefd 
{
	struct Fd f_fd; //!< Fd structure for the file.
	u_int f_fileid; //!< %File id.
	struct File f_file; //!< File structure for the file.
};

int fd_alloc(struct Fd **fd);
int fd_lookup(int fdnum, struct Fd **fd);
u_int fd2data(struct Fd*);
int fd2num(struct Fd*);
int dev_lookup(int dev_id, struct Dev **dev);
int
num2fd(int fd);
extern struct Dev devcons;
extern struct Dev devfile;
extern struct Dev devpipe;


#endif
