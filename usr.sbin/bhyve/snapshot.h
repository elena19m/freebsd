#ifndef _BHYVE_SNAPSHOT_
#define _BHYVE_SNAPSHOT_

struct vmctx;

struct __attribute__((packed)) restore_state {
	int kdata_fd;
	int vmmem_fd;

	void *kdata_map;
	size_t kdata_len;

	size_t vmmem_len;

	struct ucl_parser *meta_parser;
	ucl_object_t *meta_root_obj;
};

struct checkpoint_thread_info {
	struct vmctx *ctx;
	int socket_fd;
	struct sockaddr_un *addr;
} checkpoint_info;

typedef int (*vm_snapshot_dev_cb)(struct vmctx *, const char *, void *, size_t,
				  size_t *);
typedef int (*vm_restore_dev_cb) (struct vmctx *, const char *, void *, size_t);

struct vm_snapshot_dev_info {
	const char *dev_name;            /* device name */
	vm_snapshot_dev_cb snapshot_cb;  /* callback for device snapshot */
	vm_restore_dev_cb restore_cb;    /* callback for device restore */
};

void destroy_restore_state(struct restore_state *rstate);

const char * lookup_vmname(struct restore_state *rstate);
int lookup_memflags(struct restore_state *rstate);
size_t lookup_memsize(struct restore_state *rstate);
int lookup_guest_ncpus(struct restore_state *rstate);


int restore_vm_mem(struct vmctx *ctx, struct restore_state *rstate);
int restore_kernel_structs(struct vmctx *ctx, struct restore_state *rstate);

int restore_devs(struct vmctx *ctx, struct restore_state *rstate);

int get_checkpoint_msg(int conn_fd, struct vmctx *ctx);
void *checkpoint_thread(void *param);
int init_checkpoint_thread(struct vmctx *ctx);


int load_restore_file(const char *filename, struct restore_state *rstate);

#endif
