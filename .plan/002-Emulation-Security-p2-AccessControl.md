# Emulation Framework Security — Chapter 2: Access Control & Authorization

> **Part of:** Security chapter series (002a through 002f)
> **See also:** `002a` for threat model, `002f` for implementation phases (S2)

---

## 5. Access Control & Authorization

### 5.1 Design Principles

The emulation framework's access control model follows these principles:

1. **Root-only by default**: Only root can create or manage emulation instances unless explicitly enabled
2. **Group-based delegation**: The `emu` group (GID_EMU) provides controlled delegation to non-root users
3. **Sysctl-gated enablement**: A master sysctl (`kern.emulation.allow_nonroot`) controls whether non-root access is permitted
4. **Ownership model**: Every instance has an owning user (ucred), tracked at creation time
5. **Granular permissions**: Different operations require different permission levels — a user may start/stop an instance they don't own but cannot destroy it
6. **Resource quotas**: Per-user and per-group limits prevent resource exhaustion
7. **Jail-aware**: The framework integrates with FreeBSD's jail system, requiring explicit jail permission (`pr_allow_emu_flag`)
8. **Defense in depth**: Multiple layers of access control (sysctl → group → ownership → permissions → limits)

### 5.2 Reference: bhyve/VMM Permission Model

The bhyve/VMM codebase provides the reference pattern for our access control:

| Mechanism | bhyve/VMM | Emulation Framework |
|-----------|-----------|---------------------|
| Group | `GID_VMM` (978) | `GID_EMU` (979) |
| Control device | `/dev/vmmctl` (0660, root:vm) | `/dev/emuctl` (0660, root:emu) |
| Per-instance device | `/dev/vmm/<name>` (0600, owner:vm) | `/dev/emu/<name>` (0600, owner:emu) |
| Create privilege | `PRIV_VMM_CREATE` (711) | `PRIV_EMU_CREATE` (720) |
| Destroy privilege | `PRIV_VMM_DESTROY` (712) | `PRIV_EMU_DESTROY` (721) |
| Passthrough privilege | `PRIV_VMM_PPTDEV` (710) | N/A (no passthrough) |
| Per-user limit | `hw.vmm.maxvmms` sysctl | `kern.emulation.max_instances_per_user` sysctl |
| Jail integration | `pr_allow_vmm_flag` | `pr_allow_emu_flag` |
| Cross-user visibility | `cr_cansee()` check in `vmmdev_lookup()` | `cr_cansee()` check in instance lookup |
| Credential tracking | `struct ucred *ucred` in `vmmdev_softc` | `struct ucred *owner` in `emu_instance` |

### 5.3 Group Configuration

A new system group `emu` is required:

```c
/* In sys/sys/conf.h */
#define	GID_EMU		979	/* Emulation framework group */
```

**Group membership effects:**
- Members of the `emu` group can create and manage emulation instances (when `kern.emulation.allow_nonroot=1`)
- Non-members cannot access the emulation framework at all (unless root)
- The group provides a clean delegation boundary: add users to `emu` to grant emulation access

**Device node permissions:**
```
crw-rw----  root  emu  /dev/emuctl     (control device, 0660)
crw-------  owner emu  /dev/emu/<name>  (per-instance, 0600)
```

### 5.4 Ownership Model

Every emulation instance has an owning user, tracked at creation time:

```c
struct emu_instance {
    /* ... existing fields ... */
    struct ucred *owner;        /* Credentials of creating user */
    uid_t owner_uid;            /* Owner UID (cached for fast checks) */
    gid_t owner_gid;            /* Owner GID (cached) */
    int permissions;            /* Custom permission mask (optional) */
};
```

**Ownership rules:**
- The creating user becomes the instance owner
- Ownership is recorded in the instance's ucred at creation time
- Ownership cannot be transferred (initially — future enhancement)
- Root can operate on any instance regardless of ownership
- The `cr_cansee()` function controls cross-user instance visibility

### 5.5 Granular Permission Levels

The framework supports two tiers of permissions:

#### Macro Levels

| Level | Description | Who |
|-------|-------------|-----|
| **admin** | Full control: create, destroy, modify any instance | root only |
| **operator** | Manage all instances: start, stop, console, stack, snapshot | root, emu group members |
| **user** | Manage own instances only | instance owner |

#### Micro Permissions (Per-Operation)

Each operation on an instance requires a specific permission:

| Permission | Description | Default Grant |
|------------|-------------|---------------|
| `EMU_PERM_CREATE` | Create new instances | root, emu group (if allowed) |
| `EMU_PERM_DESTROY` | Destroy instances | root, instance owner |
| `EMU_PERM_START` | Start an instance | root, emu group, instance owner |
| `EMU_PERM_STOP` | Stop an instance | root, emu group, instance owner |
| `EMU_PERM_MODIFY` | Modify instance configuration | root, instance owner |
| `EMU_PERM_CONSOLE` | Read console output | root, emu group, instance owner |
| `EMU_PERM_STACK` | Capture/read stack traces | root, emu group, instance owner |
| `EMU_PERM_SNAPSHOT` | Create/restore snapshots | root, instance owner |
| `EMU_PERM_SHARE` | Configure filesystem shares | root, instance owner |
| `EMU_PERM_NETWORK` | Configure network access | root, instance owner |
| `EMU_PERM_LIST` | List all instances | root, emu group |
| `EMU_PERM_AUDIT` | View audit logs | root only |
| `EMU_PERM_BLOB` | Manage firmware blobs | root, emu group |

### 5.6 Permission Matrix

| Operation | Root | emu Group (owner) | emu Group (non-owner) | Regular User (owner) | Regular User (non-owner) |
|-----------|------|-------------------|----------------------|---------------------|-------------------------|
| Create instance | ✅ | ✅ (if allowed) | ✅ (if allowed) | ❌ | ❌ |
| Destroy own instance | ✅ | ✅ | ❌ | ✅ | ❌ |
| Destroy any instance | ✅ | ❌ | ❌ | ❌ | ❌ |
| Start own instance | ✅ | ✅ | ✅ | ✅ | ❌ |
| Start any instance | ✅ | ✅ | ✅ | ❌ | ❌ |
| Stop own instance | ✅ | ✅ | ✅ | ✅ | ❌ |
| Stop any instance | ✅ | ✅ | ✅ | ❌ | ❌ |
| Console own instance | ✅ | ✅ | ✅ | ✅ | ❌ |
| Console any instance | ✅ | ✅ | ✅ | ❌ | ❌ |
| Stack own instance | ✅ | ✅ | ✅ | ✅ | ❌ |
| Stack any instance | ✅ | ✅ | ✅ | ❌ | ❌ |
| Modify own config | ✅ | ✅ | ❌ | ✅ | ❌ |
| Snapshot own instance | ✅ | ✅ | ❌ | ✅ | ❌ |
| List all instances | ✅ | ✅ | ✅ | ❌ | ❌ |
| List own instances | ✅ | ✅ | ✅ | ✅ | ✅ |
| View audit logs | ✅ | ❌ | ❌ | ❌ | ❌ |
| Manage blobs | ✅ | ✅ | ✅ | ❌ | ❌ |

### 5.7 Privilege Definitions

New kernel privileges for the emulation framework:

```c
/* In sys/sys/priv.h */

/*
 * Emulation framework privileges.
 */
#define	PRIV_EMU_CREATE		720	/* Can create emulation instances. */
#define	PRIV_EMU_DESTROY	721	/* Can destroy other users' instances. */
#define	PRIV_EMU_MODIFY		722	/* Can modify other users' instance config. */
#define	PRIV_EMU_ADMIN		723	/* Full emulation administrative access. */
#define	PRIV_EMU_AUDIT		724	/* Can view audit logs. */
#define	PRIV_EMU_BLOB		725	/* Can manage firmware blobs. */
```

**Privilege check flow:**
```
Operation requested
    │
    ├── Is the caller root? → GRANT
    ├── Is the caller the instance owner?
    │       ├── Does the operation require owner? → GRANT
    │       └── Does the operation allow emu group? → check group membership
    ├── Is the caller in the emu group?
    │       ├── Is kern.emulation.allow_nonroot enabled? → check per-op permission
    │       └── DENY
    └── DENY
```

### 5.8 Sysctl Interface for Access Control

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `kern.emulation.allow_nonroot` | CTLTYPE_INT | 0 | Allow non-root users to create/manage instances |
| `kern.emulation.required_group` | CTLTYPE_INT | 979 (GID_EMU) | GID required for non-root access (0 = any group) |
| `kern.emulation.max_instances_per_user` | CTLTYPE_INT | 4 | Maximum instances per non-root user |
| `kern.emulation.max_instances_per_group` | CTLTYPE_INT | 16 | Maximum instances per group |
| `kern.emulation.max_memory_per_user` | CTLTYPE_INT | 4096 | Max total MB per non-root user (0 = unlimited) |
| `kern.emulation.destroy_others` | CTLTYPE_INT | 0 | Allow emu group members to destroy any instance |
| `kern.emulation.jail_allow_vmm` | CTLTYPE_INT | 0 | Allow emulation in jails (requires jail permission) |

### 5.9 Jail Integration

Following the bhyve pattern, the emulation framework integrates with FreeBSD's jail system:

```c
/* Jail allow flag for emulation */
pr_allow_emu_flag = prison_add_allow(NULL, "emu", NULL, 0);
```

**Jail permission check:**
```c
static int
emu_jail_priv_check(struct ucred *ucred)
{
    if (jailed(ucred) &&
        (ucred->cr_prison->pr_allow & pr_allow_emu_flag) == 0)
        return (EPERM);

    return (0);
}
```

**Jail configuration:**
```
# In jail.conf:
allow.emu;
```

### 5.10 Resource Limits

Per-user and per-group resource limits prevent abuse:

```c
/* Per-user resource tracking */
struct emu_user_limits {
    uid_t uid;                          /* User ID */
    int instance_count;                 /* Current instance count */
    uint64_t total_memory_mb;           /* Current total memory usage */
    uint64_t total_cpu_seconds;         /* Accumulated CPU time */
    LIST_ENTRY(emu_user_limits) entries; /* Hash table linkage */
};

/* Per-group resource tracking */
struct emu_group_limits {
    gid_t gid;                          /* Group ID */
    int instance_count;                 /* Current instance count */
    LIST_ENTRY(emu_group_limits) entries;
};
```

**Enforcement points:**
- On `create`: check `instance_count < max_instances_per_user` and `total_memory_mb + new_memory < max_memory_per_user`
- On `start`: check group instance count
- On `modify` (increase memory): check new total against limit
- On `destroy`/`stop`: decrement counters

### 5.11 Permission Check Implementation

```c
/* Unified permission check for all operations */
static int
emu_check_perm(struct emu_instance *inst, struct ucred *cred, int operation)
{
    int error;

    /* Root can do anything */
    if (priv_check_cred(cred, PRIV_EMU_ADMIN) == 0)
        return (0);

    /* Check jail permissions */
    error = emu_jail_priv_check(cred);
    if (error != 0)
        return (error);

    /* Check if non-root access is allowed at all */
    if (allow_nonroot == 0)
        return (EPERM);

    /* Check group membership */
    if (required_group > 0 && !group_member(cred, required_group))
        return (EPERM);

    /* Check per-user instance limit (for create) */
    if (operation == EMU_PERM_CREATE) {
        if (emu_user_instance_count(cred->cr_uid) >= max_instances_per_user)
            return (EMU_ERR_MAX_INSTANCES);
    }

    /* If instance exists, check ownership */
    if (inst != NULL) {
        /* Owner can do most operations */
        if (cred->cr_uid == inst->owner_uid)
            return (0);

        /* Non-owner in emu group: check specific operation */
        switch (operation) {
        case EMU_PERM_START:
        case EMU_PERM_STOP:
        case EMU_PERM_CONSOLE:
        case EMU_PERM_STACK:
            /* emu group members can start/stop/console/stack any instance */
            return (0);
        case EMU_PERM_DESTROY:
            /* Only if sysctl allows */
            if (destroy_others)
                return (0);
            return (EPERM);
        default:
            return (EPERM);
        }
    }

    return (0);
}
```

### 5.12 Data Structures

```c
/* Per-instance security context */
struct emu_security_ctx {
    struct ucred *owner;            /* Creating user's credentials */
    uid_t owner_uid;                /* Cached owner UID */
    gid_t owner_gid;                /* Cached owner GID */
    int perm_mask;                  /* Custom permission mask (future) */
};

/* Global access control configuration */
struct emu_access_config {
    int allow_nonroot;              /* Master switch for non-root access */
    gid_t required_group;           /* Required group GID (0 = any) */
    int max_instances_per_user;     /* Per-user instance limit */
    int max_instances_per_group;    /* Per-group instance limit */
    int max_memory_per_user;        /* Per-user memory limit (MB) */
    int destroy_others;             /* Allow emu group to destroy any instance */
};
```
