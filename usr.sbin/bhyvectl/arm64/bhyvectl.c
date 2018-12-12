/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Alexandru Elisei <alexandru.elisei@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/cpuset.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <libutil.h>
#include <fcntl.h>
#include <getopt.h>
#include <time.h>
#include <assert.h>
#include <libutil.h>

#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include <vmmapi.h>

#define	MB	(1UL << 20)
#define	GB	(1UL << 30)

#define	REQ_ARG		required_argument
#define	NO_ARG		no_argument
#define	OPT_ARG		optional_argument

static const char *progname;

static void
usage()
{

	(void)fprintf(stderr,
	"Usage: %s --vm=<vmname>\n"
	"       %*s [--destroy]\n",
	progname, (int)strlen(progname), "");
	exit(1);
}

static int create;
static int destroy;

enum {
	VMNAME = 1000,	/* avoid collision with return values from getopt */
};

static struct option *
setup_options(bool cpu_intel)
{
	const struct option common_opts[] = {
		{ "vm",		REQ_ARG,	NULL,		VMNAME },
		{ "destroy",	NO_ARG,		&destroy,	1 },
	};
	const struct option null_opt = {
		NULL, 0, NULL, 0
	};
	struct option *all_opts;
	char *cp;
	int optlen;

	optlen = sizeof(common_opts);
	optlen += sizeof(null_opt);
	all_opts = malloc(optlen);
	if (all_opts == NULL) {
		perror("malloc");
		exit(1);
	}

	cp = (char *)all_opts;
	memcpy(cp, common_opts, sizeof(common_opts));
	cp += sizeof(common_opts);
	memcpy(cp, &null_opt, sizeof(null_opt));
	cp += sizeof(null_opt);

	return (all_opts);
}

static const char *
wday_str(int idx)
{
	static const char *weekdays[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};

	if (idx >= 0 && idx < 7)
		return (weekdays[idx]);
	else
		return ("UNK");
}

static const char *
mon_str(int idx)
{
	static const char *months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	if (idx >= 0 && idx < 12)
		return (months[idx]);
	else
		return ("UNK");
}

int
main(int argc, char *argv[])
{
	char *vmname;
	int error, ch;
	struct vmctx *ctx;
	struct option *opts;

	vmname = NULL;
	progname = basename(argv[0]);

	while ((ch = getopt_long(argc, argv, "", opts, NULL)) != -1) {
		switch (ch) {
		case 0:
			break;
		case VMNAME:
			vmname = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (vmname == NULL)
		usage();

	error = 0;
	if (!error && create)
		error = vm_create(vmname);
	if (!error) {
		ctx = vm_open(vmname);
		if (ctx == NULL) {
			printf("VM:%s is not created.\n", vmname);
			exit(1);
		}
	}


	if (error)
		printf("errno = %d\n", errno);

	if (!error && destroy)
		vm_destroy(ctx);

	free (opts);
	exit(error);
}
