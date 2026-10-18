/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted actual-source DISPLAY PCK snapshot control. */
static void
test_display_reserved_snapshot(void)
{
	const int owner = 41, other = 42;
	struct sun60i_a733_display_power state, saved;
	unsigned count = 0;

	borrow_fixture();
	memset(&state, 0xa5, sizeof(state));
	saved = state;
	assert(sun60i_a733_pck_display_snapshot(1, &owner, &state) == EINVAL);
	assert(peeks == 0 && memcmp(&state, &saved, sizeof(state)) == 0);
	assert(sun60i_a733_pck_display_snapshot(99, &owner, &state) == ENXIO);
	assert(sun60i_a733_pck_display_snapshot(1, NULL, &state) == EINVAL);
	assert(sun60i_a733_pck_display_snapshot(1, &owner, NULL) == EINVAL);
	assert(sun60i_a733_pck_display_reserve(1, &owner) == 0);
	peeks = 0;
	assert(sun60i_a733_pck_display_snapshot(1, &other, &state) == EINVAL && peeks == 0);
	assert(sun60i_a733_pck_display_snapshot(1, &owner, &state) == 0 && peeks == 12);
	for (unsigned s = 0; s < 2; s++)
		for (unsigned d = 0; d < 2; d++) {
			assert(state.sample[s][d][0] == 8 && state.sample[s][d][1] == 0);
			assert(state.sample[s][d][2] == 8);
		}
	assert(state.changed == 0 && writes == 0);
	assert(sun60i_a733_pck_display_retain(1, &owner) == 0);
	assert(sun60i_a733_pck_display_snapshot(1, &owner, &state) == 0);
	count++;
	for (unsigned fault = 1; fault <= 12; fault++) {
		borrow_fixture();
		assert(sun60i_a733_pck_display_reserve(1, &owner) == 0);
		peeks = 0; fail_peek = fault;
		state = saved;
		assert(sun60i_a733_pck_display_snapshot(1, &owner, &state) == EFAULT);
		assert(peeks == fault && writes == 0);
		assert(memcmp(&state, &saved, sizeof(state)) == 0);
		count++;
	}
	for (unsigned stale = 0; stale < 5; stale++) {
		borrow_fixture();
		assert(sun60i_a733_pck_display_reserve(1, &owner) == 0);
		peeks = 0;
		if (stale == 0) sc.sc_failed[10] = true;
		if (stale == 1) registers[0xa008 / 4] = 0;
		if (stale == 2) registers[0xa000 / 4] |= PCK600_LOCK;
		if (stale == 3) sc.sc_lease[10].owner = &other;
		if (stale == 4) registers[0xa004 / 4] = 1;
		state = saved;
		assert(sun60i_a733_pck_display_snapshot(1, &owner, &state) != 0);
		assert(memcmp(&state, &saved, sizeof(state)) == 0 && writes == 0);
		if (stale == 3) assert(peeks == 0);
		count++;
	}
	lease_test = false;
	reset_fixture();
	printf("PASS: %u reserved DISPLAY PCK snapshot scenarios\n", count);
}
