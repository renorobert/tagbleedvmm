#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>

void init_ftrace(void)
{
	if (getuid() != 0 || geteuid() != 0) 
		errx(EXIT_FAILURE, "[!] Run program as root");

	system("echo printk-msg-only > /sys/kernel/debug/tracing/trace_options");
	system("echo 100000 > /sys/kernel/debug/tracing/buffer_size_kb");
	system("> /sys/kernel/debug/tracing/trace");
}

int main(int argc, char **argv)
{
	int tlbdev;

	init_ftrace();

	tlbdev = open("/dev/tlbdev", O_RDWR);

	if (tlbdev < 0)
		errx(EXIT_FAILURE, "[!] Error opening dev");

	warnx("[+] Measuring TLB evictions across VMEXITs...");
	ioctl(tlbdev, 0, NULL);

	system("cp /sys/kernel/debug/tracing/trace results/trace_tlb.log");
	warnx("[+] Check trace_tlb.log file...");

	return 0;
}
