# FreeBSD Emulation Framework API Reference

## Kernel Module API

This document describes the API for kernel modules that extend the emulation framework.

### Header Files

```c
#include <sys/emulation/emu.h>
#include <sys/emulation/emu_module.h>
```

### Module Registration

#### emu_module_register()

Register an architecture module with the core framework.

```c
int emu_module_register(const char *name, int version);
```

**Parameters:**
- `name`: Module name (e.g., "emu_amd64", "emu_arm64")
- `version`: Module version number

**Returns:**
- 0 on success
- Error code on failure

**Example:**
```c
emu_module_register("emu_amd64", 1);
```

#### emu_module_deregister()

Unregister a module from the framework.

```c
void emu_module_deregister(const char *name);
```

**Parameters:**
- `name`: Module name to unregister

#### emu_module_refcount_inc()

Increment the reference count for a module.

```c
void emu_module_refcount_inc(const char *name);
```

**Parameters:**
- `name`: Module name

#### emu_module_refcount_dec()

Decrement the reference count for a module.

```c
void emu_module_refcount_dec(const char *name);
```

**Parameters:**
- `name`: Module name

### Instance Management

#### emu_instance_create()

Create a new emulated instance.

```c
struct emu_instance *emu_instance_create(
    const char *name,
    const char *arch,
    uint64_t memory_size,
    int cpu_count
);
```

**Parameters:**
- `name`: Instance name (must be unique)
- `arch`: Target architecture
- `memory_size`: Memory size in bytes
- `cpu_count`: Number of virtual CPUs

**Returns:**
- Pointer to new instance on success
- NULL on failure

#### emu_instance_destroy()

Destroy an emulated instance.

```c
int emu_instance_destroy(struct emu_instance *inst);
```

**Parameters:**
- `inst`: Instance to destroy

**Returns:**
- 0 on success
- Error code on failure

#### emu_instance_start()

Start an instance.

```c
int emu_instance_start(struct emu_instance *inst);
```

**Parameters:**
- `inst`: Instance to start

**Returns:**
- 0 on success
- Error code on failure

#### emu_instance_stop()

Stop an instance.

```c
int emu_instance_stop(struct emu_instance *inst, int force);
```

**Parameters:**
- `inst`: Instance to stop
- `force`: If non-zero, use SIGKILL instead of SIGTERM

**Returns:**
- 0 on success
- Error code on failure

#### emu_instance_get_state()

Get current instance state.

```c
enum emu_instance_state emu_instance_get_state(struct emu_instance *inst);
```

**Returns:**
- `EMU_STATE_STOPPED`
- `EMU_STATE_RUNNING`
- `EMU_STATE_PAUSED`
- `EMU_STATE_ERROR`

### Memory Management

#### emu_memory_alloc()

Allocate guest memory.

```c
void *emu_memory_alloc(struct emu_instance *inst, uint64_t size);
```

**Parameters:**
- `inst`: Instance
- `size`: Size in bytes

**Returns:**
- Pointer to allocated memory on success
- NULL on failure

#### emu_memory_free()

Free guest memory.

```c
void emu_memory_free(struct emu_instance *inst, void *ptr, uint64_t size);
```

**Parameters:**
- `inst`: Instance
- `ptr`: Memory pointer
- `size`: Size in bytes

#### emu_memory_get_stats()

Get memory statistics for an instance.

```c
int emu_memory_get_stats(
    struct emu_instance *inst,
    struct emu_memory_stats *stats
);
```

**Parameters:**
- `inst`: Instance
- `stats`: Pointer to stats structure

**Returns:**
- 0 on success
- Error code on failure

### Module Callback Interface

Architecture modules implement callbacks that the core module calls:

```c
struct emu_arch_module {
    const char *name;
    int version;
    
    /* Initialization */
    int (*init)(void);
    void (*fini)(void);
    
    /* CPU operations */
    int (*cpu_reset)(struct emu_instance *inst);
    int (*cpu_step)(struct emu_instance *inst);
    int (*cpu_interrupt)(struct emu_instance *inst, int irq);
    
    /* Memory operations */
    int (*memory_read)(struct emu_instance *inst, uint64_t gpa,
        void *buf, size_t size);
    int (*memory_write)(struct emu_instance *inst, uint64_t gpa,
        const void *buf, size_t size);
    
    /* Register access */
    int (*get_register)(struct emu_instance *inst, int reg,
        uint64_t *value);
    int (*set_register)(struct emu_instance *inst, int reg,
        uint64_t value);
};
```

### Error Codes

```c
#define EMU_SUCCESS             0
#define EMU_ERR_NO_MEMORY       ENOMEM
#define EMU_ERR_NO_INSTANCES    EMU_ERRNO(1)
#define EMU_ERR_NOT_RUNNING     EMU_ERRNO(2)
#define EMU_ERR_ALREADY_RUNNING EMU_ERRNO(3)
#define EMU_ERR_INVALID_ARCH    EMU_ERRNO(4)
#define EMU_ERR_PERMISSION      EPERM
#define EMU_ERR_RESOURCE_LIMIT  EMU_ERRNO(5)
#define EMU_ERR_MODULE_NOT_FOUND EMU_ERRNO(6)
#define EMU_ERR_TIMEOUT         ETIMEDOUT
#define EMU_ERR_SECURELEVEL     EMU_ERRNO(7)
```

## Userland API

### Library

```c
#include <emu.h>
```

### Connection Management

#### emu_open()

Open a connection to the emulation framework.

```c
emu_handle_t emu_open(void);
```

**Returns:**
- Handle on success
- NULL on failure

#### emu_close()

Close a connection.

```c
void emu_close(emu_handle_t handle);
```

**Parameters:**
- `handle`: Handle from emu_open()

### Instance Operations

#### emu_init()

Create a new instance.

```c
int emu_init(emu_handle_t handle, struct emu_init_args *args, char **instance_id);
```

**Parameters:**
- `handle`: Connection handle
- `args`: Initialization arguments
- `instance_id`: Output: instance ID

**Returns:**
- 0 on success
- Error code on failure

#### emu_start()

Start an instance.

```c
int emu_start(emu_handle_t handle, const char *instance_id, int flags);
```

**Parameters:**
- `handle`: Connection handle
- `instance_id`: Instance to start
- `flags`: Execution flags

**Returns:**
- 0 on success
- Error code on failure

#### emu_stop()

Stop an instance.

```c
int emu_stop(emu_handle_t handle, const char *instance_id, int flags);
```

**Parameters:**
- `handle`: Connection handle
- `instance_id`: Instance to stop
- `flags`: Stop flags (e.g., EMU_STOP_FORCE)

**Returns:**
- 0 on success
- Error code on failure

#### emu_status()

Get instance status.

```c
int emu_status(emu_handle_t handle, const char *instance_id,
    struct emu_status *status);
```

**Parameters:**
- `handle`: Connection handle
- `instance_id`: Instance to query
- `status`: Output: status information

**Returns:**
- 0 on success
- Error code on failure

#### emu_destroy()

Destroy an instance.

```c
int emu_destroy(emu_handle_t handle, const char *instance_id, int flags);
```

**Parameters:**
- `handle`: Connection handle
- `instance_id`: Instance to destroy
- `flags`: Destruction flags

**Returns:**
- 0 on success
- Error code on failure

### Module Loading

#### emu_load_module()

Load a kernel module into an instance.

```c
int emu_load_module(emu_handle_t handle, const char *instance_id,
    const char *module_path);
```

**Parameters:**
- `handle`: Connection handle
- `instance_id`: Target instance
- `module_path`: Path to .ko file

**Returns:**
- 0 on success
- Error code on failure

#### emu_unload_module()

Unload a kernel module from an instance.

```c
int emu_unload_module(emu_handle_t handle, const char *instance_id,
    const char *module_name);
```

**Parameters:**
- `handle`: Connection handle
- `instance_id`: Target instance
- `module_name`: Module name to unload

**Returns:**
- 0 on success
- Error code on failure

### Console Access

#### emu_console_open()

Open console connection.

```c
emu_console_t emu_console_open(emu_handle_t handle, const char *instance_id);
```

**Parameters:**
- `handle`: Connection handle
- `instance_id`: Instance

**Returns:**
- Console handle on success
- NULL on failure

#### emu_console_read()

Read from console.

```c
ssize_t emu_console_read(emu_console_t console, char *buf, size_t len,
    int timeout_ms);
```

**Parameters:**
- `console`: Console handle
- `buf`: Read buffer
- `len`: Buffer length
- `timeout_ms`: Timeout in milliseconds

**Returns:**
- Bytes read on success
- 0 on timeout
- -1 on error

#### emu_console_write()

Write to console.

```c
ssize_t emu_console_write(emu_console_t console, const char *buf, size_t len);
```

**Parameters:**
- `console`: Console handle
- `buf`: Write buffer
- `len`: Bytes to write

**Returns:**
- Bytes written on success
- -1 on error

#### emu_console_close()

Close console connection.

```c
void emu_console_close(emu_console_t console);
```

**Parameters:**
- `console`: Console handle

### Configuration

#### emu_config_load()

Load configuration from file.

```c
int emu_config_load(const char *path, struct emu_config **config);
```

**Parameters:**
- `path`: Config file path
- `config`: Output: config structure

**Returns:**
- 0 on success
- Error code on failure

#### emu_config_free()

Free configuration structure.

```c
void emu_config_free(struct emu_config *config);
```

**Parameters:**
- `config`: Config to free

#### emu_config_get()

Get configuration value.

```c
int emu_config_get(struct emu_config *config, const char *key, char *value,
    size_t *value_size);
```

**Parameters:**
- `config`: Configuration
- `key`: Configuration key
- `value`: Output buffer
- `value_size`: Buffer size (in/out)

**Returns:**
- 0 on success
- ENOENT if key not found
- ERANGE if buffer too small

#### emu_config_set()

Set configuration value.

```c
int emu_config_set(struct emu_config *config, const char *key,
    const char *value);
```

**Parameters:**
- `config`: Configuration
- `key`: Configuration key
- `value`: New value

**Returns:**
- 0 on success
- Error code on failure

## Data Structures

### emu_init_args

```c
struct emu_init_args {
    const char *name;          // Instance name (optional)
    const char *arch;         // Target architecture
    const char *mode;         // Execution mode
    uint64_t memory;          // Memory size in bytes
    int cpus;                 // Number of virtual CPUs
    int cpu_level;            // CPU compatibility level
    const char *image;        // Boot image (optional)
    const char *blob;         // Firmware blob (optional)
    const char *workload;    // Workload specification (optional)
};
```

### emu_status

```c
struct emu_status {
    enum emu_instance_state state;  // Current state
    const char *arch;               // Target architecture
    uint64_t memory_used;           // Memory usage
    uint64_t cpu_time;              // CPU time used
    pid_t pid;                      // Emulator process PID
    time_t uptime;                  // Instance uptime
};
```

### emu_memory_stats

```c
struct emu_memory_stats {
    uint64_t total;           // Total allocated
    uint64_t used;            // Currently in use
    uint64_t available;       // Available for allocation
    uint64_t overcommit_threshold;  // Overcommit warning threshold
};
```

## Example: Using the Userland API

```c
#include <emu.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    emu_handle_t handle;
    struct emu_init_args args = {0};
    struct emu_status status;
    char *instance_id;
    int ret;

    /* Open connection */
    handle = emu_open();
    if (handle == NULL) {
        fprintf(stderr, "Failed to open emu\n");
        return 1;
    }

    /* Initialize instance */
    args.arch = "amd64";
    args.memory = 512 * 1024 * 1024;  // 512 MB
    args.cpus = 2;

    ret = emu_init(handle, &args, &instance_id);
    if (ret != 0) {
        fprintf(stderr, "Failed to init: %d\n", ret);
        emu_close(handle);
        return 1;
    }

    printf("Created instance: %s\n", instance_id);

    /* Start instance */
    ret = emu_start(handle, instance_id, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to start: %d\n", ret);
    }

    /* Get status */
    ret = emu_status(handle, instance_id, &status);
    if (ret == 0) {
        printf("State: %d, Memory: %lu\n",
            status.state, status.memory_used);
    }

    /* Stop and destroy */
    emu_stop(handle, instance_id, 0);
    emu_destroy(handle, instance_id, 0);

    emu_close(handle);
    return 0;
}
```
