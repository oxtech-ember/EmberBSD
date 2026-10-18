/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted retained borrowed-memory source control. */
static void
display_memory_fixture(void)
{
	display_fixture();
	REG(0x5b4) = UINT32_C(0x00010007);
	REG(0x584) = UINT32_C(0x00010001);
	REG(0x588) = UINT32_C(0x81000000);
	REG(0x5e0) |= __BIT(1);
	REG(0x580) = UINT32_C(0xc3000000);
}

static void
test_display_memory(void)
{
	uint32_t words[2][A733_DISPLAY_NMEMORY_REGS], saved[2][A733_DISPLAY_NMEMORY_REGS];
	static const bus_size_t expected_regs[] = {
		0x5b4, 0x584, 0x588, 0x5e0, 0x1908, 0x1988, 0x580
	};
	static const uint32_t expected_masks[] = {
		0x10007, 0x10001, 0x9f00001f, 0x802, 0x02000200, 0x02000000, 0xdf00001f
	};
	bool retained, handled;
	unsigned first = checks;
	const int other = 47;

	display_memory_fixture();
	CHECK(__arraycount(expected_regs) == A733_DISPLAY_NMEMORY_REGS);
	for (unsigned i = 0; i < A733_DISPLAY_NMEMORY_REGS; i++) {
		CHECK(sun60i_a733_display_memory_regs[i].reg == expected_regs[i]);
		CHECK(sun60i_a733_display_memory_regs[i].mask == expected_masks[i]);
	}
	memset(words, 0xa5, sizeof(words));
	memcpy(saved, words, sizeof(saved));
	CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == EINVAL);
	CHECK(sun60i_a733_ccu_display_memory_state(CLOCK(A733_CLK_DE0), &display_cookie, words) == EINVAL);
	CHECK(reads == 0 && memcmp(words, saved, sizeof(words)) == 0);
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
	reads = 0;
	handled = false;
	CHECK(sun60i_a733_gpu_guard_enter(&state, CLOCK(A733_CLK_BUS_PWM0), NULL,
	    SUNXI_CCU_ENABLE, &handled) == 0 && !handled);
	CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
	CHECK(reads == 0);
	sun60i_a733_gpu_guard_exit(&state);
	CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &other) == EINVAL);
	CHECK(reads == 0);
	CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	CHECK(reads == 14 && writes == 0);
	CHECK(sun60i_a733_ccu_display_memory_state(CLOCK(A733_CLK_DE0), &other, words) == EINVAL);
	CHECK(memcmp(words, saved, sizeof(words)) == 0);
	CHECK(sun60i_a733_ccu_display_memory_state(CLOCK(A733_CLK_DE0), &display_cookie, words) == 0);
	for (unsigned s = 0; s < 2; s++)
		for (unsigned i = 0; i < A733_DISPLAY_NMEMORY_REGS; i++)
			CHECK(words[s][i] == REG(expected_regs[i]));
	for (unsigned i = 0; i < A733_DISPLAY_NMEMORY_REGS; i++) {
		const bus_size_t reg = sun60i_a733_display_memory_regs[i].reg;
		const uint32_t mask = sun60i_a733_display_memory_regs[i].mask;
		for (unsigned bit = 0; bit < 32; bit++) {
			if ((mask & __BIT(bit)) == 0) continue;
			CHECK(sunxi_ccu_update_bits(&state, reg, __BIT(bit), 0) == EBUSY);
		}
	}
	CHECK(sunxi_ccu_update_bits(&state, MBUS_MASTER_GATE_REG, __BIT(16), 0) == 0);
	CHECK(sun60i_a733_ccu_display_memory_state(CLOCK(A733_CLK_DE0), &display_cookie, words) == 0);
	CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	CHECK(!sun60i_display_lease.memory_claimed && !display_power_retained);
	CHECK(sunxi_ccu_update_bits(&state, 0x5b4, __BIT(0), 0) == 0);
	for (unsigned fault = 1; fault <= 14; fault++) {
		display_memory_fixture();
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
		reads = 0; fail_peek = fault;
		CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == EFAULT);
		CHECK(!sun60i_display_lease.memory_claimed && reads == fault && writes == 0);
		CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	}
	for (unsigned i = 0; i < A733_DISPLAY_NMEMORY_REGS; i++) {
		display_memory_fixture();
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
		reads = 0;
		unstable_reg = sun60i_a733_display_memory_regs[i].reg;
		unstable_mask = sun60i_a733_display_memory_regs[i].mask &
		    -sun60i_a733_display_memory_regs[i].mask;
		unstable_read = 8 + i;
		CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
		CHECK(!sun60i_display_lease.memory_claimed && writes == 0);
	}
	for (unsigned i = 0; i < A733_DISPLAY_NMEMORY_REGS; i++) {
		display_memory_fixture();
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
		CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
		REG(sun60i_a733_display_memory_regs[i].reg) ^=
		    sun60i_a733_display_memory_regs[i].mask & -sun60i_a733_display_memory_regs[i].mask;
		memcpy(words, saved, sizeof(words));
		CHECK(sun60i_a733_ccu_display_memory_state(CLOCK(A733_CLK_DE0), &display_cookie, words) == EBUSY);
		CHECK(memcmp(words, saved, sizeof(words)) == 0 && writes == 0);
		CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	}
	for (unsigned fault = 1; fault <= 14; fault++) {
		display_memory_fixture(); retained = false;
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
		CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
		CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) == 0);
		CHECK(retained && sun60i_a733_ccu_display_ready(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
		reads = 0; fail_peek = fault;
		memcpy(words, saved, sizeof(words));
		CHECK(sun60i_a733_ccu_display_memory_state(CLOCK(A733_CLK_DE0), &display_cookie, words) == EFAULT);
		CHECK(memcmp(words, saved, sizeof(words)) == 0 && writes == 4);
		CHECK(sun60i_display_lease.fault == EFAULT);
		CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
	}
	display_memory_fixture(); retained = false;
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
	CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	REG(0x5b4) ^= __BIT(0);
	CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) == EBUSY);
	CHECK(retained && writes == 0 && sun60i_display_lease.fault != 0);
	display_memory_fixture(); retained = false;
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
	CHECK(sun60i_a733_ccu_display_memory_reserve(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) == 0);
	REG(0x5b4) ^= __BIT(0);
	CHECK(sun60i_a733_ccu_display_ready(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
	CHECK(writes == 4 && sun60i_display_lease.fault != 0);
	printf("A733 retained DISPLAY memory claim: %u checks passed\n", checks - first);
	prepare_test = false;
}
