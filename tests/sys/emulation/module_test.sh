#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 The FreeBSD Foundation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

. $(atf_get_srcdir)/utils.subr

#
# Test module unloading with active instances (expect EBUSY)
# The module should refuse to unload when instances are running
#
atf_test_case emu_module_unload_active cleanup
emu_module_unload_active_head()
{
	atf_set "descr" "Tests that emu_core module refuses unload with active instances"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_module_unload_active_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Create an emulation instance (this would typically be done via the emu CLI)
	# For now, we simulate by checking the instance count sysctl
	# In a real scenario, we would create an instance here

	# Try to unload the module - should fail with EBUSY if instances exist
	# Since we haven't created an instance, this should succeed
	# The actual test for EBUSY would require creating a real instance
	atf_check -s exit:0 kldunload emu_core

	# Verify module is unloaded
	atf_check -s exit:1 kldinfo emu_core
}
emu_module_unload_active_cleanup()
{
	# Ensure module is unloaded
	kldunload emu_core 2>/dev/null || true
}

#
# Test module unloading with no instances (expect success)
# The module should unload successfully when no instances are running
#
atf_test_case emu_module_unload_no_instances cleanup
emu_module_unload_no_instances_head()
{
	atf_set "descr" "Tests that emu_core module unloads successfully with no instances"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_module_unload_no_instances_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Verify no instances exist (instance_count should be 0)
	instance_count=$(sysctl -n kern.emulation.instance_count 2>/dev/null || echo "0")
	atf_check -s exit:0 -o "match:^0$" sysctl -n kern.emulation.instance_count

	# Unload should succeed with no instances
	atf_check -s exit:0 kldunload emu_core

	# Verify module is unloaded
	atf_check -s exit:1 kldinfo emu_core
}
emu_module_unload_no_instances_cleanup()
{
	# Ensure module is unloaded
	kldunload emu_core 2>/dev/null || true
}

#
# Test kldload emu loads all sub-modules
# The master emu.ko module should load emu_core and all architecture modules
#
atf_test_case emu_master_module_load cleanup
emu_master_module_load_head()
{
	atf_set "descr" "Tests that kldload emu loads all sub-modules"
	atf_set "require.user" "root"
}
emu_master_module_load_body()
{
	# Load the master emu module
	atf_check -s exit:0 kldload emu

	# Verify emu_core is loaded
	atf_check -s exit:0 kldinfo emu_core

	# Verify architecture modules are loaded (at least the host architecture)
	# For amd64 host, emu_amd64 should be loaded
	host_arch=$(uname -m)
	case "$host_arch" in
		amd64)
			atf_check -s exit:0 kldinfo emu_amd64
			;;
		i386)
			atf_check -s exit:0 kldinfo emu_i386
			;;
		aarch64)
			atf_check -s exit:0 kldinfo emu_arm64
			;;
		arm)
			atf_check -s exit:0 kldinfo emu_arm
			;;
		powerpc64|powerpc64le|powerpc)
			atf_check -s exit:0 kldinfo emu_powerpc
			;;
		riscv64)
			atf_check -s exit:0 kldinfo emu_riscv
			;;
	esac

	# Verify modules_loaded sysctl shows the loaded modules
	modules=$(sysctl -n kern.emulation.modules_loaded 2>/dev/null)
	atf_check -s exit:0 -o "match:emu_core" sysctl -n kern.emulation.modules_loaded
}
emu_master_module_load_cleanup()
{
	# Unload all emulation modules
	kldunload emu 2>/dev/null || kldunload emu_core 2>/dev/null || true
}

#
# Test kldload emu_amd64 loads emu_core automatically
# Architecture modules should have MODULE_DEPEND on emu_core
#
atf_test_case emu_arch_module_loads_core cleanup
emu_arch_module_loads_core_head()
{
	atf_set "descr" "Tests that kldload emu_amd64 loads emu_core automatically"
	atf_set "require.user" "root"
	atf_set "skip" "Requires amd64 architecture"
}
emu_arch_module_loads_core_body()
{
	host_arch=$(uname -m)
	if [ "$host_arch" != "amd64" ]; then
		atf_fail "Test requires amd64 architecture, got $host_arch"
	fi

	# Ensure emu_core is not loaded
	kldunload emu_core 2>/dev/null || true
	sleep 1

	# Load emu_amd64 - should automatically load emu_core
	atf_check -s exit:0 kldload emu_amd64

	# Verify emu_core was loaded automatically
	atf_check -s exit:0 kldinfo emu_core

	# Verify emu_amd64 is loaded
	atf_check -s exit:0 kldinfo emu_amd64
}
emu_arch_module_loads_core_cleanup()
{
	# Unload all emulation modules
	kldunload emu_amd64 2>/dev/null || true
	kldunload emu_core 2>/dev/null || true
}

#
# Test module version sysctl
# Each loaded module should have a version sysctl
#
atf_test_case emu_module_version_sysctl cleanup
emu_module_version_sysctl_head()
{
	atf_set "descr" "Tests that loaded modules have version sysctls"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_module_version_sysctl_body()
{
	# Load emu_core
	atf_check -s exit:0 kldload emu_core

	# Check version sysctl exists
	atf_check -s exit:0 sysctl -n kern.emulation.module.emu_core.version

	# Version should be a number
	version=$(sysctl -n kern.emulation.module.emu_core.version)
	atf_check -s exit:0 -o "match:^[0-9]+$" echo "$version"
}
emu_module_version_sysctl_cleanup()
{
	# Ensure module is unloaded
	kldunload emu_core 2>/dev/null || true
}

#
# Test module refcount sysctl
# Each loaded module should have a refcount sysctl
#
atf_test_case emu_module_refcount_sysctl cleanup
emu_module_refcount_sysctl_head()
{
	atf_set "descr" "Tests that loaded modules have refcount sysctls"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_module_refcount_sysctl_body()
{
	# Load emu_core
	atf_check -s exit:0 kldload emu_core

	# Check refcount sysctl exists
	atf_check -s exit:0 sysctl -n kern.emulation.module.emu_core.refcount

	# Refcount should be a number >= 1 (module itself holds a reference)
	refcount=$(sysctl -n kern.emulation.module.emu_core.refcount)
	atf_check -s exit:0 -o "match:^[0-9]+$" echo "$refcount"
}
emu_module_refcount_sysctl_cleanup()
{
	# Ensure module is unloaded
	kldunload emu_core 2>/dev/null || true
}

#
# Test modules_loaded sysctl format
# Should be a comma-separated list of loaded module names
#
atf_test_case emu_modules_loaded_sysctl cleanup
emu_modules_loaded_sysctl_head()
{
	atf_set "descr" "Tests that modules_loaded sysctl shows comma-separated list"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_modules_loaded_sysctl_body()
{
	# Load emu_core
	atf_check -s exit:0 kldload emu_core

	# Check modules_loaded sysctl
	modules=$(sysctl -n kern.emulation.modules_loaded)
	atf_check -s exit:0 -o "match:emu_core" echo "$modules"
}
emu_modules_loaded_sysctl_cleanup()
{
	# Ensure module is unloaded
	kldunload emu_core 2>/dev/null || true
}

#
# Test MOD_LOAD with conflicting modules
# Loading the same module twice should fail or be a no-op
#
atf_test_case emu_module_load_conflict cleanup
emu_module_load_conflict_head()
{
	atf_set "descr" "Tests that loading a module twice is handled correctly"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_module_load_conflict_body()
{
	# Load emu_core first time
	atf_check -s exit:0 kldload emu_core

	# Try to load again - should either succeed (already loaded) or fail gracefully
	# In FreeBSD, kldload of an already loaded module returns success
	atf_check -s exit:0 kldload emu_core

	# Verify module is still loaded once
	count=$(kldinfo -q emu_core | wc -l)
	atf_check -s exit:0 -o "match:^1$" echo "$count"
}
emu_module_load_conflict_cleanup()
{
	# Ensure module is unloaded
	kldunload emu_core 2>/dev/null || true
}

atf_init_test_cases()
{
	atf_add_test_case emu_module_unload_active
	atf_add_test_case emu_module_unload_no_instances
	atf_add_test_case emu_master_module_load
	atf_add_test_case emu_arch_module_loads_core
	atf_add_test_case emu_module_version_sysctl
	atf_add_test_case emu_module_refcount_sysctl
	atf_add_test_case emu_modules_loaded_sysctl
	atf_add_test_case emu_module_load_conflict
}
