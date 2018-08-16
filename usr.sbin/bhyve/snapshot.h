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

int snapshot_part(volatile void *data, size_t data_size, uint8_t **buffer,
		  size_t *buf_size, size_t *snapshot_len);
int restore_part(volatile void *data, size_t data_size, uint8_t **buffer,
		  size_t *buf_size);

#define	SNAPSHOT_PART(DATA, BUFFER, BUF_SIZE, SNAP_LEN) _Generic((BUFFER),     \
	uint8_t *: snapshot_part(&(DATA), sizeof(DATA), (uint8_t **) &(BUFFER),\
				(size_t *) &(BUF_SIZE), SNAP_LEN),             \
	uint8_t**: snapshot_part(&(DATA), sizeof(DATA), (uint8_t **) (BUFFER), \
				(size_t *) (BUF_SIZE), SNAP_LEN),              \
	default: fprintf(stderr, "Incompatible pointer. Must be uint8_t * or " \
			 "uint8_t **\r\n")                                     \
)

#define	RESTORE_PART(DATA, BUFFER, BUF_SIZE) _Generic((BUFFER),                \
	uint8_t* : restore_part(&(DATA), sizeof(DATA), (uint8_t **) &(BUFFER), \
				(size_t *) &(BUF_SIZE)),                       \
	uint8_t**: restore_part(&(DATA), sizeof(DATA), (uint8_t **) (BUFFER),  \
				(size_t *) (BUF_SIZE)),                        \
	default: fprintf(stderr, "Incompatible pointer. Must be uint8_t * or " \
			 "uint8_t **\r\n")                                     \
)

#define	SNAPSHOT_PART_OR_RET(DATA, BUFFER, BUF_SIZE, SNAP_LEN)                 \
do {                                                                           \
	int ret;                                                               \
	ret = SNAPSHOT_PART(DATA, BUFFER, BUF_SIZE, SNAP_LEN);                 \
	if (ret != 0)                                                          \
		return (ret);                                                  \
} while (0)

#define	RESTORE_PART_OR_RET(DATA, BUFFER, BUF_SIZE)                            \
do {                                                                           \
	int ret;                                                               \
	ret = RESTORE_PART(DATA, BUFFER, BUF_SIZE);                            \
	if (ret != 0)                                                          \
		return (ret);                                                  \
} while (0)

#endif
