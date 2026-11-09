/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD Realtek Bluetooth patch loader, AI-assisted. */
/*
 * rtlbtfw: load the patch firmware into a Realtek Bluetooth controller.
 *
 * The controller boots with ROM firmware. The patch is sent with vendor HCI
 * commands over the raw HCI socket of the attached unit. The protocol facts
 * come from FreeBSD rtlbtfw(8) and the firmware files from linux-firmware.
 */

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <bluetooth.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "firmload.h"
#include "rtlbt_fw.h"

#define RTLBT_FW_DRIVER		"rtl_bt"
#define RTLBT_CONFIG_SUFFIX	"_config.bin"

/* Vendor commands, OGF 0x3f. */
#define RTLBT_CMD_READ_ROM_VERSION	HCI_OPCODE(HCI_OGF_VENDOR, 0x006d)
#define RTLBT_CMD_DOWNLOAD		HCI_OPCODE(HCI_OGF_VENDOR, 0x0020)

#define RTLBT_CMD_TIMEOUT	10	/* seconds per HCI command, as btconfig(8) */
#define RTLBT_VERSION_RETRIES	5	/* reads after the download */
#define RTLBT_RESET_POLLS	50	/* 100 ms polls for BTF_INIT */

static int verbose;

__dead static void
usage(void)
{

	fprintf(stderr, "usage: %s [-nv] [-c config] [-f firmware] device\n",
	    getprogname());
	exit(EXIT_FAILURE);
}

static void note(const char *, ...) __printflike(1, 2);

static void
note(const char *fmt, ...)
{
	va_list ap;

	if (!verbose)
		return;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
}

/* Read a whole file. Return the length or -1 with errno set. */
static ssize_t
read_file(int fd, uint8_t **out)
{
	struct stat st;
	uint8_t *buf;
	ssize_t n;
	size_t off;

	if (fstat(fd, &st) == -1)
		return -1;
	if (st.st_size <= 0 || st.st_size > 1024 * 1024) {
		errno = EFBIG;
		return -1;
	}
	buf = malloc((size_t)st.st_size);
	if (buf == NULL)
		return -1;
	for (off = 0; off < (size_t)st.st_size; off += (size_t)n) {
		n = read(fd, buf + off, (size_t)st.st_size - off);
		if (n <= 0) {
			if (n == 0)
				errno = EIO;
			free(buf);
			return -1;
		}
	}
	*out = buf;
	return st.st_size;
}

/*
 * Open an explicit path or search hw.firmware.path for rtl_bt/<name>.
 * A failed search leaves errno at ENOENT; an explicit path keeps its own.
 */
static ssize_t
load_blob(const char *path, const char *name, uint8_t **out)
{
	int fd;
	ssize_t len;

	if (path != NULL)
		fd = open(path, O_RDONLY);
	else if ((fd = firmware_open(RTLBT_FW_DRIVER, name)) == -1)
		errno = ENOENT;
	if (fd == -1)
		return -1;
	len = read_file(fd, out);
	close(fd);
	return len;
}

static int
hci_request(int s, uint16_t opcode, void *cparam, size_t clen, void *rparam,
    size_t rlen)
{
	struct bt_devreq req;

	memset(&req, 0, sizeof(req));
	req.opcode = opcode;
	req.cparam = cparam;
	req.clen = clen;
	req.rparam = rparam;
	req.rlen = rlen;
	return bt_devreq(s, &req, RTLBT_CMD_TIMEOUT);
}

static int
read_version(int s, struct rtlbt_version *v)
{
	hci_read_local_ver_rp rp;

	if (hci_request(s, HCI_CMD_READ_LOCAL_VER, NULL, 0, &rp, sizeof(rp))
	    == -1)
		return -1;
	if (rp.status != 0) {
		errno = EIO;
		return -1;
	}
	v->hci_version = rp.hci_version;
	v->hci_revision = le16toh(rp.hci_revision);
	v->lmp_version = rp.lmp_version;
	v->manufacturer = le16toh(rp.manufacturer);
	v->lmp_subversion = le16toh(rp.lmp_subversion);
	return 0;
}

static int
read_rom_version(int s, uint8_t *rom)
{
	uint8_t rp[2];

	if (hci_request(s, RTLBT_CMD_READ_ROM_VERSION, NULL, 0, rp, sizeof(rp))
	    == -1)
		return -1;
	if (rp[0] != 0) {
		errno = EIO;
		return -1;
	}
	*rom = rp[1];
	return 0;
}

static int
enable_unit(int s, struct btreq *btr)
{

	btr->btr_flags |= BTF_UP;
	if (ioctl(s, SIOCSBTFLAGS, btr) == -1)
		return -1;
	return ioctl(s, SIOCGBTINFO, btr);
}

/*
 * Reset the controller and let the stack repeat its initialisation, as
 * "btconfig reset" does. The stack clears BTF_INIT when it has read the
 * buffer sizes, address and features of the patched firmware. A failed
 * reset leaves BTF_INIT set until the stack completes a later reset.
 */
static int
reset_unit(int s, struct btreq *btr)
{
	static const struct timespec ts = { 0, 100000000 };	/* 100 ms */
	int i;

	if (ioctl(s, SIOCGBTINFO, btr) == -1)
		return -1;
	btr->btr_flags |= BTF_INIT;
	if (ioctl(s, SIOCSBTFLAGS, btr) == -1)
		return -1;
	if (hci_request(s, HCI_CMD_RESET, NULL, 0, NULL, 0) == -1)
		return -1;
	for (i = 0; i < RTLBT_RESET_POLLS; i++) {
		nanosleep(&ts, NULL);
		if (ioctl(s, SIOCGBTINFO, btr) == -1)
			return -1;
		if ((btr->btr_flags & BTF_INIT) == 0)
			return 0;
	}
	errno = ETIMEDOUT;
	return -1;
}

static int
connect_unit(int s, const struct btreq *btr)
{
	struct sockaddr_bt sa;

	memset(&sa, 0, sizeof(sa));
	sa.bt_len = sizeof(sa);
	sa.bt_family = AF_BLUETOOTH;
	bdaddr_copy(&sa.bt_bdaddr, &btr->btr_bdaddr);
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) == -1)
		return -1;
	return connect(s, (struct sockaddr *)&sa, sizeof(sa));
}

static int
download(int s, const struct rtlbt_image *img)
{
	uint8_t cmd[1 + RTLBT_FRAG_LEN], rp[2];
	size_t count, i, len, off;

	count = rtlbt_fragment_count(img->len);
	for (i = 0; i < count; i++) {
		len = rtlbt_fragment(img->len, i, &cmd[0], &off);
		memcpy(cmd + 1, img->data + off, len);
		if (hci_request(s, RTLBT_CMD_DOWNLOAD, cmd, len + 1, rp,
		    sizeof(rp)) == -1) {
			warn("fragment %zu of %zu", i + 1, count);
			return -1;
		}
		if (rp[0] != 0 || rp[1] != cmd[0]) {
			warnx("fragment %zu of %zu: status 0x%02x index 0x%02x",
			    i + 1, count, rp[0], rp[1]);
			return -1;
		}
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	struct btreq btr;
	struct rtlbt_version ver, after;
	struct rtlbt_image img;
	const struct rtlbt_chip *chip;
	const char *cfgpath = NULL, *fwpath = NULL, *why, *dev;
	char name[64];
	uint8_t *fw, *cfg = NULL, rom;
	ssize_t fwlen, cfglen = 0;
	int ch, dryrun = 0, error, i, s;

	while ((ch = getopt(argc, argv, "c:f:nv")) != -1) {
		switch (ch) {
		case 'c':
			cfgpath = optarg;
			break;
		case 'f':
			fwpath = optarg;
			break;
		case 'n':
			dryrun = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();
	dev = argv[0];

	s = socket(PF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	if (s == -1)
		err(EXIT_FAILURE, "socket");
	memset(&btr, 0, sizeof(btr));
	strlcpy(btr.btr_name, dev, sizeof(btr.btr_name));
	if (ioctl(s, SIOCGBTINFO, &btr) == -1)
		err(EXIT_FAILURE, "%s", dev);

	/* HCI commands reach the unit only after the stack enabled it. */
	if ((btr.btr_flags & BTF_UP) == 0 && enable_unit(s, &btr) == -1)
		err(EXIT_FAILURE, "%s: enable", dev);
	if (connect_unit(s, &btr) == -1)
		err(EXIT_FAILURE, "%s: connect", dev);

	if (read_version(s, &ver) == -1)
		err(EXIT_FAILURE, "%s: read local version", dev);
	note("%s: hci %u.%04x lmp %u manufacturer 0x%04x subversion 0x%04x",
	    dev, ver.hci_version, ver.hci_revision, ver.lmp_version,
	    ver.manufacturer, ver.lmp_subversion);
	if (ver.manufacturer != RTLBT_MANUFACTURER) {
		note("%s: not a Realtek controller", dev);
		return EXIT_SUCCESS;
	}
	/*
	 * Patched firmware reports its own version in place of the ROM
	 * identity, so a controller outside the ROM table is left alone.
	 */
	chip = rtlbt_chip_lookup(&ver);
	if (chip == NULL) {
		note("%s: no ROM entry for lmp subversion 0x%04x, hci revision "
		    "0x%04x: patch already loaded or unsupported chip", dev,
		    ver.lmp_subversion, ver.hci_revision);
		return EXIT_SUCCESS;
	}
	if (read_rom_version(s, &rom) == -1)
		err(EXIT_FAILURE, "%s: read rom version", dev);
	note("%s: %s rom version %u", dev, chip->name, rom);

	snprintf(name, sizeof(name), "%s%s", chip->name, chip->fw_suffix);
	fwlen = load_blob(fwpath, name, &fw);
	if (fwlen == -1)
		err(EXIT_FAILURE, "%s: firmware %s", dev,
		    fwpath != NULL ? fwpath : name);
	snprintf(name, sizeof(name), "%s%s", chip->name, RTLBT_CONFIG_SUFFIX);
	cfglen = load_blob(cfgpath, name, &cfg);
	if (cfglen == -1) {
		if (cfgpath != NULL || errno != ENOENT ||
		    (chip->flags & RTLBT_CHIP_CONFIG) != 0)
			err(EXIT_FAILURE, "%s: config %s", dev,
			    cfgpath != NULL ? cfgpath : name);
		note("%s: no config file %s", dev, name);
		cfglen = 0;
	}

	error = rtlbt_image_build(fw, (size_t)fwlen, cfg, (size_t)cfglen, rom,
	    ver.lmp_subversion, &img, &why);
	if (error != 0)
		errx(EXIT_FAILURE, "%s: %s", dev, why);
	free(fw);
	free(cfg);
	note("%s: image %zu bytes, %zu fragments", dev, img.len,
	    rtlbt_fragment_count(img.len));
	if (dryrun) {
		printf("%s: %s rom %u: would load %zu bytes\n", dev,
		    chip->name, rom, img.len);
		free(img.data);
		return EXIT_SUCCESS;
	}

	if (download(s, &img) == -1)
		errx(EXIT_FAILURE, "%s: download failed", dev);
	free(img.data);

	/* The controller restarts on the patch. Wait until it answers. */
	for (i = 0; i < RTLBT_VERSION_RETRIES; i++) {
		if (read_version(s, &after) == 0)
			break;
	}
	if (i == RTLBT_VERSION_RETRIES)
		err(EXIT_FAILURE, "%s: no answer after download", dev);
	if (after.hci_revision == ver.hci_revision)
		warnx("%s: controller still reports rom revision 0x%04x", dev,
		    ver.hci_revision);

	if (reset_unit(s, &btr) == -1)
		err(EXIT_FAILURE, "%s: reset", dev);
	printf("%s: %s patch loaded, hci revision 0x%04x -> 0x%04x, "
	    "lmp subversion 0x%04x\n", dev, chip->name, ver.hci_revision,
	    after.hci_revision, after.lmp_subversion);
	close(s);
	return EXIT_SUCCESS;
}
