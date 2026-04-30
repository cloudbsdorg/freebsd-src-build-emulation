# GDB Stub Protocol Documentation

This document describes the GDB remote debugging interface implemented by the FreeBSD emulation framework.

## Overview

The emulation framework includes a GDB stub that allows debugging guest code using GDB. The stub implements a subset of the GDB Remote Serial Protocol (RSP).

## Protocol Basics

The GDB stub communicates over a TCP socket or Unix domain socket. The default port is 2159 (GDB debug port for emulators).

### Connection

```bash
# Connect to emulation instance
(gdb) target remote localhost:2159
```

### Packet Format

GDB uses ASCII packets with the following format:

```
$packet-data#checksum
```

- `$` - Packet start
- `packet-data` - Packet contents
- `#` - Checksum delimiter
- `checksum` - Two-digit hex checksum

### Acknowledgment

- `+` - Packet acknowledged
- `-` - Packet not acknowledged, resend
- No response within timeout - resend packet

## Supported Packets

### Stop Reply Packets

When the target stops (breakpoint, exception, etc.), it sends:

```
T02signal:05;thread:1;thread:p1.1;core:0;
```

Format: `T sigcode ; key:value ; ...`

- `sigcode` - Signal number (see Signal Numbers)
- `thread` - Thread ID
- `core` - CPU core number

### Continue (c)

Continue execution:

```
c
```

Optional: `c address` - Continue from specific address

### Step (s)

Step one instruction:

```
s
```

Optional: `s address` - Step from specific address

### Extended Mode (/)

Enable extended mode:

```
!
```

Response: `OK` or `ENN`

### Last Signal (?")

Query last signal:

```
?
```

Response: Stop reply packet

### Registers (g and G)

### Read Registers (g)

Read all general-purpose registers:

```
g
```

Response: `XX...` - Register values in hexadecimal

### Write Registers (G)

Write all general-purpose registers:

```
GXX...
```

Response: `OK` or `ENN`

### Read Single Register (p)

Read a specific register:

```
p n
```

Response: `val` - Register value

### Write Single Register (P)

Write a specific register:

```
P n=val
```

Response: `OK` or `ENN`

### Memory (m and M)

### Read Memory (m)

```
m addr,length
```

Response: `XX...` - Memory contents in hex

Error: `ENN`

### Write Memory (M)

```
M addr,length:XX...
```

Response: `OK` or `ENN`

### Query Packets

### Thread List (qL, qfThreadInfo, qsThreadInfo)

Query available threads:

```
qfThreadInfo
qsThreadInfo
```

Response: `l` (end of list) or `m...` (thread IDs)

### Thread Info (qThreadExtraInfo)

Query thread info string:

```
qThreadExtraInfo,id
```

Response: Hex-encoded string

### Current Thread (qC)

Query current thread:

```
qC
```

Response: `QC thread-id`

### Search (qSearch)

Search memory for byte pattern:

```
qSearch:mem:addr,len:pattern
```

Response: Search result

### Thread Alive (T)

Check if thread is alive:

```
T id
```

Response: `OK` or `ENN`

### Xfer (qXfer)

Read auxiliary data:

```
qXfer:object:annex:offset,length
```

Supported objects:

- `features` - Read feature files
- `threads` - Thread list
- `libraries` - Shared library list

Response: `l` (last) or `m...` (data) or `` (empty)

### Breakpoints

### Insert Breakpoint (Z0)

Insert software breakpoint:

```
Z0,addr,kind
```

Response: `OK` or `ENN`

### Remove Breakpoint (z0)

Remove software breakpoint:

```
z0,addr,kind
```

Response: `OK` or `ENN`

### Hardware Breakpoint (Z1/z1)

Insert/remove hardware breakpoint

### Write Watchpoint (Z2/z2)

Insert/remove write watchpoint

### Read Watchpoint (Z3/z3)

Insert/remove read watchpoint

### Access Watchpoint (Z4/z4)

Insert/remove access watchpoint

### File Operations

### Attach (vAttach)

Attach to process:

```
vAttach;pid
```

### Detach (D)

Detach from target:

```
D
```

Response: `OK`

### Kill (k)

Kill target:

```
k
```

### Run (R)

Restart program (extended mode):

```
R XX
```

### Signal Handling

### Pass Signal (Q)

Configure signal handling:

```
QPassSignals:signals
QPassSignal:signal
```

### Toggle Debug (Q)

Toggle debug flag:

```
QDebugChar:char
```

## Signal Numbers

The emulation framework uses these signal numbers:

| Number | Name | Description |
|--------|------|-------------|
| 0 |  | Reserved |
| 1 | SIGHUP | Hangup |
| 2 | SIGINT | Interrupt (Ctrl+C) |
| 3 | SIGQUIT | Quit |
| 4 | SIGILL | Illegal instruction |
| 5 | SIGTRAP | Trace/breakpoint trap |
| 6 | SIGABRT | Abort |
| 7 | SIGBUS | Bus error |
| 8 | SIGFPE | Floating point exception |
| 9 | SIGKILL | Kill |
| 10 | SIGUSR1 | User-defined signal 1 |
| 11 | SIGSEGV | Segmentation fault |
| 12 | SIGUSR2 | User-defined signal 2 |
| 13 | SIGPIPE | Broken pipe |
| 14 | SIGALRM | Alarm clock |
| 15 | SIGTERM | Termination |
| 17 | SIGSTOP | Stop (process) |
| 18 | SIGTSTP | Stop (user) |
| 19 | SIGCONT | Continue |
| 20 | SIGCHLD | Child status change |
| 30 | SIGURG | Urgent data |
| 31 | SIGPROF | Profiling timer |
| 32 | SIGVTALRM | Virtual timer |
| 33 | SIGPROF | Profiling timer |
| 34 | SIGWINCH | Window size change |
| 35 | SIGIO | I/O possible |
| 36 | SIGPWR | Power failure |

## Register Identification

Registers are identified by GDB's canonical numbers. For each architecture:

### AMD64/x86-64

| Number | Name | Description |
|--------|------|-------------|
| 0 | rax | Accumulator |
| 1 | rbx | Base |
| 2 | rcx | Counter |
| 3 | rdx | Data |
| 4 | rsi | Source index |
| 5 | rdi | Destination index |
| 6 | rbp | Frame pointer |
| 7 | rsp | Stack pointer |
| 8 | r8 | General purpose 8 |
| 9 | r9 | General purpose 9 |
| 10 | r10 | General purpose 10 |
| 11 | r11 | General purpose 11 |
| 12 | r12 | General purpose 12 |
| 13 | r13 | General purpose 13 |
| 14 | r14 | General purpose 14 |
| 15 | r15 | General purpose 15 |
| 16 | rip | Instruction pointer |
| 17 | eflags | Flags register |
| 18 | cs | Code segment |
| 19 | ss | Stack segment |
| 20 | ds | Data segment |
| 21 | es | Extra segment |
| 22 | fs | F segment |
| 23 | gs | G segment |
| 24-25 | - | Reserved |
| 26 | st0 | FPU stack 0 |
| 27 | st1 | FPU stack 1 |
| ... | ... | ... |
| 33 | st7 | FPU stack 7 |
| 34-39 | - | Reserved |
| 40 | xmm0 | SSE register 0 |
| 41 | xmm1 | SSE register 1 |
| ... | ... | ... |
| 55 | xmm15 | SSE register 15 |

### ARM64/AArch64

| Number | Name | Description |
|--------|------|-------------|
| 0-30 | x0-x30 | General purpose |
| 31 | sp | Stack pointer |
| 32 | pc | Program counter |
| 33 | pstate | Process state |

## Error Codes

| Code | Description |
|------|-------------|
| E00 | Undefined |
| E01 | Bad format |
| E02 | Unknown command |
| E03 | Bad address |
| E04 | Unknown register |

## Implementation Notes

### Checksum Calculation

```c
uint8_t
calc_checksum(const char *pkt, size_t len)
{
    uint8_t sum = 0;
    while (len-- > 0)
        sum += *pkt++;
    return (sum);
}
```

### Command Dispatch

```c
static void
handle_packet(struct emu_gdb *gdb, const char *pkt, size_t len)
{
    char cmd = pkt[0];
    char reply[256];
    
    switch (cmd) {
    case 'c':
        /* Continue */
        break;
    case 's':
        /* Step */
        break;
    case 'g':
        /* Read registers */
        break;
    case 'G':
        /* Write registers */
        break;
    case 'm':
        /* Read memory */
        break;
    case 'M':
        /* Write memory */
        break;
    /* ... */
    default:
        snprintf(reply, sizeof(reply), "");
        break;
    }
    
    send_packet(gdb, reply);
}
```

### Breakpoint Implementation

Software breakpoints are implemented by replacing the instruction at the target address with a breakpoint instruction:

- x86: `0xCC` (INT3)
- ARM: `0xE0000000` (BKPT)
- AArch64: `0xD4200000` (BRK #0)

### Single-Step Implementation

Single-stepping is implemented by setting the trap flag or using hardware single-step.

## Example GDB Session

```
$ gdb kernel.ko
(gdb) set architecture amd64
(gdb) target remote localhost:2159
Remote debugging using localhost:2159
0x0000000000000000 in ?? ()
(gdb) break main
Breakpoint 1 at 0x1234
(gdb) continue
Continuing.

Breakpoint 1, 0x0000000000001234 in main ()
(gdb) info registers
rax            0x0    0
rbx            0x0    0
...
(gdb) step
...
(gdb) print var
$1 = 42
(gdb) quit
```

## Limitations

1. **No shared library support** - Static binaries only
2. **No fork/vfork** - Single-threaded debugging
3. **Limited signal support** - Only common signals
4. **No reverse debugging** - Forward execution only

## Security Considerations

1. **Local connections only** - GDB port should not be exposed
2. **No authentication** - Use firewall rules instead
3. **Full memory access** - GDB can read/write all memory
