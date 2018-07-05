/** \file include/fs.h
 *
 * Define some general macros and structures for file system.
 */
// See COPYRIGHT for copyright information.

#ifndef _FS_H_
#define _FS_H_ 1

#include <types.h>

// File nodes (both in-memory and on-disk)

// Bytes per file system block - same as page size
#define BY2BLK		BY2PG //!< Bytes of a block.
#define BIT2BLK		(BY2BLK*8) //!< Bits of a block.

// Maximum size of a filename (a single path component), including null
/// Maximum size of a filename (a single path component), including null.
#define MAXNAMELEN	128

// Maximum size of a complete pathname, including null
/// Maximum size of a complete pathname, including null.
#define MAXPATHLEN	1024

// Number of (direct) block pointers in a File descriptor
/// Number of direct block pointers in a File structure.
#define NDIRECT		10
/// Number of all block pointers in a File structure.
#define NINDIRECT	(BY2BLK/4)

/// Maximum size of a file.
#define MAXFILESIZE	(NINDIRECT*BY2BLK)

/// Bytes of a File structure.
#define BY2FILE 256

/** Describe some basic information of a file.
 */
struct File {
	/** %File name.
	 */
	u_char f_name[MAXNAMELEN];	// filename
	/** %File size.
	 */
	u_int f_size;			// file size in bytes
	/** %File type.
	 */
	u_int f_type;			// file type
	/** Direct block pointers.
	 *
	 * __What can be its content? On disk, it is disk block number.__
	 */
	u_int f_direct[NDIRECT];
	/** Indirect block directory pointer.
	 *
	 * __What can be its content? On disk, it is disk block number.__
	 */
	u_int f_indirect;

	/** Address of the parent directory.
	 *
	 * __The field is only valid in memory.__
	 */
	struct File *f_dir;		// valid only in memory
	/** Padding to make size be BY2FILE bytes.
	 */
	u_char f_pad[256-MAXNAMELEN-4-4-NDIRECT*4-4-4];
};

/** Number of File structure in a block.
 */
#define FILE2BLK	(BY2BLK/sizeof(struct File))

// File types
#define FTYPE_REG		0	//!< Regular file
#define FTYPE_DIR		1	//!< Directory


// File system super-block (both in-memory and on-disk)

/** %File system's magic number.
 */
#define FS_MAGIC	0x68286097	// Everyone's favorite OS class

/** Structure representing a super block.
 */
struct Super {
	u_int s_magic;		//!< Magic number: FS_MAGIC.
	u_int s_nblocks;	//!< Total number of blocks on disk.
	struct File s_root;	//!< Root directory File structure.
};

// Definitions for requests from clients to file system

#define FSREQ_OPEN	1
#define FSREQ_MAP	2
#define FSREQ_SET_SIZE	3
#define FSREQ_CLOSE	4
#define FSREQ_DIRTY	5
#define FSREQ_REMOVE	6
#define FSREQ_SYNC	7

struct Fsreq_open {
	char req_path[MAXPATHLEN];
	u_int req_omode;
};

struct Fsreq_map {
	int req_fileid;
	u_int req_offset;
};

struct Fsreq_set_size {
	int req_fileid;
	u_int req_size;
};

struct Fsreq_close {
	int req_fileid;
};

struct Fsreq_dirty {
	int req_fileid;
	u_int req_offset;
};

struct Fsreq_remove {
	u_char req_path[MAXPATHLEN];
};

#endif // _FS_H_
