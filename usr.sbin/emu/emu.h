/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _EMU_H_
#define	_EMU_H_

#include <sys/types.h>
#include <sys/cdefs.h>

/*
 * Emulation Framework Userland Tool
 *
 * This header provides shared definitions and APIs for the emu CLI tool.
 */

/* Maximum lengths for various fields */
#define	EMU_NAME_MAX		64		/* Maximum instance name length */
#define	EMU_PATH_MAX		PATH_MAX	/* Maximum path length */
#define	EMU_ARCH_MAX		16		/* Maximum architecture name length */
#define	EMU_CPU_LEVEL_MAX	32		/* Maximum CPU level description */

/* Supported architectures */
enum emu_arch {
	EMU_ARCH_UNKNOWN = 0,
	EMU_ARCH_AMD64,
	EMU_ARCH_I386,
	EMU_ARCH_ARM64,
	EMU_ARCH_ARM,
	EMU_ARCH_POWERPC,
	EMU_ARCH_RISCV
};

/* Execution modes */
enum emu_mode {
	EMU_MODE_AUTO = 0,	/* Auto-select based on host/target */
	EMU_MODE_BHYVE,		/* Use bhyve/VMM (native only) */
	EMU_MODE_EMULATOR	/* Use custom software emulator */
};

/* Instance states */
enum emu_state {
	EMU_STATE_UNKNOWN = 0,
	EMU_STATE_INITIALIZING,
	EMU_STATE_STOPPED,
	EMU_STATE_RUNNING,
	EMU_STATE_PAUSED,
	EMU_STATE_ERROR
};

/* Output formats */
enum emu_output_format {
	EMU_OUTPUT_TEXT = 0,
	EMU_OUTPUT_JSON,
	EMU_OUTPUT_TAP,
	EMU_OUTPUT_JUNIT
};

/* Instance configuration */
struct emu_instance_config {
	char		name[EMU_NAME_MAX];
	enum emu_arch	arch;
	enum emu_mode	mode;
	char		cpu_level[EMU_CPU_LEVEL_MAX];
	int		cpu_speed_mhz;
	size_t		memory_size;
	int		num_cpus;
	char		image_path[EMU_PATH_MAX];
	char		kernel_path[EMU_PATH_MAX];
	char		blob_path[EMU_PATH_MAX];
};

/* Instance state */
struct emu_instance_state {
	char		name[EMU_NAME_MAX];
	enum emu_arch	arch;
	enum emu_mode	mode;
	enum emu_state	state;
	pid_t		pid;
	size_t		memory_used;
	uint64_t	uptime_sec;
	size_t		console_size;
};

/* Stack output structure */
struct emu_stack_frame {
	uint64_t	pc;
	uint64_t	sp;
	uint64_t	fp;
	char		symbol[256];
	char		module[EMU_NAME_MAX];
};

struct emu_stack_output {
	struct emu_stack_frame *frames;
	int		num_frames;
	char		arch[EMU_ARCH_MAX];
	time_t		timestamp;
};

/* Function declarations for subcommands */
__BEGIN_DECLS
int	emu_cmd_init(int argc, char *argv[]);
int	emu_cmd_start(int argc, char *argv[]);
int	emu_cmd_stop(int argc, char *argv[]);
int	emu_cmd_status(int argc, char *argv[]);
int	emu_cmd_load(int argc, char *argv[]);
int	emu_cmd_unload(int argc, char *argv[]);
int	emu_cmd_stack(int argc, char *argv[]);
int	emu_cmd_test(int argc, char *argv[]);
int	emu_cmd_console(int argc, char *argv[]);
int	emu_cmd_destroy(int argc, char *argv[]);
int	emu_cmd_list(int argc, char *argv[]);
int	emu_cmd_snapshot(int argc, char *argv[]);
int	emu_cmd_restore(int argc, char *argv[]);
int	emu_cmd_blob(int argc, char *argv[]);

/* Utility functions */
extern int		g_verbose;
extern int		g_quiet;
const char	*emu_arch_to_string(enum emu_arch arch);
enum emu_arch	emu_string_to_arch(const char *str);
const char	*emu_mode_to_string(enum emu_mode mode);
enum emu_mode	emu_string_to_mode(const char *str);
const char	*emu_state_to_string(enum emu_state state);
void		emu_output_json_begin(void);
void		emu_output_json_end(void);
void		emu_output_json_object_begin(const char *key);
void		emu_output_json_object_end(int more);
void		emu_output_json_array_begin(const char *key);
void		emu_output_json_array_end(int more);
void		emu_output_json_string(const char *key, const char *value);
void		emu_output_json_int(const char *key, int64_t value);
void		emu_output_json_uint(const char *key, uint64_t value);
void		emu_output_json_bool(const char *key, int value);
void		emu_output_tap_plan(int ntests);
void		emu_output_tap_ok(int testnum, const char *description, ...);
void		emu_output_tap_not_ok(int testnum, const char *description, ...);
void		emu_output_tap_skip(int testnum, const char *reason);
void		emu_output_tap_diag(const char *message);
void		emu_output_junit_begin(const char *suite_name, int tests, int failures,
							int errors, double time);
void		emu_output_junit_end(void);
void		emu_output_junit_testcase(const char *name, const char *classname,
							double time, const char *failure_message,
							const char *failure_type);
void		emu_output_table_header(const char **headers, int ncols);
void		emu_output_table_row(const char **values, int ncols);
void		emu_output_table_separator(void);
void		emu_output_instance(const struct emu_instance_state *state);
void		emu_output_instance_list_header(void);
void		emu_set_output_format(enum emu_output_format format);
enum emu_output_format	emu_get_output_format(void);
void		emu_output_error(const char *fmt, ...);
void		emu_output_info(const char *fmt, ...);
void		emu_output_verbose(const char *fmt, ...);
__END_DECLS

#endif /* !_EMU_H_ */
