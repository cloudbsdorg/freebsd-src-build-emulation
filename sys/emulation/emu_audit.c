/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 FreeBSD Emulation Framework Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/sbuf.h>
#include <sys/syslog.h>

#include <machine/stdarg.h>

#include "emu.h"
#include "emu_sysctl.h"
#include "emu_audit.h"

/*
 * Event name strings
 */
const char *emu_audit_event_names[EMU_AUDIT_EVENT_MAX] = {
	[EMU_AUDIT_EVENT_NONE] = "NONE",
	[EMU_AUDIT_EVENT_INSTANCE_CREATE] = "INSTANCE_CREATE",
	[EMU_AUDIT_EVENT_INSTANCE_DESTROY] = "INSTANCE_DESTROY",
	[EMU_AUDIT_EVENT_INSTANCE_START] = "INSTANCE_START",
	[EMU_AUDIT_EVENT_INSTANCE_STOP] = "INSTANCE_STOP",
	[EMU_AUDIT_EVENT_INSTANCE_PAUSE] = "INSTANCE_PAUSE",
	[EMU_AUDIT_EVENT_INSTANCE_RESUME] = "INSTANCE_RESUME",
	[EMU_AUDIT_EVENT_MEMORY_ALLOC] = "MEMORY_ALLOC",
	[EMU_AUDIT_EVENT_MEMORY_FREE] = "MEMORY_FREE",
	[EMU_AUDIT_EVENT_PERMISSION_DENIED] = "PERMISSION_DENIED",
	[EMU_AUDIT_EVENT_SECURELEVEL_VIOLATION] = "SECURELEVEL_VIOLATION",
	[EMU_AUDIT_EVENT_MAC_VIOLATION] = "MAC_VIOLATION",
	[EMU_AUDIT_EVENT_PTRACE_ATTACH] = "PTRACE_ATTACH",
	[EMU_AUDIT_EVENT_PTRACE_DETACH] = "PTRACE_DETACH",
	[EMU_AUDIT_EVENT_AUDIT_ACCESS] = "AUDIT_ACCESS",
	[EMU_AUDIT_EVENT_SHARE_MOUNT] = "SHARE_MOUNT",
	[EMU_AUDIT_EVENT_SHARE_UNMOUNT] = "SHARE_UNMOUNT",
	[EMU_AUDIT_EVENT_SNAPSHOT_CREATE] = "SNAPSHOT_CREATE",
	[EMU_AUDIT_EVENT_SNAPSHOT_DELETE] = "SNAPSHOT_DELETE",
	[EMU_AUDIT_EVENT_MMIO_VIOLATION] = "MMIO_VIOLATION",
	[EMU_AUDIT_EVENT_SHARE_PATH_INVALID] = "SHARE_PATH_INVALID",
};

/*
 * Global audit state
 */
static struct emu_audit_state g_audit_state;
static struct mtx emu_audit_mtx;
static uint64_t emu_audit_event_count;

/*
 * Sysctl context and node for audit
 */
static struct sysctl_ctx_list emu_audit_sysctl_ctx;
static struct sysctl_oid *emu_audit_sysctl_oid;

/*
 * Initialize audit subsystem
 */
int
emu_audit_init(void)
{

	if (g_audit_state.as_initialized)
		return (0);

	mtx_init(&emu_audit_mtx, "emu_audit", NULL, MTX_DEF);
	g_audit_state.as_enabled = 1;
	g_audit_state.as_min_severity = EMU_AUDIT_SEVERITY_INFO;
	g_audit_state.as_event_count = 0;
	g_audit_state.as_initialized = 1;

	/* Initialize sysctl context */
	if (sysctl_ctx_init(&emu_audit_sysctl_ctx) == 0) {
		emu_audit_sysctl_oid = SYSCTL_ADD_NODE(&emu_audit_sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_kern_emulation), OID_AUTO, "audit",
		    CTLFLAG_RW, NULL, "Audit logging");

		if (emu_audit_sysctl_oid != NULL) {
			SYSCTL_ADD_INT(&emu_audit_sysctl_ctx,
			    SYSCTL_CHILDREN(emu_audit_sysctl_oid), OID_AUTO, "enabled",
			    CTLFLAG_RWTUN, &g_audit_state.as_enabled, 0,
			    "Enable audit logging (0=disabled, 1=enabled)");
			SYSCTL_ADD_INT(&emu_audit_sysctl_ctx,
			    SYSCTL_CHILDREN(emu_audit_sysctl_oid), OID_AUTO, "min_severity",
			    CTLFLAG_RWTUN, &g_audit_state.as_min_severity, 0,
			    "Minimum severity level to log (0=EMERG, 7=DEBUG)");
			SYSCTL_ADD_U64(&emu_audit_sysctl_ctx,
			    SYSCTL_CHILDREN(emu_audit_sysctl_oid), OID_AUTO, "event_count",
			    CTLFLAG_RD, &emu_audit_event_count, 0,
			    "Total audit events logged");
		}
	}

	return (0);
}

/*
 * Destroy audit subsystem
 */
void
emu_audit_destroy(void)
{

	if (!g_audit_state.as_initialized)
		return;

	sysctl_ctx_free(&emu_audit_sysctl_ctx);
	mtx_destroy(&emu_audit_mtx);
	g_audit_state.as_initialized = 0;
}

/*
 * Log an audit event
 */
int
emu_audit_log(emu_audit_event_t event, emu_audit_severity_t severity,
    const char *instance, const char *fmt, ...)
{
	char buf[EMU_AUDIT_MAX_MSG_LEN];
	va_list ap;
	int priority;

	if (!g_audit_state.as_initialized || !g_audit_state.as_enabled)
		return (0);

	if (severity < g_audit_state.as_min_severity)
		return (0);

	/* Format message */
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/* Get syslog priority */
	switch (severity) {
	case EMU_AUDIT_SEVERITY_EMERG:
		priority = LOG_EMERG; break;
	case EMU_AUDIT_SEVERITY_ALERT:
		priority = LOG_ALERT; break;
	case EMU_AUDIT_SEVERITY_CRIT:
		priority = LOG_CRIT; break;
	case EMU_AUDIT_SEVERITY_ERR:
		priority = LOG_ERR; break;
	case EMU_AUDIT_SEVERITY_WARNING:
		priority = LOG_WARNING; break;
	case EMU_AUDIT_SEVERITY_NOTICE:
		priority = LOG_NOTICE; break;
	case EMU_AUDIT_SEVERITY_INFO:
		priority = LOG_INFO; break;
	case EMU_AUDIT_SEVERITY_DEBUG:
	default:
		priority = LOG_DEBUG; break;
	}

	/* Log to syslog */
	log(priority, "emu: [%s] [%s] %s: %s\n",
	    instance != NULL ? instance : "-",
	    emu_audit_event_names[event],
	    event < EMU_AUDIT_EVENT_MAX ? emu_audit_event_names[event] : "UNKNOWN",
	    buf);

	/* Increment counter */
	mtx_lock(&emu_audit_mtx);
	emu_audit_event_count++;
	mtx_unlock(&emu_audit_mtx);

	return (0);
}

/*
 * Log an audit event with binary data
 */
int
emu_audit_log_data(emu_audit_event_t event, emu_audit_severity_t severity,
    const char *instance, const void *data, size_t datalen, const char *fmt, ...)
{
	char msg_buf[EMU_AUDIT_MAX_MSG_LEN];
	char data_buf[EMU_AUDIT_MAX_DATA_LEN * 2 + 1];
	va_list ap;
	const uint8_t *p;
	size_t i;
	int priority;

	if (!g_audit_state.as_initialized || !g_audit_state.as_enabled)
		return (0);

	if (severity < g_audit_state.as_min_severity)
		return (0);

	/* Format message */
	va_start(ap, fmt);
	vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
	va_end(ap);

	/* Format data as hex */
	if (data != NULL && datalen > 0 && datalen <= EMU_AUDIT_MAX_DATA_LEN) {
		p = data;
		for (i = 0; i < datalen && i < EMU_AUDIT_MAX_DATA_LEN; i++)
			snprintf(&data_buf[i * 2], 3, "%02x", p[i]);
	} else {
		data_buf[0] = '\0';
	}

	/* Get syslog priority */
	switch (severity) {
	case EMU_AUDIT_SEVERITY_EMERG:
		priority = LOG_EMERG; break;
	case EMU_AUDIT_SEVERITY_ALERT:
		priority = LOG_ALERT; break;
	case EMU_AUDIT_SEVERITY_CRIT:
		priority = LOG_CRIT; break;
	case EMU_AUDIT_SEVERITY_ERR:
		priority = LOG_ERR; break;
	case EMU_AUDIT_SEVERITY_WARNING:
		priority = LOG_WARNING; break;
	case EMU_AUDIT_SEVERITY_NOTICE:
		priority = LOG_NOTICE; break;
	case EMU_AUDIT_SEVERITY_INFO:
		priority = LOG_INFO; break;
	case EMU_AUDIT_SEVERITY_DEBUG:
	default:
		priority = LOG_DEBUG; break;
	}

	/* Log to syslog */
	if (data_buf[0] != '\0') {
		log(priority, "emu: [%s] [%s] %s [data: %s]\n",
		    instance != NULL ? instance : "-",
		    emu_audit_event_names[event],
		    msg_buf,
		    data_buf);
	} else {
		log(priority, "emu: [%s] [%s] %s\n",
		    instance != NULL ? instance : "-",
		    emu_audit_event_names[event],
		    msg_buf);
	}

	/* Increment counter */
	mtx_lock(&emu_audit_mtx);
	emu_audit_event_count++;
	mtx_unlock(&emu_audit_mtx);

	return (0);
}

/*
 * Read audit log (returns summary via sysctl)
 */
int
emu_audit_read_log(struct uio *uio)
{
	struct sbuf *sb;
	int error;

	/* Create sbuf for output */
	sb = sbuf_new_auto();
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&emu_audit_mtx);
	sbuf_printf(sb, "Audit subsystem status:\n");
	sbuf_printf(sb, "  Enabled: %s\n", g_audit_state.as_enabled ? "yes" : "no");
	sbuf_printf(sb, "  Min severity: %d\n", g_audit_state.as_min_severity);
	sbuf_printf(sb, "  Event count: %llu\n",
	    (unsigned long long)emu_audit_event_count);
	sbuf_printf(sb, "\nAudit events are logged to syslog.\n");
	sbuf_printf(sb, "Use 'tail -f /var/log/messages' to view.\n");
	mtx_unlock(&emu_audit_mtx);

	/* Copy to userland */
	error = sbuf_finish(sb);
	if (error == 0)
		error = uiomove(sbuf_data(sb), sbuf_len(sb), uio);

	sbuf_delete(sb);
	return (error);
}
