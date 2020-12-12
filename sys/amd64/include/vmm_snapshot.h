/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Flavius Anton
 * Copyright (c) 2016 Mihai Tiganus
 * Copyright (c) 2016-2019 Mihai Carabas
 * Copyright (c) 2017-2019 Darius Mihai
 * Copyright (c) 2017-2019 Elena Mihailescu
 * Copyright (c) 2018-2019 Sergiu Weisz
 * All rights reserved.
 * The bhyve-snapshot feature was developed under sponsorships
 * from Matthew Grooms.
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

#ifndef _VMM_SNAPSHOT_
#define _VMM_SNAPSHOT_

#include <sys/errno.h>
#include <sys/types.h>

#ifndef _KERNEL
#include <stdbool.h>
#endif

struct vmctx;

enum snapshot_req {
	STRUCT_VMX,
	STRUCT_VIOAPIC,
	STRUCT_VM,
	STRUCT_VLAPIC,
	VM_MEM,
	STRUCT_VHPET,
	STRUCT_VMCX,
	STRUCT_VATPIC,
	STRUCT_VATPIT,
	STRUCT_VPMTMR,
	STRUCT_VRTC,
};

struct vm_snapshot_buffer {
	/*
	 * R/O for device-specific functions;
	 * written by generic snapshot functions.
	 */
	uint8_t *const buf_start;
	const size_t buf_size;

	/*
	 * R/W for device-specific functions used to keep track of buffer
	 * current position and remaining size.
	 */
	uint8_t *buf;
	size_t buf_rem;

	/*
	 * Length of the snapshot is either determined as (buf_size - buf_rem)
	 * or (buf - buf_start) -- the second variation returns a signed value
	 * so it may not be appropriate.
	 *
	 * Use vm_get_snapshot_size(meta).
	 */
};

#ifndef JSON_SNAPSHOT_V2
#define JSON_SNAPSHOT_V2
#define IDENT_LEVEL		10

struct vm_snapshot_device_info {
	unsigned char ident;
	char *field_name;
	char *type;
	int index;
	char *intern_arr_name;
	void *field_data;
	size_t data_size;

	struct vm_snapshot_device_info *next_field;
};

struct list_device_info {
	unsigned char ident;
	const char *intern_arr_names[IDENT_LEVEL];
	int index;

	struct vm_snapshot_device_info *first;
	struct vm_snapshot_device_info *last;
};

#endif

enum vm_snapshot_op {
	VM_SNAPSHOT_SAVE,
	VM_SNAPSHOT_RESTORE,
};

struct vm_snapshot_meta {
	struct vmctx *ctx;
	void *dev_data;
	const char *dev_name;      /* identify userspace devices */
	enum snapshot_req dev_req; /* identify kernel structs */

	struct vm_snapshot_buffer buffer;

#ifdef JSON_SNAPSHOT_V2
	struct list_device_info dev_info_list;
#endif

	enum vm_snapshot_op op;
	unsigned char version;
};

int vm_snapshot_save_fieldname(const char *fullname, volatile void *data,
				char *type, size_t data_size, struct vm_snapshot_meta *meta);
void vm_snapshot_add_intern_list(const char *arr_name,
				struct vm_snapshot_meta *meta);
void vm_snapshot_remove_intern_list(struct vm_snapshot_meta *meta);
void vm_snapshot_set_intern_arr_index(struct vm_snapshot_meta *meta, int index);
void vm_snapshot_clear_intern_arr_index(struct vm_snapshot_meta *meta);
int vm_snapshot_save_fieldname_cmp(const char *fullname, volatile void *data,
				char *type, size_t data_size, struct vm_snapshot_meta *meta);


void vm_snapshot_buf_err(const char *bufname, const enum vm_snapshot_op op);
int vm_snapshot_buf(volatile void *data, size_t data_size,
		    struct vm_snapshot_meta *meta);
size_t vm_get_snapshot_size(struct vm_snapshot_meta *meta);
int vm_snapshot_guest2host_addr(void **addrp, size_t len, bool restore_null,
				struct vm_snapshot_meta *meta);
int vm_snapshot_buf_cmp(volatile void *data, size_t data_size,
			      struct vm_snapshot_meta *meta);

#ifdef JSON_SNAPSHOT_V2

#define SNAPSHOT_ADD_INTERN_ARR(ARR_NAME, META)			\
do {													\
	vm_snapshot_add_intern_list(#ARR_NAME, (META));		\
} while (0)

#define SNAPSHOT_REMOVE_INTERN_ARR(ARR_NAME, META)		\
do {													\
	vm_snapshot_remove_intern_list((META));				\
} while (0)

#define SNAPSHOT_SET_INTERN_ARR_INDEX(META, IDX)		\
do {													\
	vm_snapshot_set_intern_arr_index((META), (IDX));	\
} while (0)

#define SNAPSHOT_CLEAR_INTERN_ARR_INDEX(META)			\
do {													\
	vm_snapshot_clear_intern_arr_index((META));			\
} while (0)

#define FMT_ENC(X) _Generic((X), \
	int *:	 #X ": %d\n",		 \
	float *: #X ": %f\n",		 \
	long *:  #X ": %ld\n",		 \
	default: NULL				 \
)

#define GET_TYPE(X) _Generic((X), \
	int *:   "int",			  	  \
	float *: "float",		 	  \
	long *:  "long",		 	  \
	default: "b64"				  \
)


#define PRINT_ENC(X) ({										\
	char *fmt;												\
															\
	fmt = FMT_ENC(X);										\
	if (fmt != NULL) {										\
		printf(fmt, *X);									\
	} else {												\
		printf("Need to encode %s\n", #X);					\
	}														\
})

#endif

#define	SNAPSHOT_BUF_OR_LEAVE(DATA, LEN, META, RES, LABEL)						\
do {																			\
	char *type;																	\
	type = GET_TYPE(DATA);														\
	if ((META)->version == 2) {													\
		(RES) = vm_snapshot_save_fieldname(#DATA, (DATA), type, (LEN), (META));	\
		if ((RES) != 0) {														\
			vm_snapshot_buf_err(#DATA, (META)->op);								\
			goto LABEL;															\
		}																		\
	} else {																	\
		/* TODO - Add else case */												\
		(RES) = vm_snapshot_buf((DATA), (LEN), (META));							\
		if ((RES) != 0) {														\
			vm_snapshot_buf_err(#DATA, (META)->op);								\
			goto LABEL;															\
		}																		\
	}																			\
} while (0)

#define	SNAPSHOT_VAR_OR_LEAVE(DATA, META, RES, LABEL)								\
		SNAPSHOT_BUF_OR_LEAVE(&(DATA), sizeof(DATA), (META), (RES), LABEL);			\

/*
 * Address variables are pointers to guest memory.
 *
 * When RNULL != 0, do not enforce invalid address checks; instead, make the
 * pointer NULL at restore time.
 */
#define	SNAPSHOT_GUEST2HOST_ADDR_OR_LEAVE(ADDR, LEN, RNULL, META, RES, LABEL)	\
do {										\
	(RES) = vm_snapshot_guest2host_addr((void **)&(ADDR), (LEN), (RNULL),	\
			(META));					\
	if ((RES) != 0) {							\
		if ((RES) == EFAULT)						\
			fprintf(stderr, "%s: invalid address: %s\r\n",		\
				__func__, #ADDR);				\
		goto LABEL;							\
	}									\
} while (0)

/* compare the value in the meta buffer with the data */
#define	SNAPSHOT_BUF_CMP_OR_LEAVE(DATA, LEN, META, RES, LABEL)						\
do {																				\
	char *type;																		\
	type = GET_TYPE(DATA);															\
	if ((META)->version == 2) {														\
		(RES) = vm_snapshot_save_fieldname_cmp(#DATA, (DATA), type, (LEN), (META));	\
		if ((RES) != 0) {															\
			vm_snapshot_buf_err(#DATA, (META)->op);									\
			goto LABEL;																\
		}																			\
	} else {																		\
		(RES) = vm_snapshot_buf_cmp((DATA), (LEN), (META));							\
		if ((RES) != 0) {															\
			vm_snapshot_buf_err(#DATA, (META)->op);									\
			goto LABEL;																\
		}																			\
	}																				\
} while (0)

#define	SNAPSHOT_VAR_CMP_OR_LEAVE(DATA, META, RES, LABEL)			\
	SNAPSHOT_BUF_CMP_OR_LEAVE(&(DATA), sizeof(DATA), (META), (RES), LABEL)

#endif
