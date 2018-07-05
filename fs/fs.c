/** \file fs/fs.c
 *
 * Implement low level function of file system. 
 *
 * \warning Most of functions here can only be used by file system server.
 */

#include "fs.h"
#include <mmu.h>

struct Super *super;//!< Address of super block in memroy.

u_int nbitmap;		//!< Number of bitmap blocks.
u_int *bitmap;		//!< Address of bitmap blocks mapped in memory.

void file_flush(struct File*);
int block_is_free(u_int);

/** Return the virtual address of the disk block specified by blockno.
 *
 * \pre If super block has been loaded, blockno must be less than super->s_nblocks.
 *
 *@param[in] blockno Indicate a block on disk.
 */
u_int
diskaddr(u_int blockno)
{
	if(super && blockno >= super->s_nblocks)
		user_panic("bad block number %08x in diskaddr", blockno);
	return DISKMAP + blockno * BY2BLK;
}

/** Check whether the virtual address is mapped.
 *
 * Complete this check by looking up virtual page table.
 *
 * @param[in] va Virtual address to be checked.
 */
u_int
va_is_mapped(u_int va)
{
	//writef("va_mapped():va:%x vpd:%x vpt:%x vpd[PDX(va)]:%x vpt[VPN(va)]:%x\n",va,(int)vpd,(int)vpt,vpd[PDX(va)],vpt[VPN(va)]);
	return (((*vpd)[PDX(va)] & (PTE_V)) && ((*vpt)[VPN(va)] & (PTE_V)));
}

/** Check whether a block is mapped into memory.
 *
 * Complete this check by getting the virtual address where the block should be
 * mapped and checking whether it is actually mapped via va_is_mapped().
 *
 * @param[in] blockno Block number of the block to be checked.
 */
u_int
block_is_mapped(u_int blockno)
{
	u_int va;

	va = diskaddr(blockno);
	if (va_is_mapped(va))
		return va;
	return 0;
}

/** Check whether a page in memory is dirty.
 *
 * \warning The function is not implemented actually.
 *
 * @param[in] va Virtual address to be checked.
 */
u_int
va_is_dirty(u_int va)
{
	return 0;//we have no PTE_D//(* vpt)[VPN(va)]&PTE_D;
}

/** Check whether a block in memory is dirty.
 *
 * \warning The function is not implemented actually.
 *
 * @param[in] blockno Block number of the block to be checked.
 */
u_int
block_is_dirty(u_int blockno)
{
	u_int va;

	va = diskaddr(blockno);
	return va_is_mapped(va) && va_is_dirty(va);
}

/** Allocate a page for some block in memory.
 *
 * If there is a page for the block, do nothing, otherwise, allocate a page via
 * syscall_mem_alloc().
 *
 * @param[in] blockno Block number of the block which request a page.
 */
int
map_block(u_int blockno)
{
	if (block_is_mapped(blockno))
		return 0;
	return syscall_mem_alloc(0, diskaddr(blockno), PTE_V|PTE_R);
}

// Make sure a particular disk block is loaded into memory.
// Return 0 on success, or a negative error code on error.
// 
// If blk!=0, set *blk to the address of the block in memory.
//
// If isnew!=0, set *isnew to 0 if the block was already in memory,
//	or to 1 if the block was loaded off disk to satisfy this request.
//
// (Isnew lets callers like file_get_block clear any memory-only
// fields from the disk blocks when they come in off disk.)
//
// Hint: use diskaddr, block_is_mapped, syscall_mem_alloc, and ide_read.

/** Make sure a particular disk block is loaded into memory.
 *
 * If the block is currently in memory, do nothing, otherwise, load it from
 * disk into memory via ide_read().
 *
 * If blk is not zero, set *blk to the address of the block in memory.
 *
 * If isnew is not zero, set *isnew to 0 if the block was already in memroy,
 * otherwise to 1.
 *
 * isnew lets callers be able to clear any memory-only fields when the block is
 * first loaded into memroy.
 *
 * \pre If super block has been loaded, blockno must be less than
 * super->s_nblocks.
 *
 * \pre If bitmap has been loaded, the block indicated by blockno
 * must be free.
 *
 * @param[in] blockno Block number of the block to be loaded.
 * @param[out] blk Address of the pointor to block in memory.
 * @param[in] isnew Address of a indicator whether the block was not in memory
 * before.
 */
int
read_block(u_int blockno, void **blk, u_int *isnew)
{
	int r;
	u_int va;

	if (super && blockno >= super->s_nblocks)
		user_panic("reading non-existent block %08x\n", blockno);

	if (bitmap && block_is_free(blockno))
		user_panic("reading free block %08x\n", blockno);

	// Your code here
	va = diskaddr(blockno);

	if(blk) *blk = va;
	if(block_is_mapped(blockno))	//the block is in memory
	{
		if(isnew)
			*isnew = 0;
	}
	else				//the block is not in memory
	{
		if(isnew)
			*isnew = 1;
		syscall_mem_alloc(0, va, PTE_V|PTE_R);
//		writef("fs.c:read_block(): before ide_read() blockno:%x va:%x\n",blockno,va);
		ide_read(1, blockno*SECT2BLK, va, SECT2BLK);
	}
//	user_panic("read_block not implemented");
//	writef("fs.c:read_block(): end !\n");
	return 0;
}

// Copy the current contents of the block out to disk.
// Then clear the PTE_D bit using syscall_mem_map.
// The PTE_USER constant in inc/mmu.h may be useful!

/** Flush current contents of the block in memory out to the disk.
 *
 * \pre The block must have been mapped in memory.
 *
 * @param[in] blockno Block number of a block.
 */
void
write_block(u_int blockno)
{
	u_int va;

	if (!block_is_mapped(blockno))
		user_panic("write unmapped block %08x", blockno);
	// Your code here to write disk block and clear PTE_D.
	va = diskaddr(blockno);
//	writef("writeBLK():  blockno:%x  va:%x\n", blockno,va);
	ide_write(1, blockno*SECT2BLK, va, SECT2BLK);
	syscall_mem_map(0, va, 0, va, (PTE_V|PTE_R|PTE_LIBRARY));
//	user_panic("write_block not implemented");
}

// Make sure this block is unmapped.
/** Unmap a block from memory. 
 *
 * The function unmaps the block via syscall_mem_unmap().
 *
 * If the block is not in memory, do nothing.
 *
 * \pre If the block is in memory, the block must be either free or not dirty. 
 *
 * @param[in] blockno Block number of a block.
 */
void
unmap_block(u_int blockno)
{
	int r;

	if(!block_is_mapped(blockno))
		return;

	user_assert(block_is_free(blockno) || !block_is_dirty(blockno));

	if ((r = syscall_mem_unmap(0, diskaddr(blockno))) < 0)
		user_panic("unmap_block: syscall_mem_unmap: %e", r);
	user_assert(!block_is_mapped(blockno));
}

// Check to see if the block bitmap indicates that block 'blockno' is free.
// Return 1 if the block is free, 0 if not.
/** Check to see if the block bitmap indicates that block 'blockno' is free.
 *
 * Return `1` if the block is free, `0` if not.
 *
 * When super block is not loaded, all blocks are considered not free.
 * Nonexistents blocks are considered not free.
 *
 * @param[in] blockno Block number of a block.
 */
int
block_is_free(u_int blockno)
{
	if (super == 0 || blockno >= super->s_nblocks)
		return 0;
	if (bitmap[blockno / 32] & (1 << (blockno % 32)))
		return 1;
	return 0;
}

// Mark a block free in the bitmap
/** Mark a block free in the bitmap.
 *
 * The blockno should represent a valid block.
 *
 * Due to block 0th is the boot record of the disk, it cannot be freed.
 *
 * \pre blockno != 0
 *
 * @param[in] blockno Block number of a block.
 */
void
free_block(u_int blockno)
{
	// Blockno zero is the null pointer of block numbers.
	if (blockno == 0)
		user_panic("attempt to free zero block");
	bitmap[blockno / 32] |= 1<<(blockno % 32);
}

// Search the bitmap for a free block and allocate it.
// 
// Return block number allocated on success, `-#E_NO_DISK` if we are out of blocks.
/** Search the bitmap for a free block and allocate it.
 *
 * Return block number allocated and flush the bitmap out to disk on success,
 * `-#E_NO_DISK` if we are out of blocks.
 */
int
alloc_block_num(void)
{
	// Your code here.
	//user_panic("alloc_block_num not implemented");
	int blockno;

	for(blockno = 3;blockno < super->s_nblocks; blockno++)
	{
		if (bitmap[blockno / 32] & (1 << (blockno % 32)))	//the block is free 
		{
			bitmap[blockno / 32] &= ~(1 << (blockno % 32));
			write_block(blockno / BIT2BLK);
			return blockno;
		}
	}
	return -E_NO_DISK;
}

// Allocate a block -- first find a free block in the bitmap,
// then map it into memory.
/** Allocate a block.
 *
 * In specifc, the free block first found in the bitmap will be allocated, and
 * assure there is a page for it in memory.
 *
 * Return the block number on success, otherwise an error code. When error
 * occured, no block will be allocated actually.
 */
int
alloc_block(void)
{
	int r, bno;

	if ((r = alloc_block_num()) < 0)
		return r;
	bno = r;

	if ((r = map_block(bno)) < 0) {
		free_block(bno);
		return r;
	}
	return bno;
}

// Read and validate the file system super-block.
/** Read and validate the file system super block, block 1th.
 *
 * On success, set #super to point to the begining of the super block in memory.
 *
 * If error occured, the process panic.
 */
void
read_super(void)
{
	int r;
	void *blk;
//	writef("fs.c:read_super() 1\n");
	if ((r = read_block(1, &blk, 0)) < 0)
		user_panic("cannot read superblock: %e", r);
//	writef("fs.c:read_super() 2\n");
	super = blk;
	if (super->s_magic != FS_MAGIC)
		user_panic("bad file system magic number %x %x",super->s_magic,FS_MAGIC);

	if (super->s_nblocks > DISKMAX/BY2BLK)
		user_panic("file system is too large");

//	writef("superblock is good\n");
}

// Read and validate the file system bitmap.
//
// Read all the bitmap blocks into memory.
// Set the "bitmap" pointer to point ablocknot the beginning of the first bitmap block.
// 
// Check that they're all marked as inuse
// (for each block i, user_assert(!block_is_free(i))).
/** Read and validate the file system bitmap.
 *
 * Read all the bitmap blocks into memory. Set the bitmap pointer to point the
 * beginning of the first bitmap block.
 */
void
read_bitmap(void)
{
	int r;
	u_int i;
	void *blk;

	// Your code here
//	user_panic("read_bitmap not implemented");
//writef("nbitmap = %d,	nblocks = %d\n",nbitmap,super->s_nblocks);
	nbitmap = (super->s_nblocks + BIT2BLK - 1) / BIT2BLK;
	for(i = 0; i < nbitmap; i++)
		read_block(i + 2, blk, 0);
	bitmap = diskaddr(2);
	// Make sure the reserved and root blocks are marked in-use
	user_assert(!block_is_free(0));
	user_assert(!block_is_free(1));
	for(i = 0;i < nbitmap; i++)
		user_assert(!block_is_free(i+2));
	user_assert(bitmap);

//	writef("read_bitmap is good\n");
}

// Test that write_block works, by smashing the superblock and reading it back.
/** Test that write_block works, by smashing the superblock and reading it
 * back.
 *
 * __Need to be verified.__
 */
void
check_write_block(void)
{
	super = 0;

	// back up super block
	read_block(0, 0, 0);
	user_bcopy((char*)diskaddr(1), (char*)diskaddr(0), BY2PG);

	// smash it 
	strcpy((char*)diskaddr(1), "OOPS!\n");
	write_block(1);//user_panic("#########################");
	user_assert(block_is_mapped(1));
	//^^^user_assert(!va_is_dirty(diskaddr(1)));
	
	// clear it out
	syscall_mem_unmap(0, diskaddr(1));
	user_assert(!block_is_mapped(1));

	// read it back in
	read_block(1, 0, 0);
	user_assert(strcmp((char*)diskaddr(1), "OOPS!\n") == 0);

	// fix it
	user_bcopy((char*)diskaddr(0), (char*)diskaddr(1), BY2PG);
	write_block(1);
	super = (struct Super*)diskaddr(1);

//	writef("write_block is good\n");
}

// Initialize the file system
/** Initialize the file system and test writing and reading.
 */
void
fs_init(void)
{
	read_super();
	
	check_write_block();
	read_bitmap();
}

// Like pgdir_walk but for files.  Set *ppdiskbno to a pointer to the disk block number
// for the filebno'th block in file f.  If alloc is set and we need an indirect
// block, allocate it.  
/** Find the disk block number entry for a specific block of a file.
 *
 * If success, set *ppdiskbno to a pointer to the entry and return `0`.
 *
 * When need to allocate an indirect block, if alloc is zero, return
 * `-#E_NOT_FOUND`, otherwise, allocate a block to it via alloc_block().
 *
 * For simplicity of OS lab, if the request filebno is greater than or equal to
 * NINDIRECT, return `-#E_INVAL`.
 *
 * @param[in] f Pointer to the file.
 * @param[in] filebno Block number of the file to be located.
 * @param[out] ppdiskbno Address of the pointor to the disk block number entry
 * for the block of the file.
 * @param[in] alloc Indicate whether alloc an indirect block when needed.
 */
int
file_block_walk(struct File *f, u_int filebno, u_int **ppdiskbno, u_int alloc)
{
	int r;
	u_int *ptr;
	void *blk;

	if (filebno < NDIRECT)
		ptr = &f->f_direct[filebno];
	else if (filebno < NINDIRECT) {
		if (f->f_indirect == 0) {
			if (alloc == 0)
				return -E_NOT_FOUND;
			if ((r = alloc_block()) < 0)
				return r;
			f->f_indirect = r;
		}
		if ((r=read_block(f->f_indirect, &blk, 0)) < 0)
			return r;
		user_assert(blk != 0);
		ptr = (u_int*)blk + filebno;
	} else
		return -E_INVAL;

	*ppdiskbno = ptr;
	return 0;
}

// Set *diskbno to the disk block number for the filebno'th block in file f.
// If alloc is set and the block does not exist, allocate it.
/** Map a physic block to a block of a file.
 *
 * If alloc is zero, check whether a physic block has been mapped to it. If
 * yes, save the disk block number in *diskbno and return `0`, otherwise return
 * `-#E_NOT_FOUND`.
 *
 * If alloc is not zero, when no physic block mapped to it, allocate one via
 * alloc_block(). If success, save the disk block number in *diskbno and return
 * `0`, otherwise return an error code.
 *
 * @param[in] f Pointer to the file.
 * @param[in] filebno Block number of the file to be maped to a physic block.
 * @param[out] diskbno Pointor to the disk block number for the block.
 * @param[in] alloc Indicate whether alloc an block when needed.
 */
int
file_map_block(struct File *f, u_int filebno, u_int *diskbno, u_int alloc)
{
	int r;
	u_int *ptr;

	if ((r = file_block_walk(f, filebno, &ptr, alloc)) < 0)
		return r;
	if (*ptr == 0) {
		if (alloc == 0)
			return -E_NOT_FOUND;
		if ((r = alloc_block()) < 0)
			return r;
		*ptr = r;
	}
	*diskbno = *ptr;
	return 0;
}

// Remove a block from file f.  If it's not there, just silently succeed.
/** Remove a block from a file.
 *
 * If it doesn't exist, just succeed silently.
 *
 * @param[in] f Pointer to the file.
 * @param[in] filebno Block number of the file to be removed.
 */
int
file_clear_block(struct File *f, u_int filebno)
{
	int r;
	u_int *ptr = NULL;

	if ((r = file_block_walk(f, filebno, &ptr, 0)) < 0 && r != -E_NOT_FOUND)
		return r;
//writef("come here\n");
	if (*ptr) {
		free_block(*ptr);
		*ptr = 0;
	}
	return 0;
}

// Set *blk to point at the filebno'th block in file f.
// 
// Hint: use file_map_block and read_block.
/** Make sure a block of a file is loaded into memory, set *blk to the address
 * of the block in memory.
 *
 * If the specific block doesn't exist, allocate a physic block to it and then
 * load it. 
 *
 * \warning No guarantee is provided the content of the allocated block. The
 * contents of it can be
 * anything.
 *
 * @param[in] f Pointer to the file.
 * @param[in] filebno Block number of the file to be maped to a physic block.
 * @param[out] blk Address of the pointor to block in memory.
 */
int
file_get_block(struct File *f, u_int filebno, void **blk)
{
	int r, isnew;
	u_int diskbno;

	// Your code here -- read in the block, leaving the pointer in *blk.
//	user_panic("file_get_block not implemented");
	r = file_map_block(f, filebno, &diskbno, 1/*0*/);
	//writef("file_get_block:diskno:0x%x\n", diskbno);
	if(r<0) return r;
	
	r = read_block(diskbno, blk, &isnew);
	if(r<0) return r;
//writef("diskbno=%d	,isnew=%d\n",diskbno,isnew);
/*
u_int *value,i;
value = *blk;
for(i = 0;i<1024;i++)
{
	writef("%x	",*value);
	value++;
}
*/
	// Don't need to maintain reference counts anymore.
	return 0;
}

// Mark the offset/BY2BLK'th block dirty in file f
// by writing its first word to itself.  
/** Mark the block where offset is in dirty by writing its first word to
 * itself.
 *
 * \warning Due to that there is no mechanism to distinguish whether a page is
 * clean, the function is not implemented actually.
 *
 * @param[in] f Pointer to the file.
 * @param[in] offset Offset of the file, indicate which block is to be dirty.
 */
int
file_dirty(struct File *f, u_int offset)
{
	int r;
	void *blk;

	if ((r = file_get_block(f, offset/BY2BLK, &blk)) < 0)
		return r;
	*(volatile char*)blk = *(volatile char*)blk;
	return 0;
}

// Try to find a file named "name" in dir.  If so, set *file to it.
/** Try to find a file named `name` in dir.
 *
 * If find out, set its dir property properly and *file to it and return `0`,
 * otherwise return `-#E_NOT_FOUND`.
 *
 * If some error occured, return an error code.
 *
 * @param[in] dir Pointer to the directory.
 * @param[in] name Name of the file try to find out.
 * @param[out] file Address of the pointer to the result file.
 */
int
dir_lookup(struct File *dir, char *name, struct File **file)
{
	int r;
	u_int i, j, nblock;
	void *blk;
	struct File *f;

	// search dir for name
	nblock = (dir->f_size + BY2BLK - 1) / BY2BLK;
	for (i = 0; i < nblock; i++)
   	{
		if ((r=file_get_block(dir, i, &blk)) < 0)
			return r;
		f = blk;
		for (j=0; j<FILE2BLK; j++)
		{
//writef("f[j].f_name=%s,	f[j].f_size=%d\n",f[j].f_name,f[j].f_size);
			if (strcmp(f[j].f_name, name) == 0)
			{
				*file = &f[j];
				f[j].f_dir = dir;
				return 0;
			}
		}
	}
	return -E_NOT_FOUND;	
}

// Set *file to point at a free File structure in dir.
/** Find a free File structure in dir and set *file pointing to it.
 *
 * On success, return `0`, otherwise, return an error code.
 *
 * @param[in] dir Pointer to the directory.
 * @param[in] file Address of the pointor to the allocated file.
 */
int
dir_alloc_file(struct File *dir, struct File **file)
{
	int r;
	u_int nblock, i , j;
	void *blk;
	struct File *f;

	nblock = (dir->f_size + BY2BLK - 1) / BY2BLK;
	for (i = 0; i < nblock; i++)
   	{
		if ((r = file_get_block(dir, i, &blk)) < 0)
			return r;
		f = blk;
		for (j = 0; j < FILE2BLK; j++)
		{
			if (f[j].f_name[0] == '\0')
			{
				*file = &f[j];
				return 0;
			}
		}
	}
	dir->f_size += BY2BLK;
	if ((r = file_get_block(dir, i, &blk)) < 0)
		return r;
	f = blk;
	*file = &f[0];
	return 0;		
}

// Skip over slashes.
/** Skip over slashes.
 *
 * Return the first location where the character is not `/`.
 *
 * @param[in] p Pointor of a string.
 */
char*
skip_slash(char *p)
{
	while (*p == '/')
		p++;
	return p;
}

// Evaluate a path name, starting at the root.
// On success, set *pfile to the file we found
// and set *pdir to the directory the file is in.
// If we cannot find the file but find the directory
// it should be in, set *pdir and copy the final path
// element into lastelem.
/** Try to find the file specified by the path.
 *
 * On success, set *pfile to the file and *pdir to the directory it is in, and
 * return `0`.
 *
 * If cannot find the file but find the directory it should be in, more
 * particularly, the parent directory, set *pdir to it and copy the rest of
 * path into lastelem. __This case is also considered as an error.__
 *
 * *pdir and *pfile will be clear at first.
 *
 * If some errors occur, return an error code.
 *
 * @param[in] path Path string.
 * @param[out] pdir Address of the pointor to result directory.
 * @param[out] pfile Address of the pointor to result file.
 * @param[out] lastelem Begining of a string to collect the last part of path
 * which cannot be parsed.
 */
int
walk_path(char *path, struct File **pdir, struct File **pfile, char *lastelem)
{
	char *p;
	char name[MAXNAMELEN];
	struct File *dir, *file;
	int r;

	// if (*path != '/')
	//	return -E_BAD_PATH;
	path = skip_slash(path);
	file = &super->s_root;
	dir = 0;
	name[0] = 0;

	if(pdir)
		*pdir = 0;
	*pfile = 0;
	
	while (*path != '\0') {
		dir = file;
	//	writef("walk_path(): dir:%s\n",dir->f_name);
		p = path;
		while (*path != '/' && *path != '\0')
			path++;
		if (path - p >= MAXNAMELEN)
			return -E_BAD_PATH;//user_panic("!!!!!!!!!!!!!!!!!! src:%x dst:%x len:%x",p, name, path - p);
		user_bcopy(p, name, path - p);
		name[path - p] = '\0';
		path = skip_slash(path);

		if (dir->f_type != FTYPE_DIR)
			return -E_NOT_FOUND;

		if ((r=dir_lookup(dir, name, &file)) < 0) {
			if(r == -E_NOT_FOUND && *path == '\0') {
				if(pdir)
					*pdir = dir;
				if (lastelem)
					strcpy(lastelem, name);
				*pfile = 0;
			}
			return r;
		}
	}

	if(pdir)
		*pdir = dir;
	*pfile = file;
	return 0;
}

// Open "path".  On success set *pfile to point at the file and return 0.
// On error return < 0.
/** Find the specific file and set *file pointing to it.
 *
 * On sucess, return `0`, otherwise, return an error code.
 *
 * Just a simple wrap of walk_path().
 *
 * @param[in] path Path string.
 * @param[out] file Address of the pointor to the result file.
 */
int
file_open(char *path, struct File **file)
{
	// Your code here
//	user_panic("file_open not implemented");
//	return 0;
	int r;
	struct File *pdir,*f;
	char name[MAXNAMELEN];
	if((r=walk_path(path, &pdir, &f, name))<0)	//cannot find the file at the path
	{
		/*
		r = dir_alloc_file(&pdir, file);
		if(r<0) return r;
		(*file)->f_dir = &pdir;
		(*file)->f_size = 0;
		strcpy((*file)->f_name,lastitem);
		(*file)->f_type = FTYPE_REG;
		*/
//		writef("%s:%e\n",path,r);
		return r;
	}
	*file = f;
	return 0;
}

// Create "path".  On success set *file to point at the file and return 0.
// On error return < 0.
/** Create a file specified by path.
 *
 * On success, set *file pointing to the result file and return `0`. If the
 * file has existed, the directory where the file is in doesn't exist, or any
 * other errors occur, return an error code.
 *
 * @param[in] path Path string.
 * @param[out] file Address of the pointor to the result file.
 */
int
file_create(char *path, struct File **file)
{
	char name[MAXNAMELEN];
	int r;
	struct File *dir, *f;

	if ((r = walk_path(path, &dir, &f, name)) == 0)
		return -E_FILE_EXISTS;
	if (r != -E_NOT_FOUND || dir == 0)
		return r;
	if (dir_alloc_file(dir, &f) < 0)
		return r;
	strcpy(f->f_name, name);
	*file = f;
	return 0;
}

// Truncate file down to newsize bytes.  
// Since the file is shorter, we can free the blocks
// that were used by the old bigger version but not
// by our new smaller self.  For both the old and new sizes,
// figure out the number of blocks required.
// and then clear the blocks from new_nblocks to old_nblocks.
// If the new_nblocks is no more than NDIRECT, free the indirect
// block too.  (Remember to clear the f->f_indirect pointer so
// you'll know whether it's valid!)
//
// Hint: use file_clear_block.
/** Truncate file down to newsize bytes.
 *
 * \pre 0 <= newsize <= f->f_size
 *
 * @param[in] f Pointor to the file to be truncated.
 * @param[in] newsize The new size of the file.
 */
void
file_truncate(struct File *f, u_int newsize)
{
	int r;
	u_int bno, old_nblocks, new_nblocks;

	// Your code here
	old_nblocks = (f->f_size + BY2BLK - 1) / BY2BLK;
	new_nblocks = (newsize + BY2BLK - 1) / BY2BLK;
	if(newsize == 0) new_nblocks = 0;
//writef("file_truncate:begin,	new_nblocks=%d\n",new_nblocks);	
//writef("file_truncate:come in <=NDIRECT\n");
	for(bno = new_nblocks; bno < old_nblocks; bno++)
		file_clear_block(f, bno);
	if(f->f_indirect)
	{
		free_block(f->f_indirect);
		f->f_indirect = 0;
	}
	f->f_size = newsize;
	return;
//	user_panic("file_truncate not implemented");
}

/** Set file's size to newsize bytes.
 * 
 * If newsize is less than the old size, truncate file down via
 * file_truncate().
 *
 * If newsize is greater than the old size, just set size but do nothing. __To
 * be verified.__
 *
 * @param[in] f Pointor to the file whose size will be set.
 * @param[in] newsize The new size of the file.
 */
int
file_set_size(struct File *f, u_int newsize)
{
//writef("f->f_name=%s,	f->f_size=%d\n",f->f_name,f->f_size);
	if (f->f_size > newsize)
		file_truncate(f, newsize);
	f->f_size = newsize;
	if (f->f_dir)
		file_flush(f->f_dir);
	return 0;
}

// Flush the contents of file f out to disk.
// Loop over all the blocks in file.
// Translate the file block number into a disk block number
// and then check whether that disk block is dirty.  If so, write it out.
//
// Hint: use file_map_block, block_is_dirty, and write_block.
/** Flush dirty blocks of the file out to disk.
 *
 * If any error occurs, return an error code.
 *
 * @param[in] f Pointor to the file to be flushed.
 */
void
file_flush(struct File *f)
{
	// Your code here
	u_int nblocks;
	u_int bno;
	u_int diskno;
	int r;

	nblocks = (f->f_size + BY2BLK - 1) / BY2BLK;
	for(bno = 0;bno < nblocks;bno++)
	{
		r = file_map_block(f, bno, &diskno, 0);
		if(r<0) return r;
		if(block_is_dirty(diskno))
			write_block(diskno);
	}
	return 0;
//	user_panic("file_flush not implemented");
}

// Sync the entire file system.  A big hammer.
/** Synchronize the entire file system.
 *
 * Flush all dirty blocks out to disk.
 */
void
fs_sync(void)
{
	int i;

	for (i=0; i<super->s_nblocks; i++)
		if (block_is_dirty(i))
			write_block(i);
}

// Close a file.
/** Close a file.
 *
 * Flush the content of the file to disk. If it has a parent directory, do the
 * same thing to the directory.
 *
 * @param[in] f Pointor to the file to be flushed.
 */
void
file_close(struct File *f)
{
	file_flush(f);
	if (f->f_dir)
		file_flush(f->f_dir);
}

// Remove a file by truncating it and then zeroing the name.
/** Remove a file specified by path.
 *
 * Truncate the file to zero and nullify its name. Finally flush it via
 * file_flush(). If it has a parent directory, do the same thing to the
 * directory.
 *
 * @param[in] path Path string.
 */
int
file_remove(char *path)
{
	int r;
	struct File *f;

	if ((r = walk_path(path, 0, &f, 0)) < 0)
		return r;

	file_truncate(f, 0);
	f->f_name[0] = '\0';
	file_flush(f);
	if (f->f_dir)
		file_flush(f->f_dir);

	return 0;
}

