// SPDX-License-Identifier: GPL-2.0-only
/*
 * SBI initialilization and all extension implementation.
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */
#include "sbi.hh"
#include "klib.h"
#include "kernel.hh"

#define __ro_after_init
#define EXPORT_SYMBOL(x)
#define pr_info printf
#define pr_warn printf
#define pr_err(...) Log(error,__VA_ARGS__)

unsigned long sbi_spec_version __ro_after_init = SBI_SPEC_VERSION_DEFAULT;
EXPORT_SYMBOL(sbi_spec_version);
static void (*__sbi_set_timer)(uint64_t stime) __ro_after_init;
static void (*__sbi_send_ipi)(unsigned int cpu) __ro_after_init;
static int (*__sbi_rfence)(int fid, const struct cpumask *cpu_mask,
			   unsigned long start, unsigned long size,
			   unsigned long arg4, unsigned long arg5) __ro_after_init;

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5)
{
	struct sbiret ret;

	register uintptr_t a0 asm ("a0") = (uintptr_t)(arg0);
	register uintptr_t a1 asm ("a1") = (uintptr_t)(arg1);
	register uintptr_t a2 asm ("a2") = (uintptr_t)(arg2);
	register uintptr_t a3 asm ("a3") = (uintptr_t)(arg3);
	register uintptr_t a4 asm ("a4") = (uintptr_t)(arg4);
	register uintptr_t a5 asm ("a5") = (uintptr_t)(arg5);
	register uintptr_t a6 asm ("a6") = (uintptr_t)(fid);
	register uintptr_t a7 asm ("a7") = (uintptr_t)(ext);
	asm volatile ("ecall"
		      : "+r" (a0), "+r" (a1)
		      : "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6), "r" (a7)
		      : "memory");
	ret.error = a0;
	ret.value = a1;

	return ret;
}

static void __sbi_set_timer_v02(uint64_t stime_value)
{
#if __riscv_xlen == 32
	sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET_TIMER, stime_value,
		  stime_value >> 32, 0, 0, 0, 0);
#else
	sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET_TIMER, stime_value, 0,
		  0, 0, 0, 0);
#endif
}

static void __sbi_send_ipi_v02(unsigned int cpu)
{
	int result;
	struct sbiret ret = {0};

	ret = sbi_ecall(SBI_EXT_IPI, SBI_EXT_IPI_SEND_IPI,
			1UL, (cpu), 0, 0, 0, 0);
	if (ret.error) {
		result = sbi_err_map_linux_errno(ret.error);
		pr_err("%s: hbase = [%lu] failed (error [%d])\n",
			__func__, (cpu), result);
	}
}

static int __sbi_rfence_v02_call(unsigned long fid, unsigned long hmask,
				 unsigned long hbase, unsigned long start,
				 unsigned long size, unsigned long arg4,
				 unsigned long arg5)
{
	struct sbiret ret = {0};
	int ext = SBI_EXT_RFENCE;
	int result = 0;

	switch (fid) {
	case SBI_EXT_RFENCE_REMOTE_FENCE_I:
		ret = sbi_ecall(ext, fid, hmask, hbase, 0, 0, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, arg4, 0);
		break;

	case SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, arg4, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, arg4, 0);
		break;
	default:
		pr_err("unknown function ID [%lu] for SBI extension [%d]\n",
		       fid, ext);
		result = -1;
	}

	if (ret.error) {
		result = sbi_err_map_linux_errno(ret.error);
		pr_err("%s: hbase = [%lu] hmask = [0x%lx] failed (error [%d])\n",
		       __func__, hbase, hmask, result);
	}

	return result;
}

static int __sbi_rfence_v02(int fid, const struct cpumask *cpu_mask,
			    unsigned long start, unsigned long size,
			    unsigned long arg4, unsigned long arg5)
{
	unsigned long hartid, cpuid, hmask = 0, hbase = 0, htop = 0;
	int result;
	pr_err("unimplemented!");
	// if (!cpu_mask || cpumask_empty(cpu_mask))
	// 	cpu_mask = cpu_online_mask;

	// for_each_cpu(cpuid, cpu_mask) {
	// 	hartid = cpuid_to_hartid_map(cpuid);
	// 	if (hmask) {
	// 		if (hartid + BITS_PER_LONG <= htop ||
	// 		    hbase + BITS_PER_LONG <= hartid) {
	// 			result = __sbi_rfence_v02_call(fid, hmask,
	// 					hbase, start, size, arg4, arg5);
	// 			if (result)
	// 				return result;
	// 			hmask = 0;
	// 		} else if (hartid < hbase) {
	// 			/* shift the mask to fit lower hartid */
	// 			hmask <<= hbase - hartid;
	// 			hbase = hartid;
	// 		}
	// 	}
	// 	if (!hmask) {
	// 		hbase = hartid;
	// 		htop = hartid;
	// 	} else if (hartid > htop) {
	// 		htop = hartid;
	// 	}
	// 	hmask |= BIT(hartid - hbase);
	// }

	// if (hmask) {
	// 	result = __sbi_rfence_v02_call(fid, hmask, hbase,
	// 				       start, size, arg4, arg5);
	// 	if (result)
	// 		return result;
	// }

	return 0;
}

/**
 * sbi_set_timer() - Program the timer for next timer event.
 * @stime_value: The value after which next timer event should fire.
 *
 * Return: None.
 */
void sbi_set_timer(uint64_t stime_value)
{
	__sbi_set_timer(stime_value);
}

/**
 * sbi_send_ipi() - Send an IPI to any hart.
 * @cpu: Logical id of the target CPU.
 */
void sbi_send_ipi(unsigned int cpu)
{
	__sbi_send_ipi(cpu);
}

/**
 * sbi_remote_fence_i() - Execute FENCE.I instruction on given remote harts.
 * @cpu_mask: A cpu mask containing all the target harts.
 *
 * Return: 0 on success, appropriate linux error code otherwise.
 */
int sbi_remote_fence_i(const struct cpumask *cpu_mask)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_FENCE_I,
			    cpu_mask, 0, 0, 0, 0);
}

/**
 * sbi_remote_sfence_vma() - Execute SFENCE.VMA instructions on given remote
 *			     harts for the specified virtual address range.
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the virtual address
 * @size: Total size of the virtual address range.
 *
 * Return: 0 on success, appropriate linux error code otherwise.
 */
int sbi_remote_sfence_vma(const struct cpumask *cpu_mask,
			   unsigned long start,
			   unsigned long size)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_SFENCE_VMA,
			    cpu_mask, start, size, 0, 0);
}

/**
 * sbi_remote_sfence_vma_asid() - Execute SFENCE.VMA instructions on given
 * remote harts for a virtual address range belonging to a specific ASID.
 *
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the virtual address
 * @size: Total size of the virtual address range.
 * @asid: The value of address space identifier (ASID).
 *
 * Return: 0 on success, appropriate linux error code otherwise.
 */
int sbi_remote_sfence_vma_asid(const struct cpumask *cpu_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID,
			    cpu_mask, start, size, asid, 0);
}

/**
 * sbi_remote_hfence_gvma() - Execute HFENCE.GVMA instructions on given remote
 *			   harts for the specified guest physical address range.
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the guest physical address
 * @size: Total size of the guest physical address range.
 *
 * Return: None
 */
int sbi_remote_hfence_gvma(const struct cpumask *cpu_mask,
			   unsigned long start,
			   unsigned long size)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA,
			    cpu_mask, start, size, 0, 0);
}
/**
 * sbi_remote_hfence_gvma_vmid() - Execute HFENCE.GVMA instructions on given
 * remote harts for a guest physical address range belonging to a specific VMID.
 *
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the guest physical address
 * @size: Total size of the guest physical address range.
 * @vmid: The value of guest ID (VMID).
 *
 * Return: 0 if success, Error otherwise.
 */
int sbi_remote_hfence_gvma_vmid(const struct cpumask *cpu_mask,
				unsigned long start,
				unsigned long size,
				unsigned long vmid)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID,
			    cpu_mask, start, size, vmid, 0);
}

/**
 * sbi_remote_hfence_vvma() - Execute HFENCE.VVMA instructions on given remote
 *			     harts for the current guest virtual address range.
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the current guest virtual address
 * @size: Total size of the current guest virtual address range.
 *
 * Return: None
 */
int sbi_remote_hfence_vvma(const struct cpumask *cpu_mask,
			   unsigned long start,
			   unsigned long size)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA,
			    cpu_mask, start, size, 0, 0);
}

/**
 * sbi_remote_hfence_vvma_asid() - Execute HFENCE.VVMA instructions on given
 * remote harts for current guest virtual address range belonging to a specific
 * ASID.
 *
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the current guest virtual address
 * @size: Total size of the current guest virtual address range.
 * @asid: The value of address space identifier (ASID).
 *
 * Return: None
 */
int sbi_remote_hfence_vvma_asid(const struct cpumask *cpu_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID,
			    cpu_mask, start, size, asid, 0);
}

static void sbi_srst_reset(unsigned long type, unsigned long reason)
{
	sbi_ecall(SBI_EXT_SRST, SBI_EXT_SRST_RESET, type, reason,
		  0, 0, 0, 0);
	pr_warn("%s: type=0x%lx reason=0x%lx failed\n",
		__func__, type, reason);
}

static void sbi_srst_power_off(void)
{
	sbi_srst_reset(SBI_SRST_RESET_TYPE_SHUTDOWN,
		       SBI_SRST_RESET_REASON_NONE);
}

/**
 * sbi_probe_extension() - Check if an SBI extension ID is supported or not.
 * @extid: The extension ID to be probed.
 *
 * Return: 1 or an extension specific nonzero value if yes, 0 otherwise.
 */
long sbi_probe_extension(int extid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_PROBE_EXT, extid,
			0, 0, 0, 0, 0);
	if (!ret.error)
		return ret.value;

	return 0;
}
static long __sbi_base_ecall(int fid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_BASE, fid, 0, 0, 0, 0, 0, 0);
	if (!ret.error)
		return ret.value;
	else
		return sbi_err_map_linux_errno(ret.error);
}

static inline long sbi_get_spec_version(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_SPEC_VERSION);
}

static inline long sbi_get_firmware_id(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_IMP_ID);
}

static inline long sbi_get_firmware_version(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_IMP_VERSION);
}

long sbi_get_mvendorid(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_MVENDORID);
}

long sbi_get_marchid(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_MARCHID);
}

long sbi_get_mimpid(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_MIMPID);
}

void sbi_init(void)
{
	int ret;

	// sbi_set_power_off();
	ret = sbi_get_spec_version();
	if (ret > 0)
		sbi_spec_version = ret;

	pr_info("SBI specification v%ld.%ld detected\n",
		sbi_major_version(), sbi_minor_version());

	if (!sbi_spec_is_0_1()) {
		pr_info("SBI implementation ID=0x%lx Version=0x%lx\n",
			sbi_get_firmware_id(), sbi_get_firmware_version());
		if (sbi_probe_extension(SBI_EXT_TIME)) {
			__sbi_set_timer = __sbi_set_timer_v02;
			pr_info("SBI TIME extension detected\n");
		} else {
			// __sbi_set_timer = __sbi_set_timer_v01;
		}
		if (sbi_probe_extension(SBI_EXT_IPI)) {
			__sbi_send_ipi	= __sbi_send_ipi_v02;
			pr_info("SBI IPI extension detected\n");
		} else {
			// __sbi_send_ipi	= __sbi_send_ipi_v01;
		}
		if (sbi_probe_extension(SBI_EXT_RFENCE)) {
			__sbi_rfence	= __sbi_rfence_v02;
			pr_info("SBI RFENCE extension detected\n");
		} else {
			// __sbi_rfence	= __sbi_rfence_v01;
		}
		if ((sbi_spec_version >= sbi_mk_version(0, 3)) &&
		    sbi_probe_extension(SBI_EXT_SRST)) {
			pr_info("SBI SRST extension detected\n");
			// pm_power_off = sbi_srst_power_off;
			// sbi_srst_reboot_nb.notifier_call = sbi_srst_reboot;
			// sbi_srst_reboot_nb.priority = 192;
			// register_restart_handler(&sbi_srst_reboot_nb);
		}
	} else {
		// __sbi_set_timer = __sbi_set_timer_v01;
		// __sbi_send_ipi	= __sbi_send_ipi_v01;
		// __sbi_rfence	= __sbi_rfence_v01;
	}
}
void sbi_console_putchar(char ch){
	sbi_ecall(0x1,0,ch,0,0,0,0,0);
}
struct sbiret sbi_debug_console_write(unsigned long num_bytes,
                                      unsigned long base_addr_lo,
                                      unsigned long base_addr_hi){
	// auto rt=sbi_ecall(0x4442434E,0,num_bytes,base_addr_lo,base_addr_hi,0,0,0);
	while(num_bytes--)
		sbi_console_putchar(*(char*)(base_addr_lo++));
	return sbiret{.error=0};
}
void sbi_shutdown(void)
{
	sbi_ecall(SBI_EXT_0_1_SHUTDOWN, 0, 0, 0, 0, 0, 0, 0);
}
int sbi_hsm_hart_start(unsigned long hartid, unsigned long saddr,
			      unsigned long a1)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_START,
			hartid, saddr, a1, 0, 0, 0);
	if (ret.error)
		return sbi_err_map_linux_errno(ret.error);
	else
		return 0;
}

int sbi_hsm_hart_get_status(unsigned long hartid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_STATUS,
			hartid, 0, 0, 0, 0, 0);
	if (ret.error)
		return sbi_err_map_linux_errno(ret.error);
	else
		return ret.value;
}