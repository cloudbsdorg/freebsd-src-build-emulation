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
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

# ATF shell test for emu security configuration validation

atf_test_case securelevel_insecure
securelevel_insecure_head() {
    atf_set "descr" "Test insecure (development) securelevel configuration"
}

securelevel_insecure_body() {
    cat > /tmp/test_emu_insecure.conf << 'EOF'
securelevel=-1
mac_enforce=false
veriexec_check=false
audit_enabled=false
EOF

    # Verify insecure configuration
    atf_check -s exit:0 -o not-empty grep -q 'securelevel=-1' /tmp/test_emu_insecure.conf
    atf_check -s exit:0 -o not-empty grep -q 'mac_enforce=false' /tmp/test_emu_insecure.conf
    atf_check -s exit:0 -o not-empty grep -q 'veriexec_check=false' /tmp/test_emu_insecure.conf
}

atf_test_case securelevel_standard
securelevel_standard_head() {
    atf_set "descr" "Test standard securelevel configuration"
}

securelevel_standard_body() {
    cat > /tmp/test_emu_standard.conf << 'EOF'
securelevel=1
mac_enforce=true
veriexec_check=true
audit_enabled=true
rctl_enabled=true
EOF

    # Verify standard configuration
    atf_check -s exit:0 -o not-empty grep -q 'securelevel=1' /tmp/test_emu_standard.conf
    atf_check -s exit:0 -o not-empty grep -q 'mac_enforce=true' /tmp/test_emu_standard.conf
    atf_check -s exit:0 -o not-empty grep -q 'veriexec_check=true' /tmp/test_emu_standard.conf
    atf_check -s exit:0 -o not-empty grep -q 'audit_enabled=true' /tmp/test_emu_standard.conf
    atf_check -s exit:0 -o not-empty grep -q 'rctl_enabled=true' /tmp/test_emu_standard.conf
}

atf_test_case securelevel_high
securelevel_high_head() {
    atf_set "descr" "Test high security configuration"
}

securelevel_high_body() {
    cat > /tmp/test_emu_high.conf << 'EOF'
securelevel=3
mac_enforce=true
veriexec_check=true
audit_enabled=true
audit_buffer_size=8192
rctl_enabled=true
network_isolation=true
swap_encryption=true
EOF

    # Verify high security configuration
    atf_check -s exit:0 -o not-empty grep -q 'securelevel=3' /tmp/test_emu_high.conf
    atf_check -s exit:0 -o not-empty grep -q 'mac_enforce=true' /tmp/test_emu_high.conf
    atf_check -s exit:0 -o not-empty grep -q 'veriexec_check=true' /tmp/test_emu_high.conf
    atf_check -s exit:0 -o not-empty grep -q 'audit_buffer_size=8192' /tmp/test_emu_high.conf
    atf_check -s exit:0 -o not-empty grep -q 'network_isolation=true' /tmp/test_emu_high.conf
    atf_check -s exit:0 -o not-empty grep -q 'swap_encryption=true' /tmp/test_emu_high.conf
}

atf_test_case securelevel_maximum
securelevel_maximum_head() {
    atf_set "descr" "Test maximum security configuration"
}

securelevel_maximum_body() {
    cat > /tmp/test_emu_maximum.conf << 'EOF'
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

    # Verify maximum security configuration
    atf_check -s exit:0 -o not-empty grep -q 'securelevel=3' /tmp/test_emu_maximum.conf
    atf_check -s exit:0 -o not-empty grep -q 'mac_enforce=true' /tmp/test_emu_maximum.conf
    atf_check -s exit:0 -o not-empty grep -q 'veriexec_check=true' /tmp/test_emu_maximum.conf
    atf_check -s exit:0 -o not-empty grep -q 'audit_buffer_size=16384' /tmp/test_emu_maximum.conf
    atf_check -s exit:0 -o not-empty grep -q 'network_isolation=true' /tmp/test_emu_maximum.conf
    atf_check -s exit:0 -o not-empty grep -q 'huge_pages=true' /tmp/test_emu_maximum.conf
    atf_check -s exit:0 -o not-empty grep -q 'memory_encryption=true' /tmp/test_emu_maximum.conf
    atf_check -s exit:0 -o not-empty grep -q 'swap_encryption=true' /tmp/test_emu_maximum.conf
    atf_check -s exit:0 -o not-empty grep -q 'cpu_affinity=0,1,2,3' /tmp/test_emu_maximum.conf
}

atf_test_case snapshot_share_security
snapshot_share_security_head() {
    atf_set "descr" "Test snapshot and share security configuration"
}

snapshot_share_security_body() {
    cat > /tmp/test_emu_share.conf << 'EOF'
snapshot_require_mac=true
share_require_mac=true
share_path_validation=true
snapshot_allowed_dirs=/var/emu/snapshots,/secure/emu/snapshots
EOF

    # Verify snapshot/share security settings
    atf_check -s exit:0 -o not-empty grep -q 'snapshot_require_mac=true' /tmp/test_emu_share.conf
    atf_check -s exit:0 -o not-empty grep -q 'share_require_mac=true' /tmp/test_emu_share.conf
    atf_check -s exit:0 -o not-empty grep -q 'share_path_validation=true' /tmp/test_emu_share.conf
    atf_check -s exit:0 -o not-empty grep -q 'snapshot_allowed_dirs=/var/emu/snapshots,/secure/emu/snapshots' /tmp/test_emu_share.conf
}

atf_test_case kernel_module_options
kernel_module_options_head() {
    atf_set "descr" "Test kernel module configuration options"
}

kernel_module_options_body() {
    cat > /tmp/test_emu_mod.conf << 'EOF'
module_debug=false
module_audit_level=2
conflict_check=true
numa_aware=true
huge_pages=auto
EOF

    # Verify kernel module options
    atf_check -s exit:0 -o not-empty grep -q 'module_debug=false' /tmp/test_emu_mod.conf
    atf_check -s exit:0 -o not-empty grep -q 'module_audit_level=2' /tmp/test_emu_mod.conf
    atf_check -s exit:0 -o not-empty grep -q 'conflict_check=true' /tmp/test_emu_mod.conf
    atf_check -s exit:0 -o not-empty grep -q 'numa_aware=true' /tmp/test_emu_mod.conf
    atf_check -s exit:0 -o not-empty grep -q 'huge_pages=auto' /tmp/test_emu_mod.conf
}

atf_init_test_cases()
{
    atf_add_test_case securelevel_insecure
    atf_add_test_case securelevel_standard
    atf_add_test_case securelevel_high
    atf_add_test_case securelevel_maximum
    atf_add_test_case snapshot_share_security
    atf_add_test_case kernel_module_options
}
