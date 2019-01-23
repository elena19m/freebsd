#ifndef _BHYVE_SNAPSHOT_
#define _BHYVE_SNAPSHOT_

#include <libxo/xo.h>
#include <machine/vmm_dev.h>
#include <sys/types.h>
#include <ucl.h>

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
typedef int (*vm_pause_dev_cb) (struct vmctx *, const char *);
typedef int (*vm_resume_dev_cb) (struct vmctx *, const char *);

struct vm_snapshot_dev_info {
	const char *dev_name;		/* device name */
	vm_snapshot_dev_cb snapshot_cb;	/* callback for device snapshot */
	vm_restore_dev_cb restore_cb;	/* callback for device restore */
	vm_pause_dev_cb pause_cb;	/* callback for device pause */
	vm_resume_dev_cb resume_cb;	/* callback for device resume */
};

struct vm_snapshot_kern_info {
	const char *struct_name;	/* kernel structure name*/
	enum snapshot_req req;		/* request type */
};

struct vm_snapshot_buffer {
	/* R/O for device-specific functions;
	 * written by generic snapshot functions
	 */
	uint8_t *const buf_start;
	const size_t buf_size;

	/* R/W for device-specific functions
	 * used to keep track of buffer current position and remaining size
	 */
	uint8_t *buf;
	size_t buf_rem;

	/* length of the snapshot is either determined as (buf_size - buf_rem)
	 * or (buf - buf_start) -- the second variation returns a signed value
	 * so it may not be appropriate
	 */
	/* size_t snapshot_len; */
};

struct vm_snapshot_meta {
	struct vmctx *ctx;
	void *dev_data;

	struct vm_snapshot_buffer buffer;
};

struct vm_snapshot_file_meta {
	int data_fd;
	xo_handle_t *xop;
};

void destroy_restore_state(struct restore_state *rstate);

const char * lookup_vmname(struct restore_state *rstate);
int lookup_memflags(struct restore_state *rstate);
size_t lookup_memsize(struct restore_state *rstate);
int lookup_guest_ncpus(struct restore_state *rstate);


int restore_vm_mem(struct vmctx *ctx, struct restore_state *rstate);
int restore_kernel_structs(struct vmctx *ctx, struct restore_state *rstate);

int restore_devs(struct vmctx *ctx, struct restore_state *rstate);
int pause_devs(struct vmctx *ctx);
int resume_devs(struct vmctx *ctx);

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
	default: ({ fprintf(stderr, "Incompatible pointer. Must be uint8_t * " \
			    "or uint8_t **\r\n"); -2; })                       \
)

#define	RESTORE_PART(DATA, BUFFER, BUF_SIZE) _Generic((BUFFER),                \
	uint8_t* : restore_part(&(DATA), sizeof(DATA), (uint8_t **) &(BUFFER), \
				(size_t *) &(BUF_SIZE)),                       \
	uint8_t**: restore_part(&(DATA), sizeof(DATA), (uint8_t **) (BUFFER),  \
				(size_t *) (BUF_SIZE)),                        \
	default: ({ fprintf(stderr, "Incompatible pointer. Must be uint8_t * " \
			    "or uint8_t **\r\n"); -2; })                       \
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
