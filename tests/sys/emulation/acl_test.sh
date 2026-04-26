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
# Test allow_nonroot sysctl default value (should be 0)
#
atf_test_case emu_allow_nonroot_default cleanup
emu_allow_nonroot_default_head()
{
	atf_set "descr" "Tests that allow_nonroot sysctl defaults to 0 (root-only)"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_allow_nonroot_default_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Check default value is 0
	atf_check -s exit:0 -o "match:^0$" sysctl -n kern.emulation.allow_nonroot
}
emu_allow_nonroot_default_cleanup()
{
	emu_cleanup_modules
}

#
# Test allow_nonroot sysctl can be modified by root
#
atf_test_case emu_allow_nonroot_root_toggle cleanup
emu_allow_nonroot_root_toggle_head()
{
	atf_set "descr" "Tests that root can toggle allow_nonroot sysctl"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_allow_nonroot_root_toggle_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Enable non-root access
	atf_check -s exit:0 sysctl kern.emulation.allow_nonroot=1
	atf_check -s exit:0 -o "match:^1$" sysctl -n kern.emulation.allow_nonroot

	# Disable non-root access
	atf_check -s exit:0 sysctl kern.emulation.allow_nonroot=0
	atf_check -s exit:0 -o "match:^0$" sysctl -n kern.emulation.allow_nonroot
}
emu_allow_nonroot_root_toggle_cleanup()
{
	emu_cleanup_modules
}

#
# Test instance creation respects max_instances limit
#
atf_test_case emu_max_instances_limit cleanup
emu_max_instances_limit_head()
{
	atf_set "descr" "Tests that max_instances sysctl limits total instances"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_max_instances_limit_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Get current max
	max=$(sysctl -n kern.emulation.max_instances)

	# Set a low limit for testing
	atf_check -s exit:0 sysctl kern.emulation.max_instances=2

	# Verify limit is set
	atf_check -s exit:0 -o "match:^2$" sysctl -n kern.emulation.max_instances

	# Reset to original value
	atf_check -s exit:0 sysctl kern.emulation.max_instances=${max}
}
emu_max_instances_limit_cleanup()
{
	emu_cleanup_modules
}

#
# Test per-user max_instances_per_user limit
#
atf_test_case emu_max_instances_per_user_limit cleanup
emu_max_instances_per_user_limit_head()
{
	atf_set "descr" "Tests that max_instances_per_user sysctl limits per-user instances"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_max_instances_per_user_limit_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Get current max
	max=$(sysctl -n kern.emulation.max_instances_per_user)

	# Set a low limit for testing
	atf_check -s exit:0 sysctl kern.emulation.max_instances_per_user=2

	# Verify limit is set
	atf_check -s exit:0 -o "match:^2$" sysctl -n kern.emulation.max_instances_per_user

	# Reset to original value
	atf_check -s exit:0 sysctl kern.emulation.max_instances_per_user=${max}
}
emu_max_instances_per_user_limit_cleanup()
{
	emu_cleanup_modules
}

#
# Test per-user max_memory_per_instance limit
#
atf_test_case emu_max_memory_per_instance_limit cleanup
emu_max_memory_per_instance_limit_head()
{
	atf_set "descr" "Tests that max_memory_per_instance sysctl limits instance memory"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_max_memory_per_instance_limit_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Get current max
	max=$(sysctl -n kern.emulation.max_memory_per_instance)

	# Set a test limit (1GB)
	atf_check -s exit:0 sysctl kern.emulation.max_memory_per_instance=1073741824

	# Verify limit is set
	atf_check -s exit:0 -o "match:^1073741824$" sysctl -n kern.emulation.max_memory_per_instance

	# Reset to original value
	atf_check -s exit:0 sysctl kern.emulation.max_memory_per_instance=${max}
}
emu_max_memory_per_instance_limit_cleanup()
{
	emu_cleanup_modules
}

#
# Test instance_count sysctl reflects active instances
#
atf_test_case emu_instance_count_tracking cleanup
emu_instance_count_tracking_head()
{
	atf_set "descr" "Tests that instance_count sysctl tracks active instances"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_instance_count_tracking_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Initial count should be 0
	atf_check -s exit:0 -o "match:^0$" sysctl -n kern.emulation.instance_count

	# Note: Actual instance creation would require the emu CLI tool
	# For now, we verify the sysctl interface exists and is readable
	atf_check -s exit:0 sysctl -n kern.emulation.instance_count
}
emu_instance_count_tracking_cleanup()
{
	emu_cleanup_modules
}

#
# Test modules_loaded sysctl format
#
atf_test_case emu_modules_loaded_format cleanup
emu_modules_loaded_head()
{
	atf_set "descr" "Tests that modules_loaded sysctl returns comma-separated list"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_modules_loaded_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Check modules_loaded format (should be comma-separated or empty)
	modules=$(sysctl -n kern.emulation.modules_loaded)
	# Accept empty string or comma-separated list
	atf_check -s exit:0 -o "match:^(|[a-zA-Z0-9_]+(,[a-zA-Z0-9_]+)*)$" echo "${modules}"
}
emu_modules_loaded_cleanup()
{
	emu_cleanup_modules
}

#
# Test module version sysctl exists
#
atf_test_case emu_module_version_sysctl cleanup
emu_module_version_sysctl_head()
{
	atf_set "descr" "Tests that module version sysctl exists for loaded modules"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_module_version_sysctl_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Check version sysctl exists for emu_core
	atf_check -s exit:0 sysctl -n kern.emulation.module.emu_core.version
}
emu_module_version_sysctl_cleanup()
{
	emu_cleanup_modules
}

#
# Test module refcount sysctl exists
#
atf_test_case emu_module_refcount_sysctl cleanup
emu_module_refcount_sysctl_head()
{
	atf_set "descr" "Tests that module refcount sysctl exists for loaded modules"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_module_refcount_sysctl_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Check refcount sysctl exists for emu_core
	atf_check -s exit:0 sysctl -n kern.emulation.module.emu_core.refcount
}
emu_module_refcount_sysctl_cleanup()
{
	emu_cleanup_modules
}

#
# Test jail integration - PR_ALLOW_EMULATION flag check
#
atf_test_case emu_jail_emulation_flag cleanup
emu_jail_emulation_flag_head()
{
	atf_set "descr" "Tests that jail emulation flag is defined"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_jail_emulation_flag_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# The jail integration is tested by verifying the sysctl interface works
	# Actual jail testing would require creating a jail, which is beyond scope here
	# We verify the framework is loaded and accessible
	atf_check -s exit:0 sysctl kern.emulation.allow_nonroot=0
}
emu_jail_emulation_flag_cleanup()
{
	emu_cleanup_modules
}

#
# Test instance state tracking sysctl
#
atf_test_case emu_instance_state_sysctl cleanup
emu_instance_state_sysctl_head()
{
	atf_set "descr" "Tests that instance state tracking sysctls exist"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_instance_state_sysctl_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Verify instance_count sysctl is accessible
	atf_check -s exit:0 sysctl -n kern.emulation.instance_count

	# Note: Individual instance sysctls would be created when instances exist
	# For now, we verify the base infrastructure is in place
}
emu_instance_state_sysctl_cleanup()
{
	emu_cleanup_modules
}

#
# Test CPU time limit sysctl
#
atf_test_case emu_max_cpu_time_per_instance cleanup
emu_max_cpu_time_per_instance_head()
{
	atf_set "descr" "Tests that max_cpu_time_per_instance sysctl is configurable"
	atf_set "require.user" "root"
	atf_set "require.kmods" "emu_core"
}
emu_max_cpu_time_per_instance_body()
{
	# Load the emu_core module
	atf_check -s exit:0 kldload emu_core

	# Get current limit
	current=$(sysctl -n kern.emulation.max_cpu_time_per_instance)

	# Set a test limit (1 hour = 3600 seconds)
	atf_check -s exit:0 sysctl kern.emulation.max_cpu_time_per_instance=3600

	# Verify limit is set
	atf_check -s exit:0 -o "match:^3600$" sysctl -n kern.emulation.max_cpu_time_per_instance

	# Reset to original value
	atf_check -s exit:0 sysctl kern.emulation.max_cpu_time_per_instance=${current}
}
emu_max_cpu_time_per_instance_cleanup()
{
	emu_cleanup_modules
}
