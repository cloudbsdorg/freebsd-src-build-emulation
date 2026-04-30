#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
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
# HOWEVER CAURED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

# ATF shell test for emu configuration parsing

atf_test_case config_basic
config_basic_head() {
    atf_set "descr" "Test basic configuration file parsing"
}

config_basic_body() {
    # Create a test configuration file
    cat > /tmp/test_emu.conf << 'EOF'
# Test configuration
instance_dir=/var/emu
image_cache_dir=/var/cache/emu/images
default_arch=amd64
default_memory=512M
default_cpus=2
verbose=no
EOF

    # Verify configuration file exists and is readable
    atf_check -s exit:0 -o not-empty test -f /tmp/test_emu.conf

    # Parse and verify basic settings
    atf_check -s exit:0 -o not-empty grep -q 'instance_dir=/var/emu' /tmp/test_emu.conf
    atf_check -s exit:0 -o not-empty grep -q 'image_cache_dir=/var/cache/emu/images' /tmp/test_emu.conf
    atf_check -s exit:0 -o not-empty grep -q 'default_arch=amd64' /tmp/test_emu.conf
    atf_check -s exit:0 -o not-empty grep -q 'default_memory=512M' /tmp/test_emu.conf
    atf_check -s exit:0 -o not-empty grep -q 'default_cpus=2' /tmp/test_emu.conf
}

atf_test_case config_security
config_security_head() {
    atf_set "descr" "Test security configuration options"
}

config_security_body() {
    # Create a test configuration file with security options
    cat > /tmp/test_emu_sec.conf << 'EOF'
# Security configuration
securelevel=1
mac_enforce=true
veriexec_check=true
audit_enabled=true
audit_buffer_size=2048
memory_encryption=false
balloon_enabled=true
rctl_enabled=true
EOF

    # Verify security settings
    atf_check -s exit:0 -o not-empty grep -q 'securelevel=1' /tmp/test_emu_sec.conf
    atf_check -s exit:0 -o not-empty grep -q 'mac_enforce=true' /tmp/test_emu_sec.conf
    atf_check -s exit:0 -o not-empty grep -q 'veriexec_check=true' /tmp/test_emu_sec.conf
    atf_check -s exit:0 -o not-empty grep -q 'audit_enabled=true' /tmp/test_emu_sec.conf
    atf_check -s exit:0 -o not-empty grep -q 'audit_buffer_size=2048' /tmp/test_emu_sec.conf
    atf_check -s exit:0 -o not-empty grep -q 'rctl_enabled=true' /tmp/test_emu_sec.conf
}

atf_test_case config_resource_limits
config_resource_limits_head() {
    atf_set "descr" "Test resource limit configuration"
}

config_resource_limits_body() {
    # Create a test configuration file with resource limits
    cat > /tmp/test_emu_limits.conf << 'EOF'
# Resource limit configuration
max_instances=32
max_memory_mb=65536
max_vcpus=16
memory_warn_percent=85
oom_protection=true
rctl_enabled=true
EOF

    # Verify resource limit settings
    atf_check -s exit:0 -o not-empty grep -q 'max_instances=32' /tmp/test_emu_limits.conf
    atf_check -s exit:0 -o not-empty grep -q 'max_memory_mb=65536' /tmp/test_emu_limits.conf
    atf_check -s exit:0 -o not-empty grep -q 'max_vcpus=16' /tmp/test_emu_limits.conf
    atf_check -s exit:0 -o not-empty grep -q 'memory_warn_percent=85' /tmp/test_emu_limits.conf
}

atf_test_case config_network
config_network_head() {
    atf_set "descr" "Test network configuration options"
}

config_network_body() {
    # Create a test configuration file with network options
    cat > /tmp/test_emu_net.conf << 'EOF'
# Network configuration
network_isolation=true
network_nat=true
vmm_strict_priority=true
firewall_rules=/etc/emu/firewall.rules
EOF

    # Verify network settings
    atf_check -s exit:0 -o not-empty grep -q 'network_isolation=true' /tmp/test_emu_net.conf
    atf_check -s exit:0 -o not-empty grep -q 'network_nat=true' /tmp/test_emu_net.conf
    atf_check -s exit:0 -o not-empty grep -q 'vmm_strict_priority=true' /tmp/test_emu_net.conf
    atf_check -s exit:0 -o not-empty grep -q 'firewall_rules=/etc/emu/firewall.rules' /tmp/test_emu_net.conf
}

atf_test_case config_high_security
config_high_security_head() {
    atf_set "descr" "Test high security configuration"
}

config_high_security_body() {
    # Create a test configuration file with high security options
    cat > /tmp/test_emu_highsec.conf << 'EOF'
# High security configuration
securelevel=3
mac_enforce=true
veriexec_check=true
audit_enabled=true
audit_buffer_size=16384
rctl_enabled=true
network_isolation=true
huge_pages=true
memory_encryption=true
swap_encryption=true
cpu_affinity=0,1,2,3
balloon_enabled=false
EOF

    # Verify high security settings
    atf_check -s exit:0 -o not-empty grep -q 'securelevel=3' /tmp/test_emu_highsec.conf
    atf_check -s exit:0 -o not-empty grep -q 'mac_enforce=true' /tmp/test_emu_highsec.conf
    atf_check -s exit:0 -o not-empty grep -q 'veriexec_check=true' /tmp/test_emu_highsec.conf
    atf_check -s exit:0 -o not-empty grep -q 'audit_buffer_size=16384' /tmp/test_emu_highsec.conf
    atf_check -s exit:0 -o not-empty grep -q 'network_isolation=true' /tmp/test_emu_highsec.conf
    atf_check -s exit:0 -o not-empty grep -q 'huge_pages=true' /tmp/test_emu_highsec.conf
    atf_check -s exit:0 -o not-empty grep -q 'memory_encryption=true' /tmp/test_emu_highsec.conf
    atf_check -s exit:0 -o not-empty grep -q 'swap_encryption=true' /tmp/test_emu_highsec.conf
    atf_check -s exit:0 -o not-empty grep -q 'cpu_affinity=0,1,2,3' /tmp/test_emu_highsec.conf
}

atf_test_case config_comments
config_comments_head() {
    atf_set "descr" "Test configuration comment handling"
}

config_comments_body() {
    # Create a test configuration file with comments
    cat > /tmp/test_emu_comments.conf << 'EOF'
# This is a comment
instance_dir=/var/emu  # inline comment
# Another comment
default_arch=amd64
EOF

    # Verify comments are properly handled
    atf_check -s exit:1 -o empty grep -q '^instance_dir=#' /tmp/test_emu_comments.conf
    atf_check -s exit:0 -o not-empty grep -q '^# This is a comment' /tmp/test_emu_comments.conf
    atf_check -s exit:0 -o not-empty grep -q 'default_arch=amd64' /tmp/test_emu_comments.conf
}

atf_init_test_cases()
{
    atf_add_test_case config_basic
    atf_add_test_case config_security
    atf_add_test_case config_resource_limits
    atf_add_test_case config_network
    atf_add_test_case config_high_security
    atf_add_test_case config_comments
}
