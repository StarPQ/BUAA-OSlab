/** \file fs/serv.c
 *
 * %File system server's main loop.  Serves IPC requests from other process.
 */

#include "fs.h"
#include "fd.h"
#include "lib.h"
#include <mmu.h>

#define debug 0

/** Descriptor of an open file.
 *
 * The open file descriptor is only visible to the file system server process.
 */
struct Open {
	/** Mapped file descriptor for the open file.
	 */
	struct File *o_file;
	/** %File id. `o_fileid % MAXOPEN` is the index of the array #opentab.
	 */
	u_int o_fileid;		
	/** %Open mode. Indicate which operation can be done to the file.
	 */
	int o_mode;		
	/** Virtual address of the Filefd page assigned to the descriptor.
	 */
	struct Filefd *o_ff;	
};

/// Max number of open files in the file system at once
#define MAXOPEN	1024
/// Base address to map open file descriptor's Filefd pages.
#define FILEVA 0x60000000

/// Array of oepn file descriptors.
struct Open opentab[MAXOPEN] = { { 0, 0, 1 } };

/// Virtual address at which to receive page containing client requests.
#define REQVA	0x0ffff000

/** Initialize array opentab.
 *
 * Set `opentab[i].o_fileid` to `i` and assign virtual address where open
 * descriptors save ???  from #FILEVA.
 */
void
serve_init(void)
{
	int i;
	u_int va;

	va = FILEVA;
	for (i=0; i<MAXOPEN; i++) {
		opentab[i].o_fileid = i;
		opentab[i].o_ff = (struct Filefd*)va;
		va += BY2PG;
	}
}

/** Allocate an open file descriptor.
 *
 * The function tries to find an empty open file descriptor. If a descriptor is
 * blank, map a physic page as Filefd page to virtual address `o_ff` via
 * syscall_mem_alloc. If the allocation failed, return an error code.
 *
 * If a descriptor already has a Filefd page and no others use it, it is
 * 'clean'.
 *
 * Finally clear the page via user_bzero, save the descriptor address to the
 * `o` and return the file id.
 *
 * If no empty open file descriptor exists, return `-#E_MAX_OPEN`.
 *
 * \note %Open mode and o_file should be set properly by caller.
 *
 * @param[out] o Address of the pointor which points to the allocated open
 * descriptor.
 */
int
open_alloc(struct Open **o)
{
	int i, r;

	// Find an available open-file table entry
	for (i = 0; i < MAXOPEN; i++) {
		switch (pageref(opentab[i].o_ff)) {
		case 0:
			//writef("^^^^^^^^^^^^^^^^ (u_int)opentab[i].o_ff: %x\n",(u_int)opentab[i].o_ff);
			if ((r = syscall_mem_alloc(0, (u_int)opentab[i].o_ff, PTE_V|PTE_R|PTE_LIBRARY)) < 0)
				return r;
			
		case 1:
			opentab[i].o_fileid += MAXOPEN;
			*o = &opentab[i];
			user_bzero((void*)opentab[i].o_ff, BY2PG);
			return (*o)->o_fileid;
		}
	}
	return -E_MAX_OPEN;
}

/**
 * Look up an open file for envid.
 *
 * If the open file descriptor specified by the `fileid` is blank or
 * its `fileid` is not equal to the parameter, do nothing and return `-#E_INVAL`.
 * Otherwise, save the address of the open file descriptor to `po` and return `0`.
 *
 * `envid` is not used in this implementation.
 * 
 * @param[in] envid Environment id.
 * @param[in] fileid The fileid of the open file.
 * @param[out] po Address of the pointor to the open file descriptor.
 */
int
open_lookup(u_int envid, u_int fileid, struct Open **po)
{
	struct Open *o;

	o = &opentab[fileid%MAXOPEN];
	if (pageref(o->o_ff) == 1 || o->o_fileid != fileid)
		return -E_INVAL;
	*po = o;
	return 0;
}

// Serve requests, sending responses back to envid.
// To send a result back, ipc_send(envid, r, 0, 0).
// To include a page, ipc_send(envid, r, srcva, perm).

/** Serve open requests from other process.
 *
 * Allocate an open descriptor, open the sepicified file and fill out the
 * descriptor properly.
 *
 * If any step failed, the server will panic and send an error code to reqeust
 * process via ipc_send().
 *
 * If success, send `0` and the Filefd page to request process.
 *
 * @param[in] envid Request environment id.
 * @param[in] rq Open request, including file
 * path and open mode.
 */
void
serve_open(u_int envid, struct Fsreq_open *rq)
{
	writef("serve_open %08x %x 0x%x\n", envid, (int)rq->req_path, rq->req_omode);

	u_char path[MAXPATHLEN];
	struct File *f;
	struct Filefd *ff;
	int fileid;
	int r;
	struct Open *o;

	// Copy in the path, making sure it's null-terminated
	user_bcopy(rq->req_path, path, MAXPATHLEN);
	path[MAXPATHLEN - 1] = 0;
//writef("serve_open:enter open %s\n",rq->req_path);
	// Find a file id.
	
	if ((r = open_alloc(&o)) < 0) {
		writef("open_alloc failed: %d", r);
		goto out;
	}
	fileid = r;
	
//writef("serve_open:ending find a file id	o = %x\n",o);
	// Open the file.
	if ((r = file_open(path, &f)) < 0) {
		writef("file_open failed: %e", r);
		goto out;
	}
//writef("serve_open:ending open the file\n");
	// Save the file pointer.
	o->o_file = f;

	// Fill out the Filefd structure
	ff = (struct Filefd*)o->o_ff;
	ff->f_file = *f;
	ff->f_fileid = o->o_fileid;
	o->o_mode = rq->req_omode;
	ff->f_fd.fd_omode = o->o_mode;
	ff->f_fd.fd_dev_id = devfile.dev_id;
//writef("serve_open:will to ipc send\n");
	if (debug) writef("sending success, page %08x\n", (u_int)o->o_ff);
	ipc_send(envid, 0, (u_int)o->o_ff, PTE_V|PTE_R|PTE_LIBRARY);
//writef("serve_open:end of open %s\n",rq->req_path);
	return;
out:user_panic("*********************path:%s",path);
	ipc_send(envid, r, 0, 0);
}

/** Serve map requests from other process.
 *
 * Import the sepicified block of file into memory.
 *
 * No matter request offset is a multiple of BY2BLK, the function will import
 * the block which includes the offset into memory.
 *
 * If some steps fail, send an error code to reqeust process. If success, send
 * `0` and the page containing the block back to the process.
 * 
 * @param[in] envid Request environment id.
 * @param[in] rq Map request, including a fileid of an open file and the offset
 * of the file.
 */
void
serve_map(u_int envid, struct Fsreq_map *rq)
{
	if (debug) writef("serve_map %08x %08x %08x\n", envid, rq->req_fileid, rq->req_offset);
	
	struct Open *pOpen;
	u_int filebno;
	void *blk;
	int r;
	// Your code here
	if((r = open_lookup(envid, rq->req_fileid, &pOpen))<0)
	{
		ipc_send(envid,r,0,0);
		return;
	}
	
	filebno = rq->req_offset/BY2BLK;
	if((r = file_get_block(pOpen->o_file, filebno, &blk))<0)
	{
		ipc_send(envid,r,0,0);
		return;
	}

	ipc_send(envid, 0, blk, PTE_V|PTE_R|PTE_LIBRARY);
	return;
//	user_panic("serve_map not implemented");
}

/** Serve set size requests.
 *
 * Set the size of the sepicified file to the given one via file_set_size().
 *
 * If some steps fail, send an error code back, otherwise, send `0`.
 *
 * @param[in] envid Request environment id.
 * @param[in] rq Set size request, including a fileid of an open file and the
 * new size.
 */
void
serve_set_size(u_int envid, struct Fsreq_set_size *rq)
{
	if (debug) writef("serve_set_size %08x %08x %08x\n", envid, rq->req_fileid, rq->req_size);

        struct Open *pOpen;
        int r;
        // Your code here
        if((r = open_lookup(envid, rq->req_fileid, &pOpen))<0)
        {
                ipc_send(envid,r,0,0);
                return;
        }
	
	if((r = file_set_size(pOpen->o_file, rq->req_size))<0)
	{
		ipc_send(envid,r,0,0);
		return;
	}

	ipc_send(envid, 0, 0, 0);//PTE_V);
	return;
//	user_panic("serve_set_size not implemented");
}

/** Serve close requests.
 *
 * Close the sepicified file via file_close.
 *
 * If some steps fail, send an error code back, otherwise, send `0`.
 *
 * It is no necessory to unmap the open descriptor, see open_alloc().
 *
 * @param[in] envid Request environment id.
 * @param[in] rq Close request, including a fileid of an open file.
 */
void
serve_close(u_int envid, struct Fsreq_close *rq)
{
	if (debug) writef("serve_close %08x %08x\n", envid, rq->req_fileid);

        struct Open *pOpen;
        int r;
        if((r = open_lookup(envid, rq->req_fileid, &pOpen))<0)
        {
                ipc_send(envid,r,0,0);
                return;
        }
//writef("serve_close:pOpen = %x\n",pOpen);	
	file_close(pOpen->o_file);
	ipc_send(envid, 0, 0, 0);//PTE_V);
	
//	syscall_mem_unmap(0, (u_int)pOpen);
	return;		
//	user_panic("serve_close not implemented");
}
		
/** Serve remove requests.
 *
 * Remove the sepicified file via file_remove().
 *
 * If some steps fail, send an error code back, otherwise, send `0`.
 *
 * __What if there is a process holdding an open descriptor of the file?__
 *
 * @param[in] envid Request environment id.
 * @param[in] rq Remove request, including the file path.
 */
void
serve_remove(u_int envid, struct Fsreq_remove *rq)
{
	if (debug) writef("serve_map %08x %s\n", envid, rq->req_path);

	// Your code here
        int r;
	u_char path[MAXPATHLEN];

        // Copy in the path, making sure it's null-terminated
        user_bcopy(rq->req_path, path, MAXPATHLEN);
        path[MAXPATHLEN-1] = 0;
	
	if((r = file_remove(path))<0)
        {
                ipc_send(envid,r,0,0);
                return;
        }
	
	ipc_send(envid, 0, 0, 0);//PTE_V);
//	user_panic("serve_remove not implemented");
}

/** Serve dirty requests.
 *
 * Mark the sepicified block of a file dirty via file_dirty().
 *
 * If some steps fail, send an error code back, otherwise, send `0`.
 *
 * __Requirment on offset?__
 *
 * @param[in] envid Request environment id.
 * @param[in] rq Dirty request, including a fileid of an open file and the
 * offset of the file.
 */
void
serve_dirty(u_int envid, struct Fsreq_dirty *rq)
{
	if (debug) writef("serve_dirty %08x %08x %08x\n", envid, rq->req_fileid, rq->req_offset);

	// Your code here
        struct Open *pOpen;
        int r;
        if((r = open_lookup(envid, rq->req_fileid, &pOpen))<0)
        {
                ipc_send(envid,r,0,0);
                return;
        }

	if((r = file_dirty(pOpen->o_file, rq->req_offset))<0)
	{
		ipc_send(envid,r,0,0);
                return;
	}

	ipc_send(envid, 0, 0, 0);
	return;
//	user_panic("serve_dirty not implemented");
}

/** Serve synchronization requests.
 *
 * Synchronize whole file system via fs_sync(), and send `0` back.
 *
 * @param[in] envid Request environment id.
 */
void
serve_sync(u_int envid)
{
	fs_sync();
	ipc_send(envid, 0, 0, 0);
}

/** Server's main loop.
 * 
 * Wait requests via ipc_recv() and handle it depending on type code.
 *
 * At each end of loop, unmap the reception page to allow ipc_recv() map new
 * request.
 *
 */
void
serve(void)
{
	u_int req, whom, perm;
	for(;;) {
		perm = 0;

		req = ipc_recv(&whom, REQVA, &perm);
		

		// All requests must contain an argument page
		if (!(perm & PTE_V)) {
			writef("Invalid request from %08x: no argument page\n",
				whom);
			continue; // just leave it hanging...
		}

		switch (req) {
		case FSREQ_OPEN:
			serve_open(whom, (struct Fsreq_open*)REQVA);
			break;
		case FSREQ_MAP:
			serve_map(whom, (struct Fsreq_map*)REQVA);
			break;
		case FSREQ_SET_SIZE:
			serve_set_size(whom, (struct Fsreq_set_size*)REQVA);
			break;
		case FSREQ_CLOSE:
			serve_close(whom, (struct Fsreq_close*)REQVA);
			break;
		case FSREQ_DIRTY:
			serve_dirty(whom, (struct Fsreq_dirty*)REQVA);
			break;
		case FSREQ_REMOVE:
			serve_remove(whom, (struct Fsreq_remove*)REQVA);
			break;
		case FSREQ_SYNC:
			serve_sync(whom);
			break;
		default:
			writef("Invalid request code %d from %08x\n", whom, req);
			break;
		}
		syscall_mem_unmap(0, REQVA);
	}
}

void
umain(void)
{
	user_assert(sizeof(struct File)==256);
	writef("FS is running\n");

	// Check that we are able to do I/O
	//outw(0x8A00, 0x8A00);
	writef("FS can do I/O\n");

	serve_init();
	fs_init();
	fs_test();
	
	serve();
}

