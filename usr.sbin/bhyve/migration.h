#ifndef _BHYVE_MIGRATION_
#define _BHYVE_MIGRATION_

#include <ucl.h>
#include <machine/vmm_dev.h>
#include <vmmapi.h>

struct vmctx;

int receive_vm_migration(struct vmctx *ctx, char *migration_data);

/* Warm Migration */
#define MAX_DEV_NAME_LEN    64

enum migration_transfer_req {
	MIGRATION_SEND_REQ	= 0,
	MIGRATION_RECV_REQ	= 1
};

enum message_types {
    MESSAGE_TYPE_SPECS		= 1,
    MESSAGE_TYPE_METADATA	= 2,
    MESSAGE_TYPE_RAM		= 3,
    MESSAGE_TYPE_KERN		= 4,
    MESSAGE_TYPE_DEV		= 5,
    MESSAGE_TYPE_UNKNOWN	= 8,
};

struct __attribute__((packed)) migration_message_type {
    size_t len;
    unsigned int type;		// enum message_type
    unsigned int req_type;	// enum snapshot_req
    char name[MAX_DEV_NAME_LEN];
};

struct __attribute__((packed)) migration_system_specs {
	char hw_machine[MAX_SPEC_LEN];
	char hw_model[MAX_SPEC_LEN];
	size_t hw_pagesize;
};

int vm_send_migrate_req(struct vmctx *ctx, struct migrate_req req);
int vm_recv_migrate_req(struct vmctx *ctx, struct migrate_req req);

#endif
