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

enum vm_snapshot_op {
	VM_SNAPSHOT_SAVE,
	VM_SNAPSHOT_RESTORE,
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

	enum vm_snapshot_op op;
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

void vm_snapshot_buf_err(const char *bufname, const enum vm_snapshot_op op);
int vm_snapshot_buf(volatile void *data, size_t data_size,
		    struct vm_snapshot_meta *meta);
size_t vm_get_snapshot_size(struct vm_snapshot_meta *meta);
int vm_snapshot_gaddr(void **addr_var, size_t gaddr_len, bool restore_null,
		      struct vm_snapshot_meta *meta);

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

#define	SNAPSHOT_BUF_OR_LEAVE(DATA, LEN, META, RES, LABEL)			\
do {										\
	(RES) = vm_snapshot_buf((DATA), (LEN), (META));				\
	if ((RES) != 0) {							\
		vm_snapshot_buf_err(#DATA, (META)->op);				\
		goto LABEL;							\
	}									\
} while (0)

#define	SNAPSHOT_VAR_OR_LEAVE(DATA, META, RES, LABEL)				\
	SNAPSHOT_BUF_OR_LEAVE(&(DATA), sizeof(DATA), (META), (RES), LABEL)

/* address variables are pointers to guest memory
 *
 * when RNULL != 0, do not enforce invalid address checks; in stead, make the
 * pointer NULL at restore time
 */
#define	SNAPSHOT_GADDR_OR_LEAVE(ADDR_VAR, GADDR_SIZE, RNULL, META, RES, LABEL)	\
do {										\
	(RES) = vm_snapshot_gaddr((void **)&(ADDR_VAR), RNULL, (GADDR_SIZE),	\
				  (META));					\
	if ((RES) != 0) {							\
		if ((RES) == EFAULT)						\
			fprintf(stderr, "%s: invalid address: %s\r\n",		\
				__func__, #ADDR_VAR);				\
		goto LABEL;							\
	}									\
} while (0)

#endif
