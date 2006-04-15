/*	$OpenBSD: m88k_machdep.c,v 1.14 2006/04/15 15:43:33 miod Exp $	*/
/*
 * Copyright (c) 1998, 1999, 2000, 2001 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/msgbuf.h>
#include <sys/exec.h>
#include <sys/errno.h>
#include <sys/lock.h>

#include <machine/asm_macro.h>
#include <machine/cmmu.h>
#include <machine/cpu.h>
#include <machine/locore.h>
#include <machine/reg.h>
#ifdef M88100
#include <machine/m88100.h>
#endif

#include <uvm/uvm_extern.h>

#include <net/netisr.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#endif /* DDB */

void	dosoftint(void);
void	dumpconf(void);
void	dumpsys(void);
void	regdump(struct trapframe *f);

/*
 * CMMU and CPU variables
 */

#ifdef MULTIPROCESSOR
__cpu_simple_lock_t cmmu_cpu_lock = __SIMPLELOCK_UNLOCKED;
cpuid_t	master_cpu;
#endif

struct cpu_info m88k_cpus[MAX_CPUS];
u_int	max_cpus;

struct cmmu_p *cmmu;

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = IPL_NONE;

/*
 * Set registers on exec.
 * Clear all except sp and pc.
 */
void
setregs(p, pack, stack, retval)
	struct proc *p;
	struct exec_package *pack;
	u_long stack;
	int retval[2];
{
	struct trapframe *tf = (struct trapframe *)USER_REGS(p);

	/*
	 * The syscall will ``return'' to snip; set it.
	 * argc, argv, envp are placed on the stack by copyregs.
	 * Point r2 to the stack. crt0 should extract envp from
	 * argc & argv before calling user's main.
	 */

	bzero((caddr_t)tf, sizeof *tf);

#ifdef M88110
	if (CPU_IS88110) {
		/*
		 * user mode, serialize mem, interrupts enabled,
		 * graphics unit, fp enabled
		 */
		tf->tf_epsr = PSR_SRM | PSR_SFD;
		/*
		 * XXX disable OoO for now...
		 */
		tf->tf_epsr |= PSR_SER;
	}
#endif
#ifdef M88100
	if (CPU_IS88100) {
		/*
		 * user mode, interrupts enabled,
		 * no graphics unit, fp enabled
		 */
		tf->tf_epsr = PSR_SFD | PSR_SFD2;
	}
#endif

	/*
	 * We want to start executing at pack->ep_entry. The way to
	 * do this is force the processor to fetch from ep_entry. Set
	 * NIP to something bogus and invalid so that it will be a NOOP.
	 * And set sfip to ep_entry with valid bit on so that it will be
	 * fetched.  mc88110 - just set exip to pack->ep_entry.
	 */
#ifdef M88110
	if (CPU_IS88110) {
		tf->tf_exip = pack->ep_entry & XIP_ADDR;
	}
#endif
#ifdef M88100
	if (CPU_IS88100) {
		tf->tf_snip = pack->ep_entry & NIP_ADDR;
		tf->tf_sfip = (pack->ep_entry & FIP_ADDR) | FIP_V;
	}
#endif
	tf->tf_r[2] = stack;
	tf->tf_r[31] = stack;
	retval[1] = 0;
}

int
copystr(fromaddr, toaddr, maxlength, lencopied)
	const void *fromaddr;
	void *toaddr;
	size_t maxlength;
	size_t *lencopied;
{
	u_int tally;

	tally = 0;

	while (maxlength--) {
		*(u_char *)toaddr = *(u_char *)fromaddr++;
		tally++;
		if (*(u_char *)toaddr++ == 0) {
			if (lencopied) *lencopied = tally;
			return (0);
		}
	}

	if (lencopied)
		*lencopied = tally;

	return (ENAMETOOLONG);
}

void
setrunqueue(p)
	struct proc *p;
{
	struct prochd *q;
	struct proc *oldlast;
	int which = p->p_priority >> 2;

#ifdef DIAGNOSTIC
	if (p->p_back != NULL)
		panic("setrunqueue %p", p);
#endif
	q = &qs[which];
	whichqs |= 1 << which;
	p->p_forw = (struct proc *)q;
	p->p_back = oldlast = q->ph_rlink;
	q->ph_rlink = p;
	oldlast->p_forw = p;
}

/*
 * Remove process p from its run queue, which should be the one
 * indicated by its priority.  Calls should be made at splstatclock().
 */
void
remrunqueue(vp)
	struct proc *vp;
{
	struct proc *p = vp;
	int which = p->p_priority >> 2;
	struct prochd *q;

#ifdef DIAGNOSTIC
	if ((whichqs & (1 << which)) == 0)
		panic("remrq %p", p);
#endif
	p->p_forw->p_back = p->p_back;
	p->p_back->p_forw = p->p_forw;
	p->p_back = NULL;
	q = &qs[which];
	if (q->ph_link == (struct proc *)q)
		whichqs &= ~(1 << which);
}

#ifdef DDB
int longformat = 1;
void
regdump(struct trapframe *f)
{
#define R(i) f->tf_r[i]
	printf("R00-05: 0x%08x  0x%08x  0x%08x  0x%08x  0x%08x  0x%08x\n",
	       R(0),R(1),R(2),R(3),R(4),R(5));
	printf("R06-11: 0x%08x  0x%08x  0x%08x  0x%08x  0x%08x  0x%08x\n",
	       R(6),R(7),R(8),R(9),R(10),R(11));
	printf("R12-17: 0x%08x  0x%08x  0x%08x  0x%08x  0x%08x  0x%08x\n",
	       R(12),R(13),R(14),R(15),R(16),R(17));
	printf("R18-23: 0x%08x  0x%08x  0x%08x  0x%08x  0x%08x  0x%08x\n",
	       R(18),R(19),R(20),R(21),R(22),R(23));
	printf("R24-29: 0x%08x  0x%08x  0x%08x  0x%08x  0x%08x  0x%08x\n",
	       R(24),R(25),R(26),R(27),R(28),R(29));
	printf("R30-31: 0x%08x  0x%08x\n",R(30),R(31));
#ifdef M88110
	if (CPU_IS88110) {
		printf("exip %x enip %x\n", f->tf_exip, f->tf_enip);
	}
#endif
#ifdef M88100
	if (CPU_IS88100) {
		printf("sxip %x snip %x sfip %x\n",
		    f->tf_sxip, f->tf_snip, f->tf_sfip);
	}
	if (CPU_IS88100 && f->tf_vector == 0x3) {
		/* print dmt stuff for data access fault */
		printf("dmt0 %x dmd0 %x dma0 %x\n",
		    f->tf_dmt0, f->tf_dmd0, f->tf_dma0);
		printf("dmt1 %x dmd1 %x dma1 %x\n",
		    f->tf_dmt1, f->tf_dmd1, f->tf_dma1);
		printf("dmt2 %x dmd2 %x dma2 %x\n",
		    f->tf_dmt2, f->tf_dmd2, f->tf_dma2);
		printf("fault type %d\n", (f->tf_dpfsr >> 16) & 0x7);
		dae_print((unsigned *)f);
	}
	if (CPU_IS88100 && longformat != 0) {
		printf("fpsr %x fpcr %x epsr %x ssbr %x\n",
		    f->tf_fpsr, f->tf_fpcr, f->tf_epsr, f->tf_ssbr);
		printf("fpecr %x fphs1 %x fpls1 %x fphs2 %x fpls2 %x\n",
		    f->tf_fpecr, f->tf_fphs1, f->tf_fpls1,
		    f->tf_fphs2, f->tf_fpls2);
		printf("fppt %x fprh %x fprl %x fpit %x\n",
		    f->tf_fppt, f->tf_fprh, f->tf_fprl, f->tf_fpit);
		printf("vector %d mask %x mode %x scratch1 %x cpu %p\n",
		    f->tf_vector, f->tf_mask, f->tf_mode,
		    f->tf_scratch1, f->tf_cpu);
	}
#endif
#ifdef M88110
	if (CPU_IS88110 && longformat != 0) {
		printf("fpsr %x fpcr %x fpecr %x epsr %x\n",
		    f->tf_fpsr, f->tf_fpcr, f->tf_fpecr, f->tf_epsr);
		printf("dsap %x duap %x dsr %x dlar %x dpar %x\n",
		    f->tf_dsap, f->tf_duap, f->tf_dsr, f->tf_dlar, f->tf_dpar);
		printf("isap %x iuap %x isr %x ilar %x ipar %x\n",
		    f->tf_isap, f->tf_iuap, f->tf_isr, f->tf_ilar, f->tf_ipar);
		printf("vector %d mask %x mode %x scratch1 %x cpu %p\n",
		    f->tf_vector, f->tf_mask, f->tf_mode,
		    f->tf_scratch1, f->tf_cpu);
	}
#endif
}
#endif	/* DDB */

/*
 * Set up the cpu_info pointer and the cpu number for the current processor.
 */
void
set_cpu_number(cpuid_t number)
{
	struct cpu_info *ci;
	extern struct pcb idle_u;

#ifdef MULTIPROCESSOR
	ci = &m88k_cpus[number];
#else
	ci = &m88k_cpus[0];
#endif
	ci->ci_cpuid = number;

	__asm__ __volatile__ ("stcr %0, cr17" :: "r" (ci));
	flush_pipeline();

#ifdef MULTIPROCESSOR
	if (number == master_cpu)
#endif
	{
		ci->ci_primary = 1;
		ci->ci_idle_pcb = &idle_u;
	}

	ci->ci_alive = 1;
}

/*
 * Soft interrupt interface
 */

int ssir;
int netisr;

#ifdef MULTIPROCESSOR
#include <sys/lock.h>

__cpu_simple_lock_t sir_lock = __SIMPLELOCK_UNLOCKED;

void
setsoftint(int sir)
{
	__cpu_simple_lock(&sir_lock);
	ssir |= sir;
	__cpu_simple_unlock(&sir_lock);
}

int
clrsoftint(int sir)
{
	int tmpsir;

	__cpu_simple_lock(&sir_lock);
	tmpsir = ssir & sir;
	ssir ^= tmpsir;
	__cpu_simple_unlock(&sir_lock);

	return (tmpsir);
}
#endif

void
dosoftint()
{
	if (clrsoftint(SIR_NET)) {
		uvmexp.softs++;
#define DONETISR(bit, fn) \
	do { \
		if (netisr & (1 << bit)) { \
			netisr &= ~(1 << bit); \
			fn(); \
		} \
	} while (0)
#include <net/netisr_dispatch.h>
#undef DONETISR
	}

	if (clrsoftint(SIR_CLOCK)) {
		uvmexp.softs++;
		softclock();
	}
}

int
spl0()
{
	int s;

	s = splsoftclock();

	if (ssir)
		dosoftint();

	setipl(0);
	return (s);
}
