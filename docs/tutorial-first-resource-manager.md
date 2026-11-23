# Your first QNX resource manager in 30 minutes

On QNX, almost everything you `open()` — serial ports, disks, the network stack, `/dev/null` — is served by an ordinary user-space process called a **resource manager**. In this codelab you write one: `/dev/tally`, a device you can `echo` numbers into and `cat` the running total out of. It is about 90 lines of C and uses the same pattern DoseKeeper uses for `/dev/pump`.

| Step | What you do | Time |
|---|---|---|
| 1 | Get a QNX 8.0 target running in QEMU | 8 min |
| 2 | Understand what a resource manager is | 3 min |
| 3 | Register a path with `resmgr_attach()` | 5 min |
| 4 | Answer `read()` | 5 min |
| 5 | Accept `write()` and validate it | 5 min |
| 6 | Build, run and poke it | 4 min |

You need a Linux x86_64 host with KVM, and a free [QNX Everywhere](https://www.qnx.com/products/everywhere) licence for QNX SDP 8.0.

## 1. A QNX 8.0 target in QEMU (8 min)

Install QNX SDP 8.0 with QNX Software Center, then in a terminal:

```sh
source ~/qnx800/qnxsdp-env.sh          # puts qcc, mkqnximage and the target sysroot on PATH
mkdir -p ~/qnxvm && cd ~/qnxvm
mkqnximage --type=qemu --arch=x86_64 --ssh-ident=none --build
mkqnximage --run                       # boots QNX; log in as root
```

`mkqnximage` builds a bootable image (an IFS plus a filesystem) and runs it under QEMU. Leave it running in its own terminal. Find its IP with `ifconfig` on the target if you want to `scp` files over; DoseKeeper's `scripts/` do this for you.

## 2. What a resource manager is (3 min)

When a program calls `open("/dev/tally")`, the C library asks the **process manager** who owns that path. The answer is a channel in some process. From then on, `read()` and `write()` are just **messages** sent over QNX's native message passing (`MsgSend()`), and the reply is what the call returns.

So a resource manager is a loop: receive a message, act on it, reply. The `resmgr`/`iofunc` libraries do the parsing, permissions and file-offset bookkeeping; you fill in only the handlers you care about. Because the driver is a normal process, a crash in it cannot take down the kernel — and it can be restarted, which is the idea DoseKeeper builds on.

## 3. Register a path (5 min)

Create `tally.c` with the full source from [`qnx/tutorial/tally.c`](../qnx/tutorial/tally.c). The `main()` is the boilerplate every resource manager shares:

```c
dispatch_t *dpp = dispatch_create();                  /* a channel + dispatch loop */
iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
                 _RESMGR_IO_NFUNCS, &io_funcs);       /* default POSIX behaviour  */
io_funcs.read  = io_read;                             /* ...except these two      */
io_funcs.write = io_write;
iofunc_attr_init(&attr, S_IFCHR | 0666, NULL, NULL);  /* a character device, rw   */
resmgr_attach(dpp, &rattr, "/dev/tally", _FTYPE_ANY, 0,
              &connect_funcs, &io_funcs, &attr);      /* claim the path           */
for (;;) { ctp = dispatch_block(ctp); dispatch_handler(ctp); }
```

`iofunc_func_init()` fills both tables with defaults, so `open`, `close`, `stat` and `ls -l /dev/tally` already work before you write a single handler.

## 4. Answer `read()` (5 min)

```c
static int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb)
{
    int status = iofunc_read_verify(ctp, msg, ocb, NULL);   /* opened for reading? */
    if (status != EOK) return status;
    ...
    size_t off = (size_t)ocb->offset;
    size_t nbytes = off < len ? len - off : 0;               /* 0 bytes means EOF */
    ocb->offset += nbytes;
    MsgReply(ctp->rcvid, (long)nbytes, buf + off, nbytes);   /* this IS read()'s result */
    return _RESMGR_NOREPLY;
}
```

Three things to notice:

- The **OCB** (open control block) is per `open()`; its `offset` is how `cat` gets the text once and then end-of-file.
- `MsgReply()` sends the bytes straight into the client's buffer and unblocks it. The status argument becomes `read()`'s return value.
- Returning an errno such as `EINVAL` instead makes the client's call fail with that errno — no special error protocol needed.

## 5. Accept `write()` and validate it (5 min)

```c
size_t nbytes = _IO_WRITE_GET_NBYTES(msg);
if (nbytes == 0 || nbytes >= sizeof buf) return EINVAL;
if (resmgr_msgget(ctp, buf, nbytes, sizeof msg->i) != (ssize_t)nbytes) return EIO;
...
if (end == buf || (*end && *end != '\n')) return EINVAL;
total += v;
_IO_SET_WRITE_NBYTES(ctp, nbytes);   /* tell write() how much we consumed */
return EOK;
```

The data follows the message header, so you copy it out with `resmgr_msgget()` at offset `sizeof msg->i`. This handler is a **trust boundary**: anything that can open the path can send you bytes, so check the length and the content before acting. DoseKeeper's `/dev/pump` does exactly this for prescriptions — a rate above 999.9 mL/h is refused here, not in the UI.

## 6. Build, run and poke it (4 min)

```sh
qcc -Vgcc_ntox86_64 -Wall -o tally tally.c
scp tally root@<target-ip>:/tmp/        # or copy it into the image with mkqnximage
```

On the target:

```sh
/tmp/tally &
echo 5 > /dev/tally
echo 7 > /dev/tally
cat /dev/tally          # total 12 (2 writes)
echo banana > /dev/tally  # write error: Invalid argument
ls -l /dev/tally        # crw-rw-rw-  ... /dev/tally
pidin -p tally          # it is just a process
slay tally; cat /dev/tally   # No such file or directory: the path went with it
```

That last line is the point: the device is a process. Kill it and the path disappears; start it again and it comes back.

## Where to go next

- Keep state that must survive a restart outside the process. DoseKeeper reads the delivered volume back from the (simulated) motor encoder and keeps the prescription in CRC-protected shared memory, so a restarted controller resumes the same dose.
- Add a watchdog. DoseKeeper's `guardian` uses `posix_spawn()` + `waitpid()` to respawn the controller and QNX pulses as a heartbeat to catch hangs.
- Give the handler a deadline. Run the work in a `SCHED_FIFO` thread and put background load under `SCHED_SPORADIC` so it cannot steal more than its budget.

Full source: [`qnx/pumpd.c`](../qnx/pumpd.c). QNX references: [Writing a Resource Manager](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.resmgr/topic/about.html), [`resmgr_attach()`](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.lib_ref/topic/r/resmgr_attach.html), [`mkqnximage`](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.utilities/topic/m/mkqnximage.html).
