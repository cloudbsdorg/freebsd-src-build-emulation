/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 The FreeBSD Foundation
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

/*
 * Symbol Resolution for Emulation Framework
 * Task 7.4: Implement symbol resolution for stack trace analysis.
 *
 * This module provides kernel symbol resolution using ELF parsing.
 * It supports resolving addresses from:
 * - Running kernel (/boot/kernel/kernel)
 * - Kernel modules (/boot/kernel)
 * - Custom kernel files specified by the user
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emu.h"

/*
 * Maximum number of symbols to cache
 */
#define	MAX_SYMBOLS	65536
#define	MAX_TABLES	32

/*
 * Symbol entry from kernel symbol table
 */
struct sym_entry {
	const char	*se_name;
	uint64_t	se_addr;
	uint64_t	se_size;
	int		se_type;
};

/*
 * Sort symbol entries by address
 */
static int
sym_compare(const void *a, const void *b)
{
	const struct sym_entry *sa = a;
	const struct sym_entry *sb = b;

	if (sa->se_addr < sb->se_addr)
		return (-1);
	if (sa->se_addr > sb->se_addr)
		return (1);
	return (0);
}

/*
 * Symbol table for a single ELF file
 */
struct sym_table {
	char		*st_path;
	void		*st_symtab;
	void		*st_strtab;
	size_t		st_symtab_size;
	size_t		st_strtab_size;
	int		st_fd;
	struct sym_entry	*st_syms;
	int		st_nsyms;
	time_t		st_fmtime;
	int		st_refcount;
};

/*
 * Global symbol tables
 */
static struct sym_table *sym_kernel;
static struct sym_table *sym_modules[MAX_TABLES];
static int sym_module_count;
static int sym_initialized;

/*
 * Forward declarations for internal functions
 */
static struct sym_table *emu_sym_load_table(const char *path);
static void emu_sym_parse_table(struct sym_table *st);
static void emu_sym_free_table(struct sym_table *st);
static struct sym_entry *sym_find(struct sym_table *st, uint64_t addr);

/* Public function for loading a module's symbols */
struct sym_table *emu_sym_load_module(const char *module_name);

/*
 * Default kernel path
 */
#define	DEFAULT_KERNEL_PATH	"/boot/kernel/kernel"
#define	MODULES_PATH		"/boot/kernel"

/*
 * Initialize symbol resolution system
 */
int
emu_sym_init(void)
{
	sym_kernel = NULL;
	sym_module_count = 0;
	sym_initialized = 1;

	/* Try to load running kernel symbols */
	if (access(DEFAULT_KERNEL_PATH, R_OK) == 0) {
		sym_kernel = emu_sym_load_table(DEFAULT_KERNEL_PATH);
		if (sym_kernel == NULL) {
			fprintf(stderr, "Warning: Could not load kernel symbols from %s\n",
			    DEFAULT_KERNEL_PATH);
		}
	} else {
		/* Try /bsd as fallback */
		if (access("/bsd", R_OK) == 0) {
			sym_kernel = emu_sym_load_table("/bsd");
			if (sym_kernel == NULL) {
				fprintf(stderr, "Warning: Could not load kernel symbols from /bsd\n");
			}
		}
	}

	return (sym_kernel != NULL ? 0 : ENOENT);
}

/*
 * Cleanup symbol resolution system
 */
void
emu_sym_destroy(void)
{
	int i;

	if (sym_kernel != NULL) {
		emu_sym_free_table(sym_kernel);
		sym_kernel = NULL;
	}

	for (i = 0; i < sym_module_count; i++) {
		if (sym_modules[i] != NULL) {
			emu_sym_free_table(sym_modules[i]);
			sym_modules[i] = NULL;
		}
	}
	sym_module_count = 0;
	sym_initialized = 0;
}

/*
 * Load symbol table from an ELF file
 */
struct sym_table *
emu_sym_load_table(const char *path)
{
	struct sym_table *st;
	struct stat sb;
	Elf_Ehdr ehdr;
	Elf_Shdr *shdr = NULL;
	void *shdr_data = NULL;
	void *symtab = NULL, *strtab = NULL;
	size_t symtab_size = 0, strtab_size = 0;
	char *shstrtab = NULL;
	int fd;
	ssize_t bytes;
	int i;

	if (path == NULL)
		return (NULL);

	st = malloc(sizeof(*st));
	if (st == NULL)
		return (NULL);

	st->st_path = strdup(path);
	if (st->st_path == NULL) {
		free(st);
		return (NULL);
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		free(st->st_path);
		free(st);
		return (NULL);
	}

	if (fstat(fd, &sb) < 0) {
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}
	st->st_fmtime = sb.st_mtime;

	/* Read ELF header */
	bytes = read(fd, &ehdr, sizeof(ehdr));
	if (bytes != sizeof(ehdr) || memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}

	/* Check ELF class and endianness */
#if __ELF_WORD_SIZE == 64
	if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}
#else
	if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}
#endif

	/* Read section headers */
	size_t shdr_size = ehdr.e_shentsize * ehdr.e_shnum;
	shdr_data = malloc(shdr_size);
	if (shdr_data == NULL) {
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}

	if (lseek(fd, ehdr.e_shoff, SEEK_SET) < 0) {
		free(shdr_data);
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}

	bytes = read(fd, shdr_data, shdr_size);
	if (bytes != (ssize_t)shdr_size) {
		free(shdr_data);
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}
	shdr = (Elf_Shdr *)shdr_data;

	/* Read section name string table */
	if (ehdr.e_shstrndx >= ehdr.e_shnum) {
		free(shdr_data);
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}

	size_t shstrtab_size = shdr[ehdr.e_shstrndx].sh_size;
	off_t shstrtab_offset = shdr[ehdr.e_shstrndx].sh_offset;
	shstrtab = malloc(shstrtab_size + 1);
	if (shstrtab == NULL) {
		free(shdr_data);
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}

	if (lseek(fd, shstrtab_offset, SEEK_SET) < 0) {
		free(shstrtab);
		free(shdr_data);
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}

	bytes = read(fd, shstrtab, shstrtab_size);
	if (bytes != (ssize_t)shstrtab_size) {
		free(shstrtab);
		free(shdr_data);
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}
	shstrtab[shstrtab_size] = '\0';

	/* Find and read symbol table and string table */
	for (i = 0; i < ehdr.e_shnum; i++) {
		if (shdr[i].sh_type == SHT_SYMTAB || shdr[i].sh_type == SHT_DYNSYM) {
			void *new_symtab;
			void *new_strtab;

			if (shdr[i].sh_link >= ehdr.e_shnum)
				continue;

			new_symtab = malloc(shdr[i].sh_size);
			if (new_symtab == NULL)
				continue;

			if (lseek(fd, shdr[i].sh_offset, SEEK_SET) < 0) {
				free(new_symtab);
				continue;
			}

			bytes = read(fd, new_symtab, shdr[i].sh_size);
			if (bytes != (ssize_t)shdr[i].sh_size) {
				free(new_symtab);
				continue;
			}

			new_strtab = malloc(shdr[shdr[i].sh_link].sh_size);
			if (new_strtab == NULL) {
				free(new_symtab);
				continue;
			}

			if (lseek(fd, shdr[shdr[i].sh_link].sh_offset, SEEK_SET) < 0) {
				free(new_symtab);
				free(new_strtab);
				continue;
			}

			bytes = read(fd, new_strtab, shdr[shdr[i].sh_link].sh_size);
			if (bytes != (ssize_t)shdr[shdr[i].sh_link].sh_size) {
				free(new_symtab);
				free(new_strtab);
				continue;
			}

			/* Prefer .symtab over .dynsym */
			if (symtab == NULL || shdr[i].sh_type == SHT_SYMTAB) {
				if (symtab != NULL) {
					free(symtab);
					free(strtab);
				}
				symtab = new_symtab;
				strtab = new_strtab;
				symtab_size = shdr[i].sh_size;
				strtab_size = shdr[shdr[i].sh_link].sh_size;
			} else {
				free(new_symtab);
				free(new_strtab);
			}
		}
	}

	free(shstrtab);
	free(shdr_data);

	if (symtab == NULL) {
		close(fd);
		free(st->st_path);
		free(st);
		return (NULL);
	}

	st->st_fd = fd;
	st->st_symtab = symtab;
	st->st_strtab = strtab;
	st->st_symtab_size = symtab_size;
	st->st_strtab_size = strtab_size;
	st->st_syms = NULL;
	st->st_nsyms = 0;
	st->st_refcount = 1;

	/* Parse symbol table */
	emu_sym_parse_table(st);

	return (st);
}

/*
 * Parse symbol table into sorted array for binary search
 */
void
emu_sym_parse_table(struct sym_table *st)
{
	Elf_Sym *sym;
	size_t nsyms;
	size_t i;
	struct sym_entry *entries;
	int count = 0;

	if (st == NULL || st->st_symtab == NULL)
		return;

	nsyms = st->st_symtab_size / sizeof(Elf_Sym);
	sym = (Elf_Sym *)st->st_symtab;

	/* First pass: count valid symbols */
	for (i = 0; i < nsyms; i++) {
		const char *name;

		/* Skip undefined and section symbols */
		if (sym[i].st_name == 0 || sym[i].st_shndx == SHN_UNDEF)
			continue;

		/* Get symbol name */
		if (sym[i].st_name >= st->st_strtab_size)
			continue;
		name = (const char *)st->st_strtab + sym[i].st_name;
		if (name == NULL || *name == '\0')
			continue;

		/* Skip empty addresses */
		if (sym[i].st_value == 0)
			continue;

		count++;
	}

	if (count == 0)
		return;

	/* Allocate symbol array */
	entries = calloc(count, sizeof(*entries));
	if (entries == NULL)
		return;

	/* Second pass: populate array */
	count = 0;
	for (i = 0; i < nsyms; i++) {
		const char *name;

		if (sym[i].st_name == 0 || sym[i].st_shndx == SHN_UNDEF)
			continue;

		if (sym[i].st_name >= st->st_strtab_size)
			continue;
		name = (const char *)st->st_strtab + sym[i].st_name;
		if (name == NULL || *name == '\0')
			continue;

		if (sym[i].st_value == 0)
			continue;

		entries[count].se_name = name;
		entries[count].se_addr = sym[i].st_value;
		entries[count].se_size = sym[i].st_size;
		entries[count].se_type = ELF_ST_TYPE(sym[i].st_info);
		count++;
	}

	/* Sort by address for binary search */
	if (count > 1)
		qsort(entries, count, sizeof(*entries), sym_compare);

	st->st_syms = entries;
	st->st_nsyms = count;
}

/*
 * Free a symbol table
 */
void
emu_sym_free_table(struct sym_table *st)
{
	if (st == NULL)
		return;

	if (st->st_syms != NULL)
		free(st->st_syms);
	if (st->st_symtab != NULL)
		free(st->st_symtab);
	if (st->st_strtab != NULL)
		free(st->st_strtab);
	if (st->st_fd >= 0)
		close(st->st_fd);
	if (st->st_path != NULL)
		free(st->st_path);

	free(st);
}

/*
 * Binary search for symbol containing address
 */
static struct sym_entry *
sym_find(struct sym_table *st, uint64_t addr)
{
	struct sym_entry *syms;
	int left, right, mid;
	uint64_t sym_start, sym_end;

	if (st == NULL || st->st_syms == NULL || st->st_nsyms == 0)
		return (NULL);

	syms = st->st_syms;
	left = 0;
	right = st->st_nsyms - 1;

	/* Binary search for largest symbol <= addr */
	while (left <= right) {
		mid = (left + right) / 2;
		if (syms[mid].se_addr > addr) {
			right = mid - 1;
		} else {
			/* Check if addr is within this symbol */
			sym_start = syms[mid].se_addr;
			sym_end = sym_start + syms[mid].se_size;
			if (sym_start <= addr && addr < sym_end)
				return (&syms[mid]);
			left = mid + 1;
		}
	}

	/* Return best match (highest addr <= target) */
	if (right >= 0 && right < st->st_nsyms)
		return (&syms[right]);

	return (NULL);
}

/*
 * Resolve a kernel address to a symbol name
 * Returns 0 on success, errno on failure
 */
int
emu_sym_resolve(uint64_t addr, char *sym_name, size_t sym_name_len,
    uint64_t *sym_offset, const char *module)
{
	struct sym_entry *best = NULL;
	struct sym_table *st;
	int i;

	/* Check kernel symbols first */
	if (sym_kernel != NULL) {
		best = sym_find(sym_kernel, addr);
		if (best != NULL) {
			if (sym_name != NULL && sym_name_len > 0)
				strlcpy(sym_name, best->se_name, sym_name_len);
			if (sym_offset != NULL)
				*sym_offset = addr - best->se_addr;
			return (0);
		}
	}

	/* Check module symbols */
	for (i = 0; i < sym_module_count; i++) {
		st = sym_modules[i];
		if (st != NULL) {
			if (module == NULL || strstr(st->st_path, module) != NULL) {
				best = sym_find(st, addr);
				if (best != NULL) {
					if (sym_name != NULL && sym_name_len > 0)
						strlcpy(sym_name, best->se_name, sym_name_len);
					if (sym_offset != NULL)
						*sym_offset = addr - best->se_addr;
					return (0);
				}
			}
		}
	}

	/* No symbol found */
	if (sym_name != NULL && sym_name_len > 0)
		sym_name[0] = '\0';
	if (sym_offset != NULL)
		*sym_offset = addr;

	return (ENOENT);
}

/*
 * Load symbols from a specific kernel module
 */
struct sym_table *
emu_sym_load_module(const char *module_name)
{
	char path[PATH_MAX];
	struct sym_table *st;
	int i;

	snprintf(path, sizeof(path), "%s/%s.ko", MODULES_PATH, module_name);

	/* Check if already loaded */
	for (i = 0; i < sym_module_count; i++) {
		st = sym_modules[i];
		if (st != NULL && strstr(st->st_path, module_name) != NULL)
			return (st);
	}

	/* Load new module */
	st = emu_sym_load_table(path);
	if (st != NULL && sym_module_count < MAX_TABLES) {
		sym_modules[sym_module_count++] = st;
	}

	return (st);
}

/*
 * Get module name from symbol address
 */
const char *
emu_sym_get_module(uint64_t addr)
{
	struct sym_table *st;
	struct sym_entry *se;
	static char name[256];
	int i;

	/* Check kernel first */
	if (sym_kernel != NULL) {
		se = sym_find(sym_kernel, addr);
		if (se != NULL)
			return ("kernel");
	}

	/* Check modules */
	for (i = 0; i < sym_module_count; i++) {
		st = sym_modules[i];
		if (st != NULL) {
			se = sym_find(st, addr);
			if (se != NULL) {
				/* Extract module name from path */
				const char *base = strrchr(st->st_path, '/');
				if (base != NULL) {
					base++; /* Skip '/' */
					size_t len = strlen(base);
					if (len > 3 && strcmp(base + len - 3, ".ko") == 0) {
						strlcpy(name, base, len - 2);
						name[len - 2] = '\0';
						return (name);
					}
				}
				return (st->st_path);
			}
		}
	}

	return (NULL);
}

/*
 * Load all kernel module symbols
 */
int
emu_sym_load_all_modules(void)
{
	DIR *dir;
	struct dirent *entry;
	char path[PATH_MAX];
	int count = 0;
	struct sym_table *st;

	dir = opendir(MODULES_PATH);
	if (dir == NULL)
		return (0);

	while ((entry = readdir(dir)) != NULL) {
		/* Skip . and .. and non-.ko files */
		size_t len = strlen(entry->d_name);
		if (len <= 3 || strcmp(entry->d_name + len - 3, ".ko") != 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", MODULES_PATH, entry->d_name);

		/* Skip kernel itself */
		if (strcmp(path, DEFAULT_KERNEL_PATH) == 0)
			continue;

		st = emu_sym_load_table(path);
		if (st != NULL && sym_module_count < MAX_TABLES) {
			sym_modules[sym_module_count++] = st;
			count++;
		}
	}

	closedir(dir);
	return (count);
}

/*
 * Get number of loaded symbol tables
 */
int
emu_sym_count(void)
{
	int count = 0;
	int i;

	if (sym_kernel != NULL)
		count++;

	for (i = 0; i < sym_module_count; i++) {
		if (sym_modules[i] != NULL)
			count++;
	}

	return (count);
}
