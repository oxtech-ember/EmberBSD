/* SPDX-License-Identifier: BSD-2-Clause */
/* Aggregate only timing. Do not read reports, coordinates, or key input. */
#pragma D option quiet
dtrace:::BEGIN
{
	last["ims_intr"] = 0;
	last["imt_intr"] = 0;
}
fbt::ihidev_work:entry
{
	self->started = timestamp;
	@work_calls = count();
}
fbt::ihidev_work:return
/self->started/
{
	@work_us = quantize((timestamp - self->started) / 1000);
	self->started = 0;
}
fbt::ims_intr:entry,
fbt::imt_intr:entry
{
	@reports[probefunc] = count();
	@interval_us[probefunc] = quantize(last[probefunc] ?
	    (timestamp - last[probefunc]) / 1000 : 0);
	last[probefunc] = timestamp;
}
tick-10s
{
	exit(0);
}
