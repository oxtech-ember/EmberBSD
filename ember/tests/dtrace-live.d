/* Origin: EmberBSD (AI-assisted), live AArch64 syscall, FBT and CTF contract. */
#pragma D option quiet

BEGIN
{
	started = timestamp;
	calls = returns = entries = freturns = typed = ticks = failed = 0;
}
syscall::getpid_with_ppid:entry
/pid == $target/
{ calls++; }
syscall::getpid_with_ppid:return
/pid == $target/
{ returns++; }
fbt:netbsd:sys_getpid_with_ppid:entry
/pid == $target/
{
	entries++;
	/* Both dereferences require the running kernel's CTF layouts. */
	typed += args[0]->l_proc->p_pid == pid;
}
fbt:netbsd:sys_getpid_with_ppid:return
/pid == $target/
{ freturns++; }
profile:::tick-100ms { ticks++; }
profile:::tick-1sec
/timestamp - started > 10000000000/
{ failed = 1; printf("FAIL: trace watchdog\n"); exit(1); }
ERROR { failed = 1; printf("FAIL: DTrace runtime error\n"); exit(1); }
END
{
	printf("calls=%d returns=%d fbt=%d fbt_returns=%d typed=%d ticks=%d\n",
	    calls, returns, entries, freturns, typed, ticks);
	exit(!failed && calls >= 100 && calls == returns && entries == calls &&
	    freturns == entries && typed == entries && ticks > 0 ? 0 : 1);
}
