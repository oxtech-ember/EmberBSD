/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute production SDIO CMD52 and checked function-CIS decoding. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __predict_false
#define __predict_false(value) __builtin_expect(!!(value), 0)
#endif
#ifndef __BIT
#define __BIT(n) (1U << (n))
#endif

typedef unsigned char u_char;
typedef unsigned int u_int;
typedef struct test_device *device_t;

struct sdmmc_softc;
struct sdmmc_function {
	struct sdmmc_softc *sc;
#include "function-number.inc"
};
struct test_device {
	struct sdmmc_softc *sc;
};
struct sdmmc_command {
	uint32_t c_opcode, c_arg, c_flags, c_resp[4];
};

#include <dev/sdmmc/sdmmc_ioreg.h>
#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmc_io_status.h>
#include <dev/pcmcia/pcmciareg.h>
#include "command-flags.inc"

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		abort(); \
	} \
} while (0)
#define KASSERT CHECK
#define device_printf(dev, ...) ((void)(dev))
#define SDIO_FUNCE_TYPE_FUNCTION 1 /* SDIO function extension */

struct model {
	uint8_t registers[1U << 17];
	unsigned commands, writes, selects;
	unsigned fail_command;
	int command_error, select_error;
	uint32_t command_status;
};

struct sdmmc_softc {
	device_t sc_dev;
	struct sdmmc_function *sc_fn0;
	struct model *model;
};

struct fixture {
	struct model model;
	struct sdmmc_softc sc;
	struct test_device dev;
	struct sdmmc_function fn0, fn1;
};

int sdmmc_io_maxblklen_checked(struct sdmmc_function *, u_int *);
static int sdmmc_select_card(struct sdmmc_softc *,
    const struct sdmmc_function *);
static int sdmmc_mmc_command(struct sdmmc_softc *, struct sdmmc_command *);

#include "production.inc"

static int
sdmmc_select_card(struct sdmmc_softc *sc, const struct sdmmc_function *sf)
{

	CHECK(sf == sc->sc_fn0);
	sc->model->selects++;
	return sc->model->select_error;
}

static int
sdmmc_mmc_command(struct sdmmc_softc *sc, struct sdmmc_command *cmd)
{
	struct model *m = sc->model;
	unsigned address, function;
	uint8_t value;
	bool write;

	CHECK(cmd->c_opcode == SD_IO_RW_DIRECT);
	CHECK(cmd->c_flags == (SCF_CMD_AC | SCF_RSP_R5));
	address = (cmd->c_arg >> SD_ARG_CMD52_REG_SHIFT) &
	    SD_ARG_CMD52_REG_MASK;
	function = (cmd->c_arg >> SD_ARG_CMD52_FUNC_SHIFT) &
	    SD_ARG_CMD52_FUNC_MASK;
	write = (cmd->c_arg & SD_ARG_CMD52_WRITE) != 0;
	CHECK(function == 0);
	m->commands++;
	value = m->registers[address];
	if (write) {
		m->writes++;
		value = cmd->c_arg & SD_ARG_CMD52_DATA_MASK;
		m->registers[address] = value;
	}
	cmd->c_resp[0] = value;
	if (m->commands == m->fail_command) {
		cmd->c_resp[0] |= m->command_status;
		return m->command_error;
	}
	return 0;
}

static void
setup(struct fixture *f, unsigned revision)
{

	memset(f, 0, sizeof(*f));
	f->dev.sc = &f->sc;
	f->fn0.sc = f->fn1.sc = &f->sc;
	f->fn0.number = 0;
	f->fn1.number = 1;
	f->sc.sc_dev = &f->dev;
	f->sc.sc_fn0 = &f->fn0;
	f->sc.model = &f->model;
	f->model.registers[SD_IO_CCCR_CCCR_SDIO_REV] =
	    revision << 4;
}

static void
set_cis_pointer(struct fixture *f, int function, uint32_t pointer)
{
	unsigned reg = SD_IO_FBR(function) + SD_IO_FBR_CIS;

	f->model.registers[reg] = pointer & 0xff;
	f->model.registers[reg + 1] = (pointer >> 8) & 0xff;
	f->model.registers[reg + 2] = (pointer >> 16) & 0xff;
}

static unsigned
append_tuple(struct fixture *f, unsigned pos, uint8_t code, uint8_t length,
    const uint8_t *payload)
{
	f->model.registers[pos++] = code;
	f->model.registers[pos++] = length;
	if (length != 0 && payload != NULL) {
		memcpy(&f->model.registers[pos], payload, length);
		pos += length;
	}
	return pos;
}

static unsigned
append_funce(struct fixture *f, unsigned pos, unsigned revision,
    unsigned length, unsigned maximum)
{
	uint8_t payload[0x2a];

	CHECK(length <= sizeof(payload));
	memset(payload, 0, sizeof(payload));
	payload[0] = SDIO_FUNCE_TYPE_FUNCTION;
	if (length > 12)
		payload[12] = maximum & 0xff;
	if (length > 13)
		payload[13] = (maximum >> 8) & 0xff;
	(void)revision;
	return append_tuple(f, pos, PCMCIA_CISTPL_FUNCE, length, payload);
}

static unsigned
make_valid_cis(struct fixture *f, unsigned revision, unsigned maximum)
{
	unsigned pos = SD_IO_CIS_START;

	set_cis_pointer(f, 1, pos);
	f->model.registers[pos++] = PCMCIA_CISTPL_NULL;
	pos = append_tuple(f, pos, PCMCIA_CISTPL_FUNCID, 2,
	    (const uint8_t[]){ PCMCIA_FUNCTION_NETWORK, 0 });
	pos = append_funce(f, pos, revision, revision == 0 ? 0x1c : 0x2a,
	    maximum);
	f->model.registers[pos++] = PCMCIA_CISTPL_END;
	return pos;
}

static void
test_valid_values(void)
{
	struct fixture f;
	u_int maximum;

	setup(&f, CCCR_SDIO_REV_1_00);
	(void)make_valid_cis(&f, CCCR_SDIO_REV_1_00, 512);
	maximum = 0xdeadbeefU;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == 0);
	CHECK(maximum == 512);
	CHECK(f.model.writes == 0);
	CHECK(f.model.commands != 0 && f.model.selects == f.model.commands);

	setup(&f, CCCR_SDIO_REV_1_10);
	(void)make_valid_cis(&f, CCCR_SDIO_REV_1_10, 256);
	maximum = 0;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == 0);
	CHECK(maximum == 256);
	CHECK(f.model.writes == 0);

	setup(&f, CCCR_SDIO_REV_1_10);
	(void)make_valid_cis(&f, CCCR_SDIO_REV_1_10, 65535);
	maximum = 0;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == 0);
	CHECK(maximum == 65535U);
	CHECK(f.model.writes == 0);
}

static void
test_conflicting_funce_rejected(void)
{
	struct fixture f;
	u_int maximum = 0xdeadbeefU;
	unsigned pos;

	setup(&f, CCCR_SDIO_REV_1_10);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	pos = append_funce(&f, SD_IO_CIS_START, CCCR_SDIO_REV_1_10, 0x2a,
	    512);
	pos = append_funce(&f, pos, CCCR_SDIO_REV_1_10, 0x2a, 256);
	f.model.registers[pos] = PCMCIA_CISTPL_END;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU);
	CHECK(f.model.writes == 0);
}

static void
test_argument_validation(void)
{
	struct fixture f;
	u_int maximum = 0xdeadbeefU;

	setup(&f, CCCR_SDIO_REV_1_00);
	CHECK(sdmmc_io_maxblklen_checked(NULL, &maximum) == EINVAL);
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, NULL) == EINVAL);
	f.fn1.number = -1;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	f.fn1.number = 0;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	f.fn1.number = 8;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	f.fn1.number = 1;
	f.fn1.sc = NULL;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	f.fn1.sc = &f.sc;
	f.sc.sc_fn0 = NULL;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	f.sc.sc_fn0 = &f.fn0;
	f.fn0.sc = NULL;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	f.fn0.sc = &f.sc;
	f.fn0.number = 1;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU);
	CHECK(f.model.commands == 0 && f.model.writes == 0);
}

static void
test_pointer_and_tuple_errors(void)
{
	struct fixture f;
	uint8_t payload[0x2a] = { 0 };
	u_int maximum;
	unsigned pos;

	payload[0] = SDIO_FUNCE_TYPE_FUNCTION;
	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START - 1);
	maximum = 0xdeadbeefU;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU && f.model.writes == 0);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START + SD_IO_CIS_SIZE);
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU && f.model.writes == 0);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	(void)append_tuple(&f, SD_IO_CIS_START, PCMCIA_CISTPL_FUNCID, 0, NULL);
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	(void)append_tuple(&f, SD_IO_CIS_START, PCMCIA_CISTPL_FUNCE, 27,
	    payload);
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_10);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	(void)append_tuple(&f, SD_IO_CIS_START, PCMCIA_CISTPL_FUNCE, 41,
	    payload);
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	pos = append_tuple(&f, SD_IO_CIS_START, PCMCIA_CISTPL_FUNCE, 1,
	    (const uint8_t[]){ 0x7f });
	f.model.registers[pos++] = PCMCIA_CISTPL_END;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == ENOTSUP);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	pos = append_funce(&f, SD_IO_CIS_START, CCCR_SDIO_REV_1_00, 0x1c, 0);
	f.model.registers[pos++] = PCMCIA_CISTPL_END;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == ENOTSUP);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	f.model.registers[SD_IO_CIS_START] = PCMCIA_CISTPL_END;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == ENOTSUP);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START + SD_IO_CIS_SIZE - 1);
	f.model.registers[SD_IO_CIS_START + SD_IO_CIS_SIZE - 1] =
	    PCMCIA_CISTPL_FUNCID;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START + SD_IO_CIS_SIZE - 3);
	f.model.registers[SD_IO_CIS_START + SD_IO_CIS_SIZE - 3] =
	    PCMCIA_CISTPL_FUNCID;
	f.model.registers[SD_IO_CIS_START + SD_IO_CIS_SIZE - 2] = 4;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	pos = append_funce(&f, SD_IO_CIS_START, CCCR_SDIO_REV_1_00, 0x1c, 512);
	/* A value is not published until a terminating END tuple is seen. */
	CHECK(pos == SD_IO_CIS_START + 30);
	set_cis_pointer(&f, 1, SD_IO_CIS_START + SD_IO_CIS_SIZE - 30);
	(void)append_funce(&f, SD_IO_CIS_START + SD_IO_CIS_SIZE - 30,
	    CCCR_SDIO_REV_1_00, 0x1c, 512);
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EINVAL);
	CHECK(maximum == 0xdeadbeefU);
}

static void
test_tuple_and_byte_limits(void)
{
	struct fixture f;
	uint8_t payload[255];
	u_int maximum = 0xdeadbeefU;
	unsigned pos, i;

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	pos = SD_IO_CIS_START;
	for (i = 0; i < 254; i++)
		f.model.registers[pos++] = PCMCIA_CISTPL_NULL;
	pos = append_funce(&f, pos, CCCR_SDIO_REV_1_00, 0x1c, 512);
	f.model.registers[pos++] = PCMCIA_CISTPL_END;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == 0);
	CHECK(maximum == 512 && f.model.writes == 0);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	pos = SD_IO_CIS_START;
	for (i = 0; i < 255; i++)
		f.model.registers[pos++] = PCMCIA_CISTPL_NULL;
	pos = append_funce(&f, pos, CCCR_SDIO_REV_1_00, 0x1c, 512);
	f.model.registers[pos++] = PCMCIA_CISTPL_END;
	maximum = 0xdeadbeefU;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EFBIG);
	CHECK(maximum == 0xdeadbeefU);

	memset(payload, 0xa5, sizeof(payload));
	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	pos = SD_IO_CIS_START;
	for (i = 0; i < 15; i++)
		pos = append_tuple(&f, pos, 0x20, 255, payload);
	pos = append_tuple(&f, pos, 0x20, 238, payload);
	CHECK(pos - SD_IO_CIS_START == 4095);
	f.model.registers[pos++] = PCMCIA_CISTPL_END;
	CHECK(pos - SD_IO_CIS_START == 4096);
	maximum = 0xdeadbeefU;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == ENOTSUP);
	CHECK(maximum == 0xdeadbeefU);

	setup(&f, CCCR_SDIO_REV_1_00);
	set_cis_pointer(&f, 1, SD_IO_CIS_START);
	pos = SD_IO_CIS_START;
	for (i = 0; i < 15; i++)
		pos = append_tuple(&f, pos, 0x20, 255, payload);
	pos = append_tuple(&f, pos, 0x20, 239, payload);
	CHECK(pos - SD_IO_CIS_START == 4096);
	maximum = 0xdeadbeefU;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EFBIG);
	CHECK(maximum == 0xdeadbeefU);
}

static void
test_read_failures(void)
{
	struct fixture f;
	u_int maximum;
	unsigned reads, fail;

	setup(&f, CCCR_SDIO_REV_1_10);
	(void)make_valid_cis(&f, CCCR_SDIO_REV_1_10, 512);
	maximum = 0;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == 0);
	reads = f.model.commands;
	CHECK(reads >= 8 && f.model.writes == 0);

	for (fail = 1; fail <= reads; fail++) {
		setup(&f, CCCR_SDIO_REV_1_10);
		(void)make_valid_cis(&f, CCCR_SDIO_REV_1_10, 512);
		f.model.fail_command = fail;
		f.model.command_error = EAGAIN;
		maximum = 0xdeadbeefU;
		CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EAGAIN);
		CHECK(maximum == 0xdeadbeefU);
		CHECK(f.model.commands == fail && f.model.writes == 0);

		setup(&f, CCCR_SDIO_REV_1_10);
		(void)make_valid_cis(&f, CCCR_SDIO_REV_1_10, 512);
		f.model.fail_command = fail;
		f.model.command_status = 0x8000;
		maximum = 0xdeadbeefU;
		CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == EILSEQ);
		CHECK(maximum == 0xdeadbeefU);
		CHECK(f.model.commands == fail && f.model.writes == 0);
	}

	setup(&f, CCCR_SDIO_REV_1_10);
	(void)make_valid_cis(&f, CCCR_SDIO_REV_1_10, 512);
	f.model.select_error = ENXIO;
	maximum = 0xdeadbeefU;
	CHECK(sdmmc_io_maxblklen_checked(&f.fn1, &maximum) == ENXIO);
	CHECK(maximum == 0xdeadbeefU);
	CHECK(f.model.commands == 0 && f.model.writes == 0);
}

int
main(void)
{

	test_valid_values();
	test_conflicting_funce_rejected();
	test_argument_validation();
	test_pointer_and_tuple_errors();
	test_tuple_and_byte_limits();
	test_read_failures();
	puts("SDMMC checked CIS max block length: PASS");
	return 0;
}
