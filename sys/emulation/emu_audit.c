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
 *
 * $FreeBSD$
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
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/kthread.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/refcount.h>
#include <sys/sbuf.h>

#include <machine/stdarg.h>

#include "emu.h"
#include "emu_audit.h"

/*
 * Emulation Framework Audit Logging Implementation
 * 
 * Provides comprehensive audit logging for all security-relevant events
 * in the emulation framework. Supports multiple output destinations
 * (syslog, file) with configurable rotation and filtering.
 */

/* Global audit state */
static struct emu_audit_state g_audit_state;

/* Sysctl context */
static struct sysctl_ctx_list emu_audit_sysctl_ctx;
static struct sysctl_oid *emu_audit_sysctl_tree;

/* Forward declarations */
static int emu_audit_write_syslog(struct emu_audit_record *rec);
static int emu_audit_write_file(struct emu_audit_record *rec);
static int emu_audit_rotate_file(void);
static int emu_audit_check_rotation(void);
static void emu_audit_format_message(struct emu_audit_record *rec, char *buf, size_t len);

/*
 * Initialize the audit logging subsystem
 */
int
emu_audit_init(void)
{
	int error;

	if (g_audit_state.as_initialized)
		return (0);

	/* Initialize state mutex */
	mtx_init(&g_audit_state.as_mtx, "emu_audit", NULL, MTX_DEF);

	/* Set default configuration */
	g_audit_state.as_config.ac_enabled = 1;
	g_audit_state.as_config.ac_destination = EMU_AUDIT_DEST_SYSLOG;
	strlcpy(g_audit_state.as_config.ac_file_path, "/var/log/emu_audit.log",
	    sizeof(g_audit_state.as_config.ac_file_path));
	g_audit_state.as_config.ac_rotation_size = EMU_AUDIT_DEFAULT_ROTATION_SIZE;
	g_audit_state.as_config.ac_rotation_count = EMU_AUDIT_DEFAULT_ROTATION_COUNT;
	g_audit_state.as_config.ac_min_severity = EMU_AUDIT_SEVERITY_INFO;
	g_audit_state.as_config.ac_include_data = 1;
	g_audit_state.as_event_count = 0;

	/* Initialize writer mutex */
	mtx_init(&g_audit_state.as_writer.aw_mtx, "emu_audit_writer", NULL, MTX_DEF);
	g_audit_state.as_writer.aw_initialized = 0;
	g_audit_state.as_writer.aw_vp = NULL;
	g_audit_state.as_writer.aw_fp = NULL;

	/* Create sysctl tree */
	SYSCTL_DECL(_kern_emulation);
	emu_audit_sysctl_tree = SYSCTL_ADD_NODE(&emu_audit_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_kern_emulation), OID_AUTO, "audit",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, "Audit logging configuration");

	if (emu_audit_sysctl_tree == NULL) {
		mtx_destroy(&g_audit_state.as_mtx);
		mtx_destroy(&g_audit_state.as_writer.aw_mtx);
		return (ENOMEM);
	}

	/* Add sysctl controls */
	SYSCTL_ADD_INT(&emu_audit_sysctl_ctx,
	    SYSCTL_CHILDREN(emu_audit_sysctl_tree), OID_AUTO,
	    "enabled", CTLFLAG_RW, &g_audit_state.as_config.ac_enabled, 0,
	    "Audit logging enabled (0=disabled, 1=enabled)");

	SYSCTL_ADD_INT(&emu_audit_sysctl_ctx,
	    SYSCTL_CHILDREN(emu_audit_sysctl_tree), OID_AUTO,
	    "destination", CTLFLAG_RW, &g_audit_state.as_config.ac_destination, 0,
	    "Audit output destination (0=none, 1=syslog, 2=file, 3=both)");

	SYSCTL_ADD_STRING(&emu_audit_sysctl_ctx,
	    SYSCTL_CHILDREN(emu_audit_sysctl_tree), OID_AUTO,
	    "file_path", CTLFLAG_RW, g_audit_state.as_config.ac_file_path,
	    sizeof(g_audit_state.as_config.ac_file_path),
	    "Audit log file path");

	SYSCTL_ADD_LONG(&emu_audit_sysctl_ctx,
	    SYSCTL_CHILDREN(emu_audit_sysctl_tree), OID_AUTO,
	    "rotation_size", CTLFLAG_RW, &g_audit_state.as_config.ac_rotation_size,
	    "Log file rotation size (bytes)");

	SYSCTL_ADD_INT(&emu_audit_sysctl_ctx,
	    SYSCTL_CHILDREN(emu_audit_sysctl_tree), OID_AUTO,
	    "rotation_count", CTLFLAG_RW, &g_audit_state.as_config.ac_rotation_count,
	    "Number of rotated log files to keep");

	SYSCTL_ADD_INT(&emu_audit_sysctl_ctx,
	    SYSCTL_CHILDREN(emu_audit_sysctl_tree), OID_AUTO,
	    "min_severity", CTLFLAG_RW, &g_audit_state.as_config.ac_min_severity,
	    "Minimum severity level to log (0-7)");

	SYSCTL_ADD_INT(&emu_audit_sysctl_ctx,
	    SYSCTL_CHILDREN(emu_audit_sysctl_tree), OID_AUTO,
	    "include_data", CTLFLAG_RW, &g_audit_state.as_config.ac_include_data,
	    "Include context data in log entries");

	SYSCTL_ADD_INT(&emu_audit_sysctl_ctx,
	    SYSCTL_CHILDREN(emu_audit_sysctl_tree), OID_AUTO,
	    "event_count", CTLFLAG_RD, &g_audit_state.as_event_count,
	    "Total number of audit events logged");

	g_audit_state.as_initialized = 1;

	/* Log initialization event */
	AUDIT_INSTANCE_CREATE("audit_subsystem");

	return (0);
}

/*
 * Destroy the audit logging subsystem
 */
void
emu_audit_destroy(void)
{
	if (!g_audit_state.as_initialized)
		return;

	/* Log shutdown event */
	AUDIT_INSTANCE_DESTROY("audit_subsystem");

	/* Close file if open */
	if (g_audit_state.as_writer.aw_fp != NULL) {
		fclose(g_audit_state.as_writer.aw_fp);
		g_audit_state.as_writer.aw_fp = NULL;
		g_audit_state.as_writer.aw_vp = NULL;
	}

	/* Destroy sysctl tree */
	if (emu_audit_sysctl_tree != NULL) {
		sysctl_ctx_free(&emu_audit_sysctl_ctx);
		emu_audit_sysctl_tree = NULL;
	}

	/* Destroy mutexes */
	mtx_destroy(&g_audit_state.as_mtx);
	mtx_destroy(&g_audit_state.as_writer.aw_mtx);

	g_audit_state.as_initialized = 0;
}

/*
 * Format audit record into a log message
 */
static void
emu_audit_format_message(struct emu_audit_record *rec, char *buf, size_t len)
{
	struct tm *tm;
	char timestamp[64];
	const char *event_name;
	const char *severity_name;

	/* Format timestamp */
	tm = localtime(&rec->ar_timestamp.tv_sec);
	if (tm != NULL) {
		snprintf(timestamp, sizeof(timestamp),
		    "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
		    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		    tm->tm_hour, tm->tm_min, tm->tm_sec,
		    rec->ar_timestamp.tv_usec / 1000);
	} else {
		strlcpy(timestamp, "unknown", sizeof(timestamp));
	}

	/* Get event name */
	if (rec->ar_event < EMU_AUDIT_EVENT_MAX &&
	    emu_audit_event_names[rec->ar_event] != NULL) {
		event_name = emu_audit_event_names[rec->ar_event];
	} else {
		event_name = "UNKNOWN";
	}

	/* Get severity name */
	switch (rec->ar_severity) {
	case EMU_AUDIT_SEVERITY_EMERG:
		severity_name = "EMERG";
		break;
	case EMU_AUDIT_SEVERITY_ALERT:
		severity_name = "ALERT";
		break;
	case EMU_AUDIT_SEVERITY_CRIT:
		severity_name = "CRIT";
		break;
	case EMU_AUDIT_SEVERITY_ERR:
		severity_name = "ERR";
		break;
	case EMU_AUDIT_SEVERITY_WARNING:
		severity_name = "WARNING";
		break;
	case EMU_AUDIT_SEVERITY_NOTICE:
		severity_name = "NOTICE";
		break;
	case EMU_AUDIT_SEVERITY_INFO:
		severity_name = "INFO";
		break;
	case EMU_AUDIT_SEVERITY_DEBUG:
		severity_name = "DEBUG";
		break;
	default:
		severity_name = "UNKNOWN";
		break;
	}

	/* Format complete message */
	if (g_audit_state.as_config.ac_include_data &&
	    rec->ar_data[0] != '\0') {
		snprintf(buf, len,
		    "[%s] [%s] [%s] [pid:%d tid:%d] [uid:%d gid:%d] "
		    "[instance:%s] %s - data: %s",
		    timestamp, severity_name, event_name,
		    rec->ar_pid, rec->ar_tid,
		    rec->ar_uid, rec->ar_gid,
		    rec->ar_instance[0] != '\0' ? rec->ar_instance : "-",
		    rec->ar_message, rec->ar_data);
	} else {
		snprintf(buf, len,
		    "[%s] [%s] [%s] [pid:%d tid:%d] [uid:%d gid:%d] "
		    "[instance:%s] %s",
		    timestamp, severity_name, event_name,
		    rec->ar_pid, rec->ar_tid,
		    rec->ar_uid, rec->ar_gid,
		    rec->ar_instance[0] != '\0' ? rec->ar_instance : "-",
		    rec->ar_message);
	}
}

/*
 * Write audit record to syslog
 */
static int
emu_audit_write_syslog(struct emu_audit_record *rec)
{
	char buf[EMU_AUDIT_MAX_MSG_LEN + EMU_AUDIT_MAX_DATA_LEN + 256];
	int priority;

	/* Get syslog priority from severity */
	if (rec->ar_severity >= 0 &&
	    rec->ar_severity <= EMU_AUDIT_SEVERITY_DEBUG) {
		priority = emu_audit_severity_syslog[rec->ar_severity];
	} else {
		priority = LOG_INFO;
	}

	/* Format message */
	emu_audit_format_message(rec, buf, sizeof(buf));

	/* Write to syslog */
	log(priority, "emu: %s\n", buf);

	return (0);
}

/*
 * Write audit record to file
 */
static int
emu_audit_write_file(struct emu_audit_record *rec)
{
	char buf[EMU_AUDIT_MAX_MSG_LEN + EMU_AUDIT_MAX_DATA_LEN + 256];
	FILE *fp;
	int error;

	/* Check if file is open */
	if (g_audit_state.as_writer.aw_fp == NULL) {
		/* Open file */
		fp = fopen(g_audit_state.as_config.ac_file_path, "a");
		if (fp == NULL) {
			return (errno);
		}
		g_audit_state.as_writer.aw_fp = fp;
	}

	/* Check rotation */
	error = emu_audit_check_rotation();
	if (error != 0) {
		return (error);
	}

	/* Format message */
	emu_audit_format_message(rec, buf, sizeof(buf));

	/* Write to file */
	mtx_lock(&g_audit_state.as_writer.aw_mtx);
	fprintf(g_audit_state.as_writer.aw_fp, "%s\n", buf);
	fflush(g_audit_state.as_writer.aw_fp);
	mtx_unlock(&g_audit_state.as_writer.aw_mtx);

	return (0);
}

/*
 * Check if log file rotation is needed
 */
static int
emu_audit_check_rotation(void)
{
	struct stat sb;
	int error;

	if (g_audit_state.as_writer.aw_fp == NULL)
		return (0);

	error = fstat(fileno(g_audit_state.as_writer.aw_fp), &sb);
	if (error != 0)
		return (errno);

	if (sb.st_size >= g_audit_state.as_config.ac_rotation_size) {
		return (emu_audit_rotate_file());
	}

	return (0);
}

/*
 * Rotate log file
 */
static int
emu_audit_rotate_file(void)
{
	char old_path[512];
	char new_path[512];
	int i, error;

	mtx_lock(&g_audit_state.as_writer.aw_mtx);

	/* Close current file */
	if (g_audit_state.as_writer.aw_fp != NULL) {
		fclose(g_audit_state.as_writer.aw_fp);
		g_audit_state.as_writer.aw_fp = NULL;
		g_audit_state.as_writer.aw_vp = NULL;
	}

	/* Rotate existing files */
	for (i = g_audit_state.as_config.ac_rotation_count - 1; i >= 1; i--) {
		snprintf(old_path, sizeof(old_path),
		    "%s.%d", g_audit_state.as_config.ac_file_path, i);
		snprintf(new_path, sizeof(new_path),
		    "%s.%d", g_audit_state.as_config.ac_file_path, i + 1);
		rename(old_path, new_path);
	}

	/* Rotate current log to .1 */
	snprintf(new_path, sizeof(new_path),
	    "%s.1", g_audit_state.as_config.ac_file_path);
	rename(g_audit_state.as_config.ac_file_path, new_path);

	mtx_unlock(&g_audit_state.as_writer.aw_mtx);

	return (0);
}

/*
 * Log an audit event (variadic)
 */
int
emu_audit_log(emu_audit_event_t event, emu_audit_severity_t severity,
    const char *instance, const char *fmt, ...)
{
	struct emu_audit_record rec;
	struct thread *td;
	va_list ap;
	int error;

	if (!g_audit_state.as_initialized ||
	    !g_audit_state.as_config.ac_enabled)
		return (0);

	/* Check minimum severity */
	if (severity < g_audit_state.as_config.ac_min_severity)
		return (0);

	/* Initialize record */
	bzero(&rec, sizeof(rec));
	rec.ar_event = event;
	rec.ar_severity = severity;
	getmicrotime(&rec.ar_timestamp);

	/* Get current thread/process info */
	td = curthread;
	if (td != NULL && td->td_proc != NULL) {
		rec.ar_pid = td->td_proc->p_pid;
		rec.ar_tid = td->td_tid;
		rec.ar_uid = td->td_proc->p_ucred->cr_uid;
		rec.ar_gid = td->td_proc->p_ucred->cr_gid;
	} else {
		rec.ar_pid = 0;
		rec.ar_tid = 0;
		rec.ar_uid = 0;
		rec.ar_gid = 0;
	}

	/* Copy instance name */
	if (instance != NULL)
		strlcpy(rec.ar_instance, instance, sizeof(rec.ar_instance));

	/* Format message */
	va_start(ap, fmt);
	vsnprintf(rec.ar_message, sizeof(rec.ar_message), fmt, ap);
	va_end(ap);

	/* Write to syslog */
	if (g_audit_state.as_config.ac_destination == EMU_AUDIT_DEST_SYSLOG ||
	    g_audit_state.as_config.ac_destination == EMU_AUDIT_DEST_BOTH) {
		error = emu_audit_write_syslog(&rec);
		if (error != 0)
			return (error);
	}

	/* Write to file */
	if (g_audit_state.as_config.ac_destination == EMU_AUDIT_DEST_FILE ||
	    g_audit_state.as_config.ac_destination == EMU_AUDIT_DEST_BOTH) {
		error = emu_audit_write_file(&rec);
		if (error != 0)
			return (error);
	}

	/* Increment event count */
	mtx_lock(&g_audit_state.as_mtx);
	g_audit_state.as_event_count++;
	mtx_unlock(&g_audit_state.as_mtx);

	return (0);
}

/*
 * Log an audit event with context data
 */
int
emu_audit_log_data(emu_audit_event_t event, emu_audit_severity_t severity,
    const char *instance, const char *data, const char *fmt, ...)
{
	struct emu_audit_record rec;
	struct thread *td;
	va_list ap;
	int error;

	if (!g_audit_state.as_initialized ||
	    !g_audit_state.as_config.ac_enabled)
		return (0);

	/* Check minimum severity */
	if (severity < g_audit_state.as_config.ac_min_severity)
		return (0);

	/* Initialize record */
	bzero(&rec, sizeof(rec));
	rec.ar_event = event;
	rec.ar_severity = severity;
	getmicrotime(&rec.ar_timestamp);

	/* Get current thread/process info */
	td = curthread;
	if (td != NULL && td->td_proc != NULL) {
		rec.ar_pid = td->td_proc->p_pid;
		rec.ar_tid = td->td_tid;
		rec.ar_uid = td->td_proc->p_ucred->cr_uid;
		rec.ar_gid = td->td_proc->p_ucred->cr_gid;
	} else {
		rec.ar_pid = 0;
		rec.ar_tid = 0;
		rec.ar_uid = 0;
		rec.ar_gid = 0;
	}

	/* Copy instance name */
	if (instance != NULL)
		strlcpy(rec.ar_instance, instance, sizeof(rec.ar_instance));

	/* Copy context data */
	if (data != NULL)
		strlcpy(rec.ar_data, data, sizeof(rec.ar_data));

	/* Format message */
	va_start(ap, fmt);
	vsnprintf(rec.ar_message, sizeof(rec.ar_message), fmt, ap);
	va_end(ap);

	/* Write to syslog */
	if (g_audit_state.as_config.ac_destination == EMU_AUDIT_DEST_SYSLOG ||
	    g_audit_state.as_config.ac_destination == EMU_AUDIT_DEST_BOTH) {
		error = emu_audit_write_syslog(&rec);
		if (error != 0)
			return (error);
	}

	/* Write to file */
	if (g_audit_state.as_config.ac_destination == EMU_AUDIT_DEST_FILE ||
	    g_audit_state.as_config.ac_destination == EMU_AUDIT_DEST_BOTH) {
		error = emu_audit_write_file(&rec);
		if (error != 0)
			return (error);
	}

	/* Increment event count */
	mtx_lock(&g_audit_state.as_mtx);
	g_audit_state.as_event_count++;
	mtx_unlock(&g_audit_state.as_mtx);

	return (0);
}

/*
 * Check if audit logging is enabled
 */
int
emu_audit_is_enabled(void)
{
	return (g_audit_state.as_initialized &&
	    g_audit_state.as_config.ac_enabled);
}

/*
 * Get current audit destination
 */
int
emu_audit_get_destination(void)
{
	return (g_audit_state.as_config.ac_destination);
}

/*
 * Set audit destination
 */
int
emu_audit_set_destination(emu_audit_destination_t dest)
{
	if (!g_audit_state.as_initialized)
		return (ENXIO);

	mtx_lock(&g_audit_state.as_mtx);
	g_audit_state.as_config.ac_destination = dest;
	mtx_unlock(&g_audit_state.as_mtx);

	AUDIT_AUDIT_CONFIG_CHANGE("audit_subsystem");

	return (0);
}

/*
 * Set audit log file path
 */
int
emu_audit_set_file_path(const char *path)
{
	if (!g_audit_state.as_initialized)
		return (ENXIO);

	if (path == NULL || strlen(path) == 0)
		return (EINVAL);

	mtx_lock(&g_audit_state.as_mtx);
	strlcpy(g_audit_state.as_config.ac_file_path, path,
	    sizeof(g_audit_state.as_config.ac_file_path));
	mtx_unlock(&g_audit_state.as_mtx);

	AUDIT_AUDIT_CONFIG_CHANGE("audit_subsystem");

	return (0);
}

/*
 * Set log rotation parameters
 */
int
emu_audit_set_rotation(size_t size, int count)
{
	if (!g_audit_state.as_initialized)
		return (ENXIO);

	if (size < 1024 * 1024 || count < 1)
		return (EINVAL);

	mtx_lock(&g_audit_state.as_mtx);
	g_audit_state.as_config.ac_rotation_size = size;
	g_audit_state.as_config.ac_rotation_count = count;
	mtx_unlock(&g_audit_state.as_mtx);

	AUDIT_LIMIT_CHANGE("audit_subsystem");

	return (0);
}

/*
 * Set minimum severity level
 */
int
emu_audit_set_min_severity(emu_audit_severity_t severity)
{
	if (!g_audit_state.as_initialized)
		return (ENXIO);

	if (severity > EMU_AUDIT_SEVERITY_DEBUG)
		return (EINVAL);

	mtx_lock(&g_audit_state.as_mtx);
	g_audit_state.as_config.ac_min_severity = severity;
	mtx_unlock(&g_audit_state.as_mtx);

	AUDIT_LIMIT_CHANGE("audit_subsystem");

	return (0);
}

/*
 * Check if thread can access audit log (root only)
 */
int
emu_audit_check_access(struct thread *td)
{
	int error;

	if (td == NULL)
		return (EINVAL);

	error = priv_check(td, PRIV_EMU_AUDIT);
	if (error != 0) {
		AUDIT_PERM_DENIED("audit_access", "non-root access attempt");
		return (error);
	}

	AUDIT_AUDIT_LOG_ACCESS("audit_access");

	return (0);
}

/*
 * Read audit log (restricted to root)
 */
int
emu_audit_read_log(struct uio *uio)
{
	struct sbuf *sb;
	int error;

	/* Check access */
	error = emu_audit_check_access(curthread);
	if (error != 0)
		return (error);

	/* Create sbuf for log content */
	sb = sbuf_new_auto();
	if (sb == NULL)
		return (ENOMEM);

	/* TODO: Read log file content into sbuf */
	sbuf_printf(sb, "Audit log - %d events logged\n",
	    g_audit_state.as_event_count);

	/* Copy to userland */
	error = sbuf_finish(sb);
	if (error == 0) {
		error = uiomove(sbuf_data(sb), sbuf_len(sb), uio);
	}

	sbuf_delete(sb);

	return (error);
}
