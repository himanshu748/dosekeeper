/* tally: the smallest useful QNX resource manager.
 *   echo 5 > /dev/tally ; echo 7 > /dev/tally ; cat /dev/tally   ->  total 12 (2 writes) */
struct _iofunc_ocb;
#define RESMGR_OCB_T struct _iofunc_ocb
#define IOFUNC_OCB_T struct _iofunc_ocb
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/neutrino.h>

static long total, writes;

static int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb)
{
    int status = iofunc_read_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    char buf[64];
    size_t len = (size_t)snprintf(buf, sizeof buf, "total %ld (%ld writes)\n", total, writes);
    size_t off = (size_t)ocb->offset;
    size_t nbytes = off < len ? len - off : 0;          /* 0 bytes = end of file */
    if (nbytes > _IO_READ_GET_NBYTES(msg)) nbytes = _IO_READ_GET_NBYTES(msg);
    ocb->offset += (off_t)nbytes;

    MsgReply(ctp->rcvid, (long)nbytes, buf + off, nbytes);   /* the reply IS read()'s result */
    return _RESMGR_NOREPLY;
}

static int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb)
{
    int status = iofunc_write_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    size_t nbytes = _IO_WRITE_GET_NBYTES(msg);
    char buf[32];
    if (nbytes == 0 || nbytes >= sizeof buf) return EINVAL;
    if (resmgr_msgget(ctp, buf, nbytes, sizeof msg->i) != (ssize_t)nbytes) return EIO;
    buf[nbytes] = 0;

    char *end;
    long v = strtol(buf, &end, 10);
    if (end == buf || (*end && *end != '\n')) return EINVAL;   /* write() fails with EINVAL */
    total += v;
    writes++;

    _IO_SET_WRITE_NBYTES(ctp, nbytes);
    return EOK;
}

int main(void)
{
    static resmgr_connect_funcs_t connect_funcs;
    static resmgr_io_funcs_t io_funcs;
    static iofunc_attr_t attr;
    resmgr_attr_t rattr;

    dispatch_t *dpp = dispatch_create();
    memset(&rattr, 0, sizeof rattr);
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
    io_funcs.read = io_read;
    io_funcs.write = io_write;
    iofunc_attr_init(&attr, S_IFCHR | 0666, NULL, NULL);

    if (resmgr_attach(dpp, &rattr, "/dev/tally", _FTYPE_ANY, 0, &connect_funcs, &io_funcs, &attr) == -1) {
        perror("resmgr_attach");
        return EXIT_FAILURE;
    }
    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    for (;;) {
        ctp = dispatch_block(ctp);
        dispatch_handler(ctp);
    }
}
