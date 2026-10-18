/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted actual-provider DISPLAY controls. */
static const int display_cookie = 23;

static void
display_fixture(void)
{
	ready_fixture();
	prepare_test = true;
	display_power_owner = &display_cookie;
	REG(AHB_CFG_REG) = (3 << 24) | 2;
	REG(PERI0PLL_GATE_EN_REG) = UINT32_C(0x02060206);
	REG(PERI0PLL_GATE_STAT_REG) = UINT32_C(0x02060000);
	REG(AHB_MASTER_GATE_REG) |= __BIT(6) | __BIT(5) | __BIT(4);
	REG(MBUS_MASTER_GATE_REG) = __BIT(18) | __BIT(16) | __BIT(11);
	REG(0xa74) = __BIT(16);
	REG(0x16ec) = __BIT(16) | __BIT(0);
	REG(DE0_CLK_REG) = __BIT(29) | 31;
	REG(DE0_BGR_REG) = __BIT(7);
}

static void
test_display_native(void)
{
	struct sun60i_a733_display_owner_status status, sentinel;
	struct sunxi_ccu_reset reset_descriptor;
	bool retained, handled;
	const int wrong = 29;
	unsigned first = checks;

	display_fixture();
	CHECK(clk_enable(CLOCK(A733_CLK_DE0)) == EOPNOTSUPP);
	CHECK(writes == 0 && parent_queries == 0);
	CHECK(sun60i_a733_ccu_display_reserve(&hosc, 17, &display_cookie) == EINVAL);
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, NULL) == EINVAL);
	CHECK(reads == 0 && writes == 0);
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 18, &display_cookie) == EINVAL);
	CHECK(sun60i_display_lease.core.owner == NULL && writes == 0);
	for (unsigned fault = 1; fault <= 60; fault++) {
		display_fixture();
		fail_peek = fault;
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == EFAULT);
		CHECK(sun60i_display_lease.core.owner == NULL && writes == 0 && reads == fault);
	}
	for (unsigned fault = 0; fault < 7; fault++) {
		display_fixture();
		switch (fault) {
		case 0: ccu_size = 0x19ab; break;
		case 1: ccu_reg_error = EIO; break;
		case 2: dcxo_len = 8; break;
		case 3: dcxo_node = 0; break;
		case 4: dcxo_error = EIO; break;
		case 5: bad_dcxo = true; break;
		case 6: display_power_error = EIO; break;
		}
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) != 0);
		CHECK(sun60i_display_lease.core.owner == NULL && writes == 0);
		if (fault < 6)
			CHECK(reads == 0);
	}
	display_fixture();
	handled = false;
	CHECK(sun60i_a733_gpu_guard_enter(&state, CLOCK(A733_CLK_BUS_PWM0), NULL,
	    SUNXI_CCU_ENABLE, &handled) == 0 && !handled);
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == EBUSY);
	CHECK(reads == 0 && sun60i_display_lease.core.writers == 1);
	sun60i_a733_gpu_guard_exit(&state);
	CHECK(sun60i_display_lease.core.writers == 0);
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &wrong) == EBUSY);
	CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &wrong) == EINVAL);
	CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	CHECK(!display_power_retained && writes == 0);
	/* Retention failure remains clean. A later failure retains both claims. */
	for (unsigned fault = 0; fault < 3; fault++) {
		display_fixture(); retained = false;
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
		if (fault == 0) display_retain_error = EIO;
		if (fault == 1) fail_peek = reads + 1;
		if (fault == 2) REG(0x1604) = __BIT(16);
		CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) != 0);
		CHECK(retained == (fault != 0) && display_power_retained == retained && writes == 0);
		CHECK(sun60i_a733_ccu_display_status(CLOCK(A733_CLK_DE0), &display_cookie, &status) == 0);
		CHECK(status.retained == retained && ((status.fault != 0) == retained));
		CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) ==
		    (retained ? EBUSY : 0));
	}
	for (unsigned step = 1; step <= 4; step++) {
		for (unsigned error_kind = 0; error_kind < 2; error_kind++) {
			display_fixture(); retained = false;
			CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
			if (error_kind == 0) fail_poke_number = step;
			else reject_write_number = step;
			CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) ==
			    (error_kind == 0 ? EFAULT : EIO));
			CHECK(retained && display_power_retained && writes == step);
			CHECK(!sun60i_display_lease.prepared && sun60i_display_lease.fault != 0);
			CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
		}
	}
	/* Every pre-write, write-readback and post-write CCU peek can fail. */
	for (unsigned fault = 1; fault <= 128; fault++) {
		display_fixture(); retained = false;
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
		reads = 0; fail_peek = fault;
		CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) == EFAULT);
		CHECK(retained && display_power_retained && !sun60i_display_lease.prepared);
		CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
	}
	display_fixture(); retained = false;
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
	CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &wrong, &retained) == EINVAL && !retained);
	CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) == 0);
	CHECK(retained && display_power_retained && writes == 4 && barriers == 4);
	CHECK(write_regs[0] == DE0_CLK_REG && write_vals[0] == UINT32_C(0x23000000));
	CHECK(write_regs[1] == DE0_BGR_REG && write_vals[1] == UINT32_C(0x00010080));
	CHECK(write_regs[2] == DE0_BGR_REG && write_vals[2] == UINT32_C(0x00010081));
	CHECK(write_regs[3] == DE0_CLK_REG && write_vals[3] == UINT32_C(0xa3000000));
	CHECK(sun60i_a733_ccu_display_ready(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	CHECK(sun60i_a733_ccu_display_status(CLOCK(A733_CLK_DE0), &display_cookie, &status) == 0);
	CHECK(status.reserved && status.retained && status.fault == 0 && status.rates.periph400_hz == 400000000);
	sentinel = status;
	CHECK(sun60i_a733_ccu_display_status(CLOCK(A733_CLK_DE0), &wrong, &status) == EINVAL);
	CHECK(memcmp(&sentinel, &status, sizeof(status)) == 0);
	/* Legal disjoint accelerator fields must not invalidate DISPLAY proof. */
	REG(AHB_MASTER_GATE_REG) ^= __BIT(7) | __BIT(6);
	REG(MBUS_MASTER_GATE_REG) ^= __BIT(16) | __BIT(18);
	REG(NPU_CLK_REG) = UINT32_C(0x82000000);
	CHECK(sun60i_a733_ccu_display_ready(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
	static const struct { bus_size_t reg; uint32_t mask; } expected_masks[] = {
		{0, UINT32_MAX}, {0xa0, UINT32_MAX}, {0xa8, UINT32_MAX},
		{0xac, UINT32_MAX}, {0x500, UINT32_MAX}, {0x1908, UINT32_MAX},
		{0x1988, UINT32_MAX}, {0x2e0, UINT32_MAX}, {0x2e8, UINT32_MAX},
		{0x2ec, UINT32_MAX}, {0x1928, UINT32_MAX}, {0x19a8, UINT32_MAX},
		{0xa00, 0x8700001f}, {0xa04, 0x10001}, {0xa74, 0x10000},
		{0x1604, 0x10001}, {0x16cc, 0x10001}, {0x16ec, 0x10001},
		{0x1684, 0x87001f1f}, {0x168c, 0x70001}, {0x1690, 0x87000000},
		{0x5c0, 0x80000030}, {0x5e0, 0x800}
	};
	for (unsigned i = 0; i < __arraycount(expected_masks); i++)
		CHECK(sun60i_a733_display_mask(expected_masks[i].reg) == expected_masks[i].mask);
	/* Both admission and actual raw RMW reject each protected display bit. */
	for (unsigned reg = 0; reg < 0x2000; reg += 4) {
		uint32_t mask = sun60i_a733_display_mask(reg);
		for (unsigned bit = 0; bit < 32; bit++) {
			if ((mask & __BIT(bit)) == 0) continue;
			CHECK(sunxi_ccu_update_bits(&state, reg, __BIT(bit), 0) == EBUSY);
			reset_descriptor.reg = reg; reset_descriptor.mask = __BIT(bit);
			handled = false;
			CHECK(sun60i_a733_gpu_guard_enter(&state, NULL, &reset_descriptor,
			    SUNXI_CCU_ASSERT, &handled) == EBUSY && handled);
		}
	}
	CHECK(clk_enable(CLOCK(A733_CLK_PLL_PERIPH0_400M)) == 0 && writes == 4);
	CHECK(clk_set_rate(CLOCK(A733_CLK_PLL_PERIPH0_400M), 1) == EBUSY);
	CHECK(clk_enable(CLOCK(A733_CLK_DE0)) == EBUSY && writes == 4);
	/* Stale protected state or lost PCK readiness is sticky and read-only. */
	for (unsigned stale = 0; stale < 13; stale++) {
		display_fixture(); retained = false;
		CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
		CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) == 0);
		unsigned old_reads = reads;
		switch (stale) {
		case 0: REG(DE0_CLK_REG) ^= __BIT(31); break;
		case 1: REG(DE0_BGR_REG) ^= __BIT(16); break;
		case 2: REG(0xa74) ^= __BIT(16); break;
		case 3: REG(0x16ec) ^= __BIT(16); break;
		case 4: REG(AHB_MASTER_GATE_REG) ^= __BIT(5); break;
		case 5: REG(MBUS_MASTER_GATE_REG) ^= __BIT(11); break;
		case 6: REG(PLL_REF_CTRL_REG) ^= __BIT(8); break;
		case 7: REG(PERI0PLL_GATE_EN_REG) ^= __BIT(1); break;
		case 8: REG(PERI0PLL_GATE_STAT_REG) ^= __BIT(17); break;
		case 9: REG(0x2e0) ^= __BIT(0); break;
		case 10: display_power_retained = false; break;
		case 11: display_power_error = EIO; break;
		case 12: dcxo_error = EIO; break;
		}
		CHECK(sun60i_a733_ccu_display_ready(CLOCK(A733_CLK_DE0), &display_cookie) != 0);
		if (stale == 10 || stale == 11) CHECK(reads == old_reads);
		CHECK(sun60i_display_lease.fault != 0 && writes == 4);
		CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
	}
	/* Three local owners coexist in every activation order. */
	static const unsigned orders[6][3] = {
		{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}
	};
	for (unsigned order = 0; order < 6; order++) {
		display_fixture();
		REG(GPU0_CLK_REG) = 0;
		REG(GPU0_BGR_REG) = __BIT(7);
		for (unsigned n = 0; n < 3; n++) {
			retained = false;
			switch (orders[order][n]) {
			case 0:
				CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &wrong) == 0);
				CHECK(sun60i_a733_ccu_gpu_prepare(CLOCK(A733_CLK_GPU0), &wrong, &retained) == 0);
				break;
			case 1:
				CHECK(sun60i_a733_ccu_npu_reserve(CLOCK(A733_CLK_NPU), &wrong) == 0);
				CHECK(sun60i_a733_ccu_npu_prepare(CLOCK(A733_CLK_NPU), &wrong, &retained) == 0);
				break;
			case 2:
				CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
				CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) == 0);
				break;
			}
			CHECK(retained);
		}
		CHECK(sun60i_a733_ccu_display_ready(CLOCK(A733_CLK_DE0), &display_cookie) == 0);
		CHECK(sun60i_a733_ccu_npu_ready(CLOCK(A733_CLK_NPU), &wrong) == 0);
	}
	display_fixture(); retained = false;
	CHECK(sun60i_a733_ccu_display_reserve(CLOCK(A733_CLK_DE0), 17, &display_cookie) == 0);
	CHECK(sun60i_a733_ccu_display_begin_de0(CLOCK(A733_CLK_DE0), &display_cookie, &retained) == 0);
	/* Public fault is accounting only, sticky, and cannot release resources. */
	CHECK(sun60i_a733_ccu_display_fault(CLOCK(A733_CLK_DE0), &display_cookie, EIO) == 0);
	CHECK(sun60i_a733_ccu_display_fault(CLOCK(A733_CLK_DE0), &display_cookie, EFAULT) == 0);
	CHECK(sun60i_a733_ccu_display_status(CLOCK(A733_CLK_DE0), &display_cookie, &status) == 0);
	CHECK(status.fault == EIO && writes == 4);
	CHECK(sun60i_a733_ccu_display_ready(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
	CHECK(sun60i_a733_ccu_display_release(CLOCK(A733_CLK_DE0), &display_cookie) == EBUSY);
	printf("A733 typed native DISPLAY admission: %u checks passed\n", checks - first);
	prepare_test = false;
}
