/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */


#define CONFIG_USE_RDTSCP 1

typedef uint64_t cycles_t;

extern volatile cycles_t timer_cycles;

static inline void code_barrier(void)
{
	asm volatile("cpuid\n" :: "a" (0) : "%rbx", "%rcx", "%rdx");
}

static inline void data_barrier(void)
{
	asm volatile("mfence\n" ::: "memory");
}

static inline cycles_t rdtscp(void)
{
#if CONFIG_USE_RDTSCP
	cycles_t cycles_lo, cycles_hi;
	
	asm volatile("rdtscp\n" :
		"=a" (cycles_lo), "=d" (cycles_hi) ::
		"%rcx");

	return ((uint64_t)cycles_hi << 32) | cycles_lo;
#elif CONFIG_USE_RDTSC
	cycles_t cycles_lo, cycles_hi;

	asm volatile("rdtsc\n" :
		"=a" (cycles_lo), "=d" (cycles_hi));
	return ((uint64_t)cycles_hi << 32) | cycles_lo;
#else
	return timer_cycles;
#endif
}

struct tlb_cache {
        char *dtlb;
        char *stlb;
        char *dtlb_ptr;
        char *stlb_ptr;
} tlb_cache;

#define wrmsrl(msr) __asm__ __volatile__("wrmsr" : : "c" (msr))

