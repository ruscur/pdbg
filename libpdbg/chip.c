/* Copyright 2016 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <ccan/array_size/array_size.h>
#include <unistd.h>

#include "hwunit.h"
#include "operations.h"
#include "bitutils.h"
#include "debug.h"
#include "sprs.h"
#include "chip.h"

uint64_t mfspr(uint64_t reg, uint64_t spr)
{
	if (reg > 31)
		PR_ERROR("Invalid register specified for mfspr\n");

	return MFSPR_OPCODE | (reg << 21) | ((spr & 0x1f) << 16) | ((spr & 0x3e0) << 6);
}

uint64_t mtspr(uint64_t spr, uint64_t reg)
{
	if (reg > 31)
		PR_ERROR("Invalid register specified for mtspr\n");

	return MTSPR_OPCODE | (reg << 21) | ((spr & 0x1f) << 16) | ((spr & 0x3e0) << 6);
}

static uint64_t mfocrf(uint64_t reg, uint64_t cr)
{
	if (reg > 31)
		PR_ERROR("Invalid register specified for mfocrf\n");
	if (cr > 7)
		PR_ERROR("Invalid CR field specified\n");

	return MFOCRF_OPCODE | (reg << 21) | (1U << (12 + cr));
}

static uint64_t mtocrf(uint64_t cr, uint64_t reg)
{
	if (reg > 31) {
		PR_ERROR("Invalid register specified for mfocrf\n");
		exit(1);
	}
	if (cr > 7) {
		PR_ERROR("Invalid CR field specified\n");
		exit(1);
	}

	return MTOCRF_OPCODE | (reg << 21) | (1U << (12 + cr));
}

static uint64_t mfnia(uint64_t reg)
{
	if (reg > 31)
		PR_ERROR("Invalid register specified for mfnia\n");

	return MFNIA_OPCODE | (reg << 21);
}

static uint64_t mtnia(uint64_t reg)
{
	if (reg > 31)
		PR_ERROR("Invalid register specified for mtnia\n");

	return MTNIA_OPCODE | (reg << 21);
}

static uint64_t mfmsr(uint64_t reg)
{
	if (reg > 31)
		PR_ERROR("Invalid register specified for mfmsr\n");

	return MFMSR_OPCODE | (reg << 21);
}

static uint64_t mtmsr(uint64_t reg)
{
	if (reg > 31)
		PR_ERROR("Invalid register specified for mtmsr\n");

	return MTMSR_OPCODE | (reg << 21);
}

static uint64_t ld(uint64_t rt, uint64_t ds, uint64_t ra)
{
	if ((rt > 31) | (ra > 31) | (ds > 0x3fff))
		PR_ERROR("Invalid register specified\n");

	return LD_OPCODE | (rt << 21) | (ra << 16) | (ds << 2);
}

/*
 * RAMs the opcodes in *opcodes and store the results of each opcode
 * into *results. *results must point to an array the same size as
 * *opcodes. Each entry from *results is put into SCR0 prior to
 * executing an opcode so that it may also be used to pass in
 * data. Note that only registers r0 and r1 are saved and restored so
 * opcode sequences must preserve other registers.
 */
int ram_instructions(struct thread *thread, uint64_t *opcodes,
			    uint64_t *results, int len, unsigned int lpar)
{
	uint64_t opcode = 0, r0 = 0, r1 = 0, scratch = 0;
	int i;
	int exception = 0;
	bool did_setup = false;

	if (!thread->ram_is_setup) {
		CHECK_ERR(thread->ram_setup(thread));
		did_setup = true;
	}

	/* RAM instructions */
	for (i = -2; i < len + 2; i++) {
		if (i == -2)
			/* Save r1 (assumes opcodes don't touch other registers) */
			opcode = mtspr(277, 1);
		else if (i == -1)
			/* Save r0 (assumes opcodes don't touch other registers) */
			opcode = mtspr(277, 0);
		else if (i < len) {
			scratch = results[i];
			opcode = opcodes[i];
		} else if (i == len) {
			/* Restore r0 */
			scratch = r0;
			opcode = mfspr(0, 277);
		} else if (i == len + 1) {
			/* Restore r1 */
			scratch = r1;
			opcode = mfspr(1, 277);
		}

		if (thread->ram_instruction(thread, opcode, &scratch)) {
			PR_DEBUG("%s: %d, %016" PRIx64 "\n", __FUNCTION__, __LINE__, opcode);
			exception = 1;
			if (i >= 0 && i < len)
				/* skip the rest and attempt to restore r0 and r1 */
				i = len - 1;
			else
				break;
		}

		if (i == -2)
			r1 = scratch;
		else if (i == -1)
			r0 = scratch;
		else if (i < len)
			results[i] = scratch;
	}

	if (did_setup)
		CHECK_ERR(thread->ram_destroy(thread));

	return exception;
}

/*
 * Get gpr value. Chip must be stopped.
 */
int ram_getgpr(struct thread *thread, int gpr, uint64_t *value)
{
	uint64_t opcodes[] = {mtspr(277, gpr)};
	uint64_t results[] = {0};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	*value = results[0];
	return 0;
}

int ram_putgpr(struct thread *thread, int gpr, uint64_t value)
{
	uint64_t opcodes[] = {mfspr(gpr, 277)};
	uint64_t results[] = {value};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	return 0;
}

int ram_getnia(struct thread *thread, uint64_t *value)
{
	uint64_t opcodes[] = {mfnia(0), mtspr(277, 0)};
	uint64_t results[] = {0, 0};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	*value = results[1];
	return 0;
}

/*
 * P9 must MTNIA from LR, P8 can MTNIA from R0. So we set both LR and R0
 * to value. LR must be saved and restored.
 *
 * This is a hack and should be made much cleaner once we have target
 * specific putspr commands.
 */
int ram_putnia(struct thread *thread, uint64_t value)
{
	uint64_t opcodes[] = {	mfspr(1, 8),	/* mflr r1 */
				mfspr(0, 277),	/* value -> r0 */
				mtspr(8, 0),	/* mtlr r0 */
				mtnia(0),
				mtspr(8, 1), };	/* mtlr r1 */
	uint64_t results[] = {0, value, 0, 0, 0};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	return 0;
}

int ram_getspr(struct thread *thread, int spr, uint64_t *value)
{
	uint64_t opcodes[] = {mfspr(0, spr), mtspr(277, 0)};
	uint64_t results[] = {0, 0};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	*value = results[1];
	return 0;
}

int ram_putspr(struct thread *thread, int spr, uint64_t value)
{
	uint64_t opcodes[] = {mfspr(0, 277), mtspr(spr, 0)};
	uint64_t results[] = {value, 0};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	return 0;
}

int ram_getmsr(struct thread *thread, uint64_t *value)
{
	uint64_t opcodes[] = {mfmsr(0), mtspr(277, 0)};
	uint64_t results[] = {0, 0};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	*value = results[1];
	return 0;
}

int ram_getcr(struct thread *thread, uint32_t *value)
{
	uint64_t opcodes[] = {mfocrf(0, 0), mtspr(277, 0), mfocrf(0, 1), mtspr(277, 0),
			      mfocrf(0, 2), mtspr(277, 0), mfocrf(0, 3), mtspr(277, 0),
			      mfocrf(0, 4), mtspr(277, 0), mfocrf(0, 5), mtspr(277, 0),
			      mfocrf(0, 6), mtspr(277, 0), mfocrf(0, 7), mtspr(277, 0)};
	uint64_t results[16] = {0};
	uint32_t cr_field, cr = 0;
	int i;

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	for (i = 1; i < 16; i += 2) {
		cr_field = results[i];
		/* We are not guaranteed that the other bits will be zeroed out */
		cr |= cr_field & (0xf << 2*(i-1));
	}

	*value = cr;
	return 0;
}

int ram_putcr(struct thread *thread, uint32_t value)
{
	uint64_t opcodes[] = {mfspr(0, 277), mtocrf(0, 0), mtocrf(1, 0),
			      mtocrf(2, 0), mtocrf(3, 0), mtocrf(4, 0),
			      mtocrf(5, 0), mtocrf(6, 0), mtocrf(7, 0)};
	uint64_t results[] = {value};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	return 0;
}

int ram_putmsr(struct thread *thread, uint64_t value)
{
	uint64_t opcodes[] = {mfspr(0, 277), mtmsr(0)};
	uint64_t results[] = {value, 0};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	return 0;
}

int ram_getmem(struct thread *thread, uint64_t addr, uint64_t *value)
{
	uint64_t opcodes[] = {mfspr(0, 277), mfspr(1, 277), ld(0, 0, 1), mtspr(277, 0)};
	uint64_t results[] = {0xdeaddeaddeaddead, addr, 0, 0};

	CHECK_ERR(ram_instructions(thread, opcodes, results, ARRAY_SIZE(opcodes), 0));
	*value = results[3];
	return 0;
}

/*
 * Read the given ring from the given chiplet. Result must be large enough to hold ring_len bits.
 */
int getring(struct pdbg_target *target, uint64_t ring_addr, uint64_t ring_len, uint32_t result[])
{
	struct chiplet *chiplet;

	assert(pdbg_target_is_class(target, "chiplet"));
	chiplet = target_to_chiplet(target);
	return chiplet->getring(chiplet, ring_addr, ring_len, result);
}

int ram_getregs(struct thread *thread, struct thread_regs *regs)
{
	struct thread_regs _regs;
	uint64_t value = 0;
	int i;

	if (!regs)
		regs = &_regs;

	CHECK_ERR(thread->ram_setup(thread));

	ram_getnia(thread, &regs->nia);
	ram_getspr(thread, SPR_CFAR, &regs->cfar);
	ram_getmsr(thread, &regs->msr);
	ram_getspr(thread, SPR_LR, &regs->lr);
	ram_getspr(thread, SPR_CTR, &regs->ctr);
	ram_getspr(thread, 815, &regs->tar);
	ram_getcr(thread, &regs->cr);

	thread->getxer(thread, &regs->xer);

	for (i = 0; i < 32; i++)
		ram_getgpr(thread, i, &regs->gprs[i]);

	ram_getspr(thread, SPR_LPCR, &regs->lpcr);
	ram_getspr(thread, SPR_PTCR, &regs->ptcr);
	ram_getspr(thread, SPR_LPIDR, &regs->lpidr);
	ram_getspr(thread, SPR_PIDR, &regs->pidr);
	ram_getspr(thread, SPR_HFSCR, &regs->hfscr);

	ram_getspr(thread, SPR_HDSISR, &value);
	regs->hdsisr = value;

	ram_getspr(thread, SPR_HDAR, &regs->hdar);

	ram_getspr(thread, SPR_HEIR, &value);
	regs->heir = value;

	ram_getspr(thread, SPR_HID, &regs->hid);
	ram_getspr(thread, SPR_HSRR0, &regs->hsrr0);
	ram_getspr(thread, SPR_HSRR1, &regs->hsrr1);
	ram_getspr(thread, SPR_HDEC, &regs->hdec);
	ram_getspr(thread, SPR_HSPRG0, &regs->hsprg0);
	ram_getspr(thread, SPR_HSPRG1, &regs->hsprg1);
	ram_getspr(thread, SPR_FSCR, &regs->fscr);

	ram_getspr(thread, SPR_DSISR, &value);
	regs->dsisr = value;

	ram_getspr(thread, SPR_DAR, &regs->dar);
	ram_getspr(thread, SPR_SRR0, &regs->srr0);
	ram_getspr(thread, SPR_SRR1, &regs->srr1);
	ram_getspr(thread, SPR_DEC, &regs->dec);
	ram_getspr(thread, SPR_TB, &regs->tb);
	ram_getspr(thread, SPR_SPRG0, &regs->sprg0);
	ram_getspr(thread, SPR_SPRG1, &regs->sprg1);
	ram_getspr(thread, SPR_SPRG2, &regs->sprg2);
	ram_getspr(thread, SPR_SPRG3, &regs->sprg3);
	ram_getspr(thread, SPR_PPR, &regs->ppr);

	CHECK_ERR(thread->ram_destroy(thread));

	thread_print_regs(regs);

	return 0;
}

static struct proc proc = {
	.target = {
		.name = "Processor Module",
		.compatible = "ibm,power-proc",
		.class = "proc",
	},
};
DECLARE_HW_UNIT(proc);

__attribute__((constructor))
static void register_proc(void)
{
	pdbg_hwunit_register(PDBG_DEFAULT_BACKEND, &proc_hw_unit);
}
