/* The kernel call implemented in this file:
 *   m_type:	SYS_UMAP_REMOTE
 *
 * The parameters for this kernel call are:
 *   m_lsys_krn_sys_umap.src_endpt	(process number)
 *   m_lsys_krn_sys_umap.segment	(segment where address is: T, D, or S)
 *   m_lsys_krn_sys_umap.src_addr	(virtual address)
 *   m_lsys_krn_sys_umap.dst_endpt	(process number of grantee to check access for)
 *   m_krn_lsys_sys_umap.dst_addr	(returns physical address)
 *   m_lsys_krn_sys_umap.nr_bytes	(size of datastructure)
 */

#include "kernel/system.h"
#include "kernel/vm.h"

#include <minix/endpoint.h>
#include <assert.h>

#if USE_UMAP || USE_UMAP_REMOTE

#if ! USE_UMAP_REMOTE
#undef do_umap_remote
#endif

/*==========================================================================*
 *				do_umap_remote				    *
 *==========================================================================*/
int do_umap_remote_impl(struct proc * caller, message * m_ptr)
{
	/* Since we need to keep the lock on the caller, only unlock the target
	 * if it is not the caller. The following macros does that for us while
	 * removing some clutter.*/
#define unlock_targetpr() do{if(targetpr!=caller)unlock_proc(targetpr);} while(0)

	/* Map virtual address to physical, for non-kernel processes. */
	const int seg_type = m_ptr->m_lsys_krn_sys_umap.segment & SEGMENT_TYPE;
	int seg_index = m_ptr->m_lsys_krn_sys_umap.segment & SEGMENT_INDEX;
	vir_bytes offset = m_ptr->m_lsys_krn_sys_umap.src_addr;
	const int count = m_ptr->m_lsys_krn_sys_umap.nr_bytes;
	const endpoint_t endpt = m_ptr->m_lsys_krn_sys_umap.src_endpt;
	endpoint_t grantee = m_ptr->m_lsys_krn_sys_umap.dst_endpt;
	int proc_nr, proc_nr_grantee;
	phys_bytes phys_addr = 0, lin_addr = 0;
	struct proc *targetpr;

	/* Verify process number. */
	if (endpt == SELF)
		okendpt(caller->p_endpoint, &proc_nr);
	else
		if (! isokendpt(endpt, &proc_nr))
			return(EINVAL);

	targetpr = proc_addr(proc_nr);

	/* Verify grantee endpoint */
	if (grantee == SELF) {
		grantee = caller->p_endpoint;
	} else if (grantee == NONE ||
			grantee == ANY ||
			seg_index != MEM_GRANT ||
			!isokendpt(grantee, &proc_nr_grantee)) {
		return EINVAL;
	}

	/* See which mapping should be made. */
	if(seg_type!=LOCAL_VM_SEG) {
		printf("umap: peculiar type\n");
		return EINVAL;
	}

	if(seg_index == MEM_GRANT) {
		vir_bytes newoffset;
		endpoint_t newep;
		int new_proc_nr;
		cp_grant_id_t grant = (cp_grant_id_t) offset;

		lock_two_procs(caller,targetpr);
		const int vres = verify_grant(caller,targetpr->p_endpoint, grantee, grant, count,
					0, 0, &newoffset, &newep, NULL);
		if(vres==VMSUSPEND) {
			unlock_targetpr();
			/* Propagate VMSUSPEND. */
			return vres;
		}
		if(vres!=OK) {
			printf("SYSTEM: do_umap: verify_grant in %s, grant %d, bytes 0x%lx, failed, caller %s\n", targetpr->p_name, offset, count, caller->p_name);
			proc_stacktrace(caller);
			goto fail;
		}
		unlock_two_procs(caller,targetpr);

		if(!isokendpt(newep, &new_proc_nr)) {
			printf("SYSTEM: do_umap: isokendpt failed\n");
			goto fail;
		}

		/* New lookup. */
		offset = newoffset;
		targetpr = proc_addr(new_proc_nr);
		seg_index = VIR_ADDR;
	}
	lock_proc(targetpr);

	if(seg_index == VIR_ADDR) {
		phys_addr = lin_addr = offset;
	} else {
		printf("SYSTEM: bogus seg type 0x%lx\n", seg_index);
		goto fail;
	}

	if(!lin_addr) {
		printf("SYSTEM:do_umap: umap_local failed\n");
		goto fail;
	}

	if(vm_lookup(targetpr, lin_addr, &phys_addr, NULL) != OK) {
		printf("SYSTEM:do_umap: vm_lookup failed\n");
		goto fail;
	}

	if(phys_addr == 0)
		panic("vm_lookup returned zero physical address");

	if(vm_running && vm_lookup_range(targetpr, lin_addr, NULL, count) != count) {
		printf("SYSTEM:do_umap: not contiguous\n");
		goto fail;
	}

	m_ptr->m_krn_lsys_sys_umap.dst_addr = phys_addr;
	if(phys_addr == 0) {
		printf("kernel: umap 0x%x done by %d / %s, pc 0x%lx, 0x%lx -> 0x%lx\n",
				seg_type, caller->p_endpoint, caller->p_name,
				caller->p_reg.pc, offset, phys_addr);
		printf("caller stack: ");
		proc_stacktrace(caller);
	}

	if (phys_addr == 0){
		goto fail;
	} else {
		unlock_targetpr();
		if(targetpr!=caller)
			lock_proc(caller);
		return OK;
	}
fail:
	unlock_targetpr();
	if(targetpr!=caller)
		lock_proc(caller);
	return EFAULT;
}

int do_umap_remote(struct proc * caller, message * m_ptr)
{
	const int res = do_umap_remote_impl(caller,m_ptr);
	assert_proc_locked(caller);
	return res;
}

#endif /* USE_UMAP || USE_UMAP_REMOTE */
