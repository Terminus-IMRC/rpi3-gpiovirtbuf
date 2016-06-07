/*
 * rpi3-gpiovirtbuf.c -- Control Raspberry Pi 3's activity LED by using the Mailbox interface
 *
 * Copyright (c) 2016 Sugizaki Yukimasa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "raspberrypi-firmware.h"

#define RPI_FIRMWARE_DEV "/dev/vcio"
#define IOCTL_RPI_FIRMWARE_PROPERTY _IOWR(100, 0, char*)

#define DEV_MEM "/dev/mem"

#define BUS_TO_PHYS(addr) ((((addr)) & ~0xc0000000))

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s NUM\n", progname);
	fprintf(stderr, "Set NUM to 0 to turn off the activity LED and non-0 to turn it on.\n");
	fprintf(stderr, "This program requires root privilege to map memory.\n");
}

static int rpi_firmware_open()
{
	int fd;

	fd = open(RPI_FIRMWARE_DEV, O_NONBLOCK);
	if (fd == -1) {
		fprintf(stderr, "error: open: %s: %s\n", RPI_FIRMWARE_DEV, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return fd;
}

static void rpi_firmware_close(const int fd)
{
	int reti;

	reti = close(fd);
	if (reti == -1) {
		fprintf(stderr, "error: close: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void rpi_firmware_property(const int fd, const uint32_t tag, void *tag_data, const size_t buf_size)
{
	int i;
	uint32_t *p = NULL;
	int reti;

	p = malloc((5 + buf_size / 4 + 1) * sizeof(*p));
	if (p == NULL) {
		fprintf(stderr, "error: Failed to allocate memory for RPi firmware\n");
		exit(EXIT_FAILURE);
	}

	i = 0;
	p[i++] = (5 + buf_size / 4 + 1) * sizeof(*p);
	p[i++] = RPI_FIRMWARE_STATUS_REQUEST;
	p[i++] = tag; // tag
	p[i++] = buf_size; // buf_size
	p[i++] = 0; // req_resp_size
	memcpy(p + i, tag_data, buf_size);
	p[i + buf_size / 4] = RPI_FIRMWARE_PROPERTY_END;;

	reti = ioctl(fd, IOCTL_RPI_FIRMWARE_PROPERTY, p);
	if (reti == -1) {
		fprintf(stderr, "error: ioctl: IOCTL_RPI_FIRMWARE_PROPERTY: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (p[1] != RPI_FIRMWARE_STATUS_SUCCESS) {
		fprintf(stderr, "error: RPi firmware returned 0x%08x\n", p[1]);
		exit(EXIT_FAILURE);
	}

	memcpy(tag_data, p + i, buf_size);
}

static void gpio_set(uint32_t *addr, const unsigned off, const int val)
{
	uint16_t enables, disables;
	int16_t diff;
	_Bool lit;

	enables = addr[off] >> 16;
	disables = addr[off] >> 0;
	diff = (int16_t)(enables - disables);
	lit = (diff > 0);
	if ((val && lit) || (!val && !lit))
		return;
	if (val)
		enables ++;
	else
		disables ++;
	addr[off] = (enables << 16) | (disables << 0);
}

/* Note: base should be pagesize-aligned. */
static void* mapmem_cpu(const off_t base, const size_t size)
{
	int fd = -1;
	unsigned *mem = NULL;
	int reti;

	fd = open(DEV_MEM, O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "error: open: %s: %s\n", DEV_MEM, strerror(errno));
		exit(EXIT_FAILURE);
	}

	mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
	if (mem == MAP_FAILED) {
		fprintf(stderr, "error: mmap: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	reti = close(fd);
	if (reti == -1) {
		fprintf(stderr, "error: close: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	return mem;
}

static void unmapmem_cpu(void *addr, const size_t size)
{
	int reti;

	reti = munmap(addr, size);

	if (reti == -1) {
		fprintf(stderr, "error: munmap: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[])
{
	int mb = -1;
	uint32_t gvp = NULL;
	unsigned *addr = NULL;
	unsigned off;
	int val;

	if (argc != 2) {
		fprintf(stderr, "error: Invalid the number of the arguments\n");
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	off = 0; /* 0 is for the activity LED. */
	val = atoi(argv[1]);

	mb = rpi_firmware_open();

	rpi_firmware_property(mb, RPI_FIRMWARE_FRAMEBUFFER_GET_GPIOVIRTBUF, &gvp, sizeof(gvp));
	addr = mapmem_cpu(BUS_TO_PHYS(gvp), 4096);
	gpio_set(addr, off, val);
	unmapmem_cpu(addr, 4096);
	addr = NULL;

	rpi_firmware_close(mb);
	mb = -1;

	return 0;
}
