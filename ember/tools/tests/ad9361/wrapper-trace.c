/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD AD9361 TX owner, attach, and reset/error contract cases. */
int main(void)
{
    struct ad9361_softc devices[16] = {0};
    struct spi_handle spi = {0};
    struct spi_attach_args sa = { .sa_handle = &spi };
    struct ad9361_tx *tx = NULL, *other = NULL;
    struct ad9361_tx_status status;
    int owner, stranger;
    unsigned int count, n;
    const uint64_t frequencies[] = {2430000000ULL, 2440000000ULL};

    sa.sa_cookie = 1;
    ad9361_attach(NULL, &devices[1], &sa);
    assert(probes == 0 && transfers == 0 && configurations == 1);
    assert(configured_speeds[0] == 1000000 && configured_modes[0] == 3);
    assert(devices[1].sc_spi_speed == 10000000 && devices[1].sc_spi_mode == 3);
    assert(pins[1].acquired && pins[1].level == 0);
    assert(ad9361_tx_acquire(1, NULL, &tx) == EINVAL && tx == NULL);
    assert(ad9361_tx_acquire(99, &owner, &tx) == ENXIO);
    assert(ad9361_tx_acquire(1, &owner, &tx) == 0);
    assert(ad9361_tx_acquire(1, &owner, &other) == EBUSY && other == NULL);
    assert(ad9361_tx_acquire(1, &stranger, &other) == EBUSY);
    count = resets;
    assert(ad9361_tx_prepare_reset(tx, &stranger) == EPERM);
    assert(ad9361_tx_bootstrap(tx, &stranger, 2440000000ULL) == EPERM);
    assert(ad9361_tx_retune(tx, &stranger, 2440000000ULL) == EPERM);
    assert(ad9361_tx_lease_fastlock_prepare(tx, &stranger, frequencies, 2) == EPERM);
    assert(ad9361_tx_lease_fastlock_stage(tx, &stranger, 1) == EPERM);
    assert(ad9361_tx_lease_fastlock_recall(tx, &stranger, 1) == EPERM);
    assert(fastlock_calls == 0);
    assert(ad9361_tx_set_attenuation(tx, &stranger, 10000) == EPERM);
    assert(ad9361_tx_lease_mute(tx, &stranger) == EPERM);
    assert(ad9361_tx_read_status(tx, &stranger, &status) == EPERM);
    assert(ad9361_tx_release(tx, &stranger) == EPERM && resets == count);
    assert(ad9361_tx_bootstrap(tx, &owner, 2440000000ULL) == EINVAL);
    assert(ad9361_tx_read_status(tx, &owner, &status) == ENXIO);
    // Preparation must use the validated attach values, not new properties.
    dt_speed = 20000000;
    dt_mode = SPI_MODE_0;
    assert(ad9361_tx_prepare_reset(tx, &owner) == 0 && pins[1].level == 1);
    assert(configurations == 3 && transfers == 2);
    assert(configured_speeds[1] == 1000000 && configured_speeds[2] == 10000000);
    assert(configured_modes[1] == 3 && configured_modes[2] == 3);
    assert(read_speeds[0] == 1000000 && read_speeds[1] == 10000000);
    dt_speed = 10000000;
    dt_mode = SPI_MODE_CPHA | SPI_MODE_CPOL;
    assert(ad9361_tx_prepare_reset(tx, &owner) == EALREADY);
    count = resets;
    assert(ad9361_tx_bootstrap(tx, &owner, 2440000000ULL) == 0);
    assert(resets == count + 2 && pins[1].level == 1);
    assert(ad9361_tx_bootstrap(tx, &owner, 2440000000ULL) == EALREADY);
    assert(ad9361_tx_read_status(tx, &owner, NULL) == EINVAL);
    assert(ad9361_tx_read_status(tx, &owner, &status) == 0);
    assert(status.frequency_hz == 2440000000ULL && status.attenuation_mdB[0] == 89750);
    assert(status.sample_rate_hz == 13000000 && status.ensm_state == 10);
    assert(status.tx_enable == 0xc0 && status.rx_delay == 0x62 && status.tx_delay == 0x74);
    assert(status.pll_locked && status.bbpll_locked && status.quadrature_calibrated &&
        !status.digital_tuning_valid);
    assert(ad9361_tx_set_attenuation(tx, &owner, 10000) == 0);
    assert(ad9361_tx_retune(tx, &owner, 2450000000ULL) == 0);
    assert(ad9361_tx_read_status(tx, &owner, &status) == 0);
    assert(status.frequency_hz == 2450000000ULL && status.attenuation_mdB[1] == 10000);
    assert(ad9361_tx_lease_mute(tx, &owner) == 0);
    assert(ad9361_tx_lease_fastlock_prepare(tx, &owner, frequencies, 2) == 0);
    assert(ad9361_tx_lease_fastlock_stage(tx, &owner, 1) == 0 && fastlock_index == 1);
    assert(ad9361_tx_lease_fastlock_recall(tx, &owner, 1) == 0 && fastlock_index == 1);
    assert(ad9361_tx_read_status(tx, &owner, &status) == 0);
    assert(status.frequency_hz == frequencies[0] && fastlock_calls == 3);
    assert(ad9361_tx_read_status(tx, &owner, &status) == 0 && status.attenuation_mdB[0] == 89750);
    assert(ad9361_tx_release(tx, &owner) == 0 && pins[1].level == 0);
    assert(ad9361_tx_prepare_reset(tx, &owner) == EPERM);
    assert(ad9361_tx_release(tx, &owner) == EPERM);

    // Every native control failure invalidates the PHY and requires a new lease.
    for (n = 0; n < 8; n++) {
        assert(ad9361_tx_acquire(1, &owner, &tx) == 0);
        assert(ad9361_tx_prepare_reset(tx, &owner) == 0);
        native_error = n == 0 ? EIO : 0;
        assert(ad9361_tx_bootstrap(tx, &owner, 2440000000ULL) == native_error);
        native_error = EIO;
        if (n == 1) assert(ad9361_tx_retune(tx, &owner, 2450000000ULL) == EIO);
        if (n == 2) assert(ad9361_tx_set_attenuation(tx, &owner, 10000) == EIO);
        if (n == 3) assert(ad9361_tx_lease_mute(tx, &owner) == EIO);
        if (n == 4) assert(ad9361_tx_read_status(tx, &owner, &status) == EIO);
        if (n == 5) assert(ad9361_tx_lease_fastlock_prepare(tx, &owner, frequencies, 2) == EIO);
        if (n == 6) assert(ad9361_tx_lease_fastlock_stage(tx, &owner, 1) == EIO);
        if (n == 7) assert(ad9361_tx_lease_fastlock_recall(tx, &owner, 1) == EIO);
        assert(pins[1].level == 0 && devices[1].sc_phy == NULL);
        assert(ad9361_tx_prepare_reset(tx, &owner) == EALREADY);
        assert(ad9361_tx_bootstrap(tx, &owner, 2440000000ULL) == EALREADY);
        native_error = 0;
        assert(ad9361_tx_release(tx, &owner) == 0);
    }
    // Each failed rate configuration or ID check closes this lease's transport.
    for (n = 0; n < 6; n++) {
        int expected = n < 2 ? EIO : n < 4 ? ETIMEDOUT : ENXIO;
        assert(ad9361_tx_acquire(1, &owner, &tx) == 0);
        assert(devices[1].sc_native.first_error == 0);
        configure_fail_at = n < 2 ? configurations+n+1 : 0;
        read_fail_at = n >= 2 && n < 4 ? transfers+n-1 : 0;
        bad_id_at = n >= 4 ? transfers+(n == 4 ? 1 : 2) : 0;
        // The fast mismatch keeps the product-family mask valid.
        bad_id_value = n == 4 ? 0 : AD9361_PRODUCT_ID_9361 | 3;
        assert(ad9361_tx_prepare_reset(tx, &owner) == expected);
        assert(devices[1].sc_native.first_error == expected);
        assert(pins[1].level == 0 && !devices[1].sc_tx_prepared);
        assert(devices[1].sc_phy == NULL && !devices[1].sc_tx_poisoned);
        count = transfers;
        assert(ad9361_tx_prepare_reset(tx, &owner) == EALREADY);
        assert(ad9361_tx_bootstrap(tx, &owner, 2440000000ULL) == EALREADY);
        assert(ad9361_tx_read_status(tx, &owner, &status) == ENXIO);
        assert(transfers == count && devices[1].sc_native.first_error == expected);
        configure_fail_at = read_fail_at = bad_id_at = 0;
        assert(ad9361_tx_release(tx, &owner) == 0);
    }
    assert(ad9361_tx_acquire(1, &owner, &tx) == 0);
    assert(ad9361_tx_prepare_reset(tx, &owner) == 0);
    assert(ad9361_tx_bootstrap(tx, &owner, 2440000000ULL) == 0);
    reset_mismatch = true;
    assert(ad9361_tx_release(tx, &owner) == EIO);
    reset_mismatch = false;
    assert(ad9361_tx_acquire(1, &owner, &tx) == EIO && tx == NULL);

    sa.sa_cookie = 2;
    rx_property = true;
    ad9361_attach(NULL, &devices[2], &sa);
    assert(ad9361_tx_acquire(2, &owner, &tx) == ENXIO && !pins[2].acquired);
    tx_property = false;
    sa.sa_cookie = 3;
    ad9361_attach(NULL, &devices[3], &sa);
    assert(probes == 1 && ad9361_tx_acquire(3, &owner, &tx) == ENXIO);
    assert(spi.speed == 1000000 && devices[3].sc_spi_speed == 10000000);
    tx_property = true;
    rx_property = false;
    count = transfers;
    for (n = 4; n < 11; n++) {
        sa.sa_cookie = n;
        clock_missing = n == 4;
        gpio_missing = n == 5;
        params_error = n == 6 ? EINVAL : 0;
        spi_error = n == 7 ? EINVAL : 0;
        clock_error = n == 8 ? EIO : 0;
        reset_mismatch = n == 9;
        bad_wiring = n == 10;
        ad9361_attach(NULL, &devices[n], &sa);
        assert(ad9361_tx_acquire(n, &owner, &tx) == ENXIO && !pins[n].acquired);
    }
    assert(probes == 1 && transfers == count);
    assert(bootstraps == 10 && destroys >= 10);
    clock_missing = gpio_missing = reset_mismatch = bad_wiring = false;
    spi_error = params_error = clock_error = 0;
    dt_speed = 500000;
    sa.sa_cookie = 11;
    ad9361_attach(NULL, &devices[11], &sa);
    assert(spi.speed == 500000);
    assert(ad9361_tx_acquire(11, &owner, &tx) == 0);
    count = transfers;
    assert(ad9361_tx_prepare_reset(tx, &owner) == 0);
    assert(transfers == count+1 && read_speeds[count] == 500000);
    assert(ad9361_tx_release(tx, &owner) == 0);
    for (n = 12; n < 14; n++) {
        dt_speed = n == 12 ? 0 : 50000001;
        sa.sa_cookie = n;
        count = configurations;
        ad9361_attach(NULL, &devices[n], &sa);
        assert(configurations == count);
        assert(ad9361_tx_acquire(n, &owner, &tx) == ENXIO);
    }
    for (n = 0; n < 16; n++) {
        assert(devices[n].sc_phy == NULL);
        free(devices[n].sc_tx_params);
    }
    puts("PASS AD9361 TX provider: conservative probe, validated SPI promotion, fail-closed transport, unchanged RX, ownership, reset lifecycle, poison.");
    return 0;
}
