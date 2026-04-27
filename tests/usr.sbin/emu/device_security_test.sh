#!/usr/bin/env atf-sh

#
# Device Security Tests for Emulation Framework
#
# These tests verify the security of device emulation components:
# - MMIO bounds checking (S5.1)
# - Device state corruption prevention (S5.2-S5.6)
# - Network isolation (S4.4-S4.6)
#

atf_test_case mmio_bounds_check ok
mmio_bounds_check_head()
{
	atf_set "descr" "Verify MMIO accesses are bounds-checked"
	atf_set "require.user" "root"
}
mmio_bounds_check_body()
{
	# Test 1: MMIO region registration with overlap detection
	atf_check -s exit:0 -o match:"MMIO region registered" \
		emu mmio-test --register --base 0xf0000000 --size 0x1000 --name test_region

	# Test 2: Access within bounds should succeed
	atf_check -s exit:0 -o match:"Read succeeded" \
		emu mmio-test --read --addr 0xf0000100 --size 4

	# Test 3: Access outside bounds should fail gracefully
	atf_check -s exit:0 -o match:"Access out of bounds" \
		emu mmio-test --read --addr 0xf0001000 --size 4

	# Test 4: Unaligned access detection
	atf_check -s exit:0 -o match:"Alignment error" \
		emu mmio-test --read --addr 0xf0000101 --size 4

	# Test 5: Write to read-only region should fail
	atf_check -s exit:0 -o match:"Permission denied" \
		emu mmio-test --write --addr 0xf0000100 --size 4 --value 0x12345678

	# Test 6: Integer overflow in offset calculation
	atf_check -s exit:0 -o match:"Overflow detected" \
		emu mmio-test --read --addr 0xffffffff --size 8

	# Cleanup
	atf_check -s exit:0 emu mmio-test --unregister --name test_region
}

atf_test_case uart_console_only ok
uart_console_only_head()
{
	atf_set "descr" "Verify UART device only outputs to console"
	atf_set "require.user" "root"
}
uart_console_only_body()
{
	# Test 1: UART initialization
	atf_check -s exit:0 emu dev-test --init uart

	# Test 2: UART output should go to console buffer, not host files
	atf_check -s exit:0 -o match:"Console output" \
		emu dev-test --uart-transmit --char 'A'

	# Test 3: Attempt to access host filesystem via UART should fail
	atf_check -s exit:0 -o match:"Host file access denied" \
		emu dev-test --uart-write-file --path /etc/passwd

	# Test 4: UART FIFO bounds checking
	atf_check -s exit:0 -o match:"FIFO overflow prevented" \
		emu dev-test --uart-fifo-fill --count 1000

	# Cleanup
	atf_check -s exit:0 emu dev-test --destroy uart
}

atf_test_case virtio_blk_path_validation ok
virtio_blk_path_validation_head()
{
	atf_set "descr" "Verify virtio-blk path validation blocks dangerous paths"
	atf_set "require.user" "root"
}
virtio_blk_path_validation_body()
{
	# Test 1: Valid path should work
	atf_check -s exit:0 emu dev-test --blk-init --path /tmp/test.img

	# Test 2: /dev paths should be blocked
	atf_check -s exit:0 -o match:"Path validation failed" \
		emu dev-test --blk-init --path /dev/sda

	# Test 3: /proc paths should be blocked
	atf_check -s exit:0 -o match:"Path validation failed" \
		emu dev-test --blk-init --path /proc/self/mem

	# Test 4: /sys paths should be blocked
	atf_check -s exit:0 -o match:"Path validation failed" \
		emu dev-test --blk-init --path /sys/block/sda

	# Test 5: Symlink escape should be detected
	ln -sf /etc/passwd /tmp/passwd_link
	atf_check -s exit:0 -o match:"Symlink escape detected" \
		emu dev-test --blk-init --path /tmp/passwd_link
	rm -f /tmp/passwd_link

	# Test 6: Non-regular file should be rejected
	atf_check -s exit:0 -o match:"Not a regular file" \
		emu dev-test --blk-init --path /tmp

	# Test 7: Bounds checking on I/O operations
	atf_check -s exit:0 -o match:"I/O out of bounds" \
		emu dev-test --blk-read --offset 999999999 --size 512

	# Cleanup
	atf_check -s exit:0 emu dev-test --blk-close --path /tmp/test.img
}

atf_test_case virtio_net_isolation ok
virtio_net_isolation_head()
{
	atf_set "descr" "Verify network isolation in host-only mode"
	atf_set "require.user" "root"
}
virtio_net_isolation_body()
{
	# Test 1: Initialize network device in host-only mode
	atf_check -s exit:0 emu dev-test --net-init --mode hostonly

	# Test 2: MAC validation - invalid MAC should be rejected
	atf_check -s exit:0 -o match:"Invalid MAC address" \
		emu dev-test --net-set-mac --mac "00:00:00:00:00:00"

	# Test 3: MAC validation - multicast MAC should be rejected for guest
	atf_check -s exit:0 -o match:"Multicast MAC not allowed" \
		emu dev-test --net-set-mac --mac "01:00:00:00:00:00"

	# Test 4: Valid MAC should work
	atf_check -s exit:0 emu dev-test --net-set-mac --mac "02:00:00:00:00:01"

	# Test 5: Packet size validation - too small
	atf_check -s exit:0 -o match:"Packet too small" \
		emu dev-test --net-transmit --size 10

	# Test 6: Packet size validation - too large
	atf_check -s exit:0 -o match:"Packet too large" \
		emu dev-test --net-transmit --size 10000

	# Test 7: Host-only mode should not reach external network
	atf_check -s exit:0 -o match:"External network unreachable" \
		emu dev-test --net-ping --target 8.8.8.8

	# Test 8: Rate limiting should prevent flooding
	atf_check -s exit:0 -o match:"Rate limit exceeded" \
		emu dev-test --net-flood --count 10000

	# Cleanup
	atf_check -s exit:0 emu dev-test --net-destroy
}

atf_test_case gdb_localhost_only ok
gdb_localhost_only_head()
{
	atf_set "descr" "Verify GDB stub binds to localhost only"
	atf_set "require.user" "root"
}
gdb_localhost_only_body()
{
	# Test 1: GDB stub should bind to 127.0.0.1 only
	atf_check -s exit:0 emu dev-test --gdb-init --port 1234

	# Test 2: Verify binding address
	atf_check -s exit:0 -o match:"127.0.0.1:1234" \
		emu dev-test --gdb-info

	# Test 3: Connection from localhost should succeed
	atf_check -s exit:0 emu dev-test --gdb-connect --from 127.0.0.1

	# Test 4: Connection from external address should fail
	atf_check -s exit:0 -o match:"Connection refused" \
		emu dev-test --gdb-connect --from 192.168.1.100

	# Test 5: Bounds-checked memory access via GDB
	atf_check -s exit:0 -o match:"Memory access out of bounds" \
		emu dev-test --gdb-read-mem --addr 0xffffffffffffffff --size 8

	# Cleanup
	atf_check -s exit:0 emu dev-test --gdb-destroy
}

atf_test_case device_state_corruption ok
device_state_corruption_head()
{
	atf_set "descr" "Verify device state cannot be corrupted by guest"
	atf_set "require.user" "root"
}
device_state_corruption_body()
{
	# Test 1: Initialize multiple devices
	atf_check -s exit:0 emu dev-test --init-all

	# Test 2: Attempt to corrupt UART state via invalid register access
	atf_check -s exit:0 -o match:"Invalid register" \
		emu dev-test --uart-write-reg --reg 999 --value 0x1234

	# Test 3: Attempt to corrupt network state via invalid ioctl
	atf_check -s exit:0 -o match:"Invalid ioctl" \
		emu dev-test --net-ioctl --cmd 0xDEADBEEF

	# Test 4: Attempt to corrupt block device via invalid request
	atf_check -s exit:0 -o match:"Invalid virtio request" \
		emu dev-test --blk-invalid-request

	# Test 5: Verify device state is intact after attacks
	atf_check -s exit:0 -o match:"Device state valid" \
		emu dev-test --verify-state

	# Test 6: MMIO write to read-only register should fail
	atf_check -s exit:0 -o match:"Read-only register" \
		emu dev-test --mmio-write-ro --addr 0x100

	# Test 7: Rapid reset should not corrupt state
	atf_check -s exit:0 emu dev-test --reset-stress --count 100

	# Cleanup
	atf_check -s exit:0 emu dev-test --destroy-all
}

atf_test_case network_mac_filtering ok
network_mac_filtering_head()
{
	atf_set "descr" "Verify MAC filtering works correctly"
	atf_set "require.user" "root"
}
network_mac_filtering_body()
{
	# Test 1: Initialize network with MAC filtering
	atf_check -s exit:0 emu dev-test --net-init --mode hostonly --mac-filter

	# Test 2: Add allowed MAC addresses
	atf_check -s exit:0 emu dev-test --net-add-mac --mac "02:00:00:00:00:01"
	atf_check -s exit:0 emu dev-test --net-add-mac --mac "02:00:00:00:00:02"

	# Test 3: Packet from allowed MAC should pass
	atf_check -s exit:0 -o match:"Packet accepted" \
		emu dev-test --net-receive --from "02:00:00:00:00:01"

	# Test 4: Packet from unknown MAC should be dropped
	atf_check -s exit:0 -o match:"Packet dropped" \
		emu dev-test --net-receive --from "02:00:00:00:00:03"

	# Test 5: Promiscuous mode should be disabled by default
	atf_check -s exit:0 -o match:"Promiscuous mode disabled" \
		emu dev-test --net-check-promisc

	# Test 6: Enable promiscuous mode (requires privilege)
	atf_check -s exit:0 emu dev-test --net-set-promisc --enable

	# Test 7: In promiscuous mode, all packets should be accepted
	atf_check -s exit:0 -o match:"Packet accepted (promisc)" \
		emu dev-test --net-receive --from "02:00:00:00:00:03"

	# Cleanup
	atf_check -s exit:0 emu dev-test --net-destroy
}

atf_test_case device_cleanup_on_destroy ok
device_cleanup_on_destroy_head()
{
	atf_set "descr" "Verify all device resources are cleaned up on destroy"
	atf_set "require.user" "root"
}
device_cleanup_on_destroy_body()
{
	# Test 1: Initialize devices
	atf_check -s exit:0 emu dev-test --init-all

	# Test 2: Open files and allocate resources
	atf_check -s exit:0 emu dev-test --blk-init --path /tmp/test.img
	atf_check -s exit:0 emu dev-test --net-init --mode hostonly
	atf_check -s exit:0 emu dev-test --uart-init

	# Test 3: Destroy all devices
	atf_check -s exit:0 emu dev-test --destroy-all

	# Test 4: Verify no file descriptors leaked
	atf_check -s exit:0 -o match:"No leaked FDs" \
		emu dev-test --check-fds

	# Test 5: Verify no memory leaked
	atf_check -s exit:0 -o match:"No memory leaks" \
		emu dev-test --check-memory

	# Test 6: Verify no kernel resources leaked
	atf_check -s exit:0 -o match:"No kernel resource leaks" \
		emu dev-test --check-kernel-resources
}
