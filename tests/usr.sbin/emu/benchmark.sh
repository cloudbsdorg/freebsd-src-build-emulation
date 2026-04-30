#!/usr/bin/env atf-sh
#
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

# Performance Benchmark Test (S8.6, S8.12)
#
# Benchmark emulation overhead and performance.
# Measures instruction execution rates, module load times,
# stack capture times, and memory usage.

. $(atf_get_srcdir)/utils.subr

# Benchmark result storage
BENCHMARK_RESULTS="${EMU_TEST_DIR}/benchmark_results.txt"

# Initialize benchmark results file
init_benchmark_results()
{
	emu_create_test_dir
	echo "# Emulation Framework Benchmark Results" > "${BENCHMARK_RESULTS}"
	echo "# Generated: $(date)" >> "${BENCHMARK_RESULTS}"
	echo "" >> "${BENCHMARK_RESULTS}"
}

# Record benchmark metric
record_metric()
{
	local name="$1"
	local value="$2"
	local unit="$3"
	local mode="$4"

	echo "${mode},${name},${value},${unit}" >> "${BENCHMARK_RESULTS}"
}

# Test 1: Instance creation time
atf_test_case instance_creation_time cleanup
instance_creation_time_head() {
	atf_set "descr" "Benchmark instance creation time"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
instance_creation_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Warm up
	emu_start_test_instance >/dev/null 2>&1
	emu_cleanup_test_instance

	# Measure creation time (average of 5 runs)
	local total_time=0
	local iterations=5

	for i in $(seq 1 ${iterations}); do
		local start_time=$(date +%s%N)

		local inst_id
		inst_id=$(emu_start_test_instance)

		local end_time=$(date +%s%N)
		local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

		total_time=$((total_time + elapsed))

		emu_cleanup_test_instance ${inst_id}
	done

	local avg_time=$((total_time / iterations))
	record_metric "instance_creation" ${avg_time} "ms" "emulator"

	atf_pass
}
instance_creation_time_cleanup() {
	:
}

# Test 2: Module load time
atf_test_case module_load_time cleanup
module_load_time_head() {
	atf_set "descr" "Benchmark module load time"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
module_load_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Warm up (load module once first)
	emu_load_module ${inst_id} test_module >/dev/null 2>&1
	emu_cleanup_modules ${inst_id}

	# Measure load time (average of 5 runs)
	local total_time=0
	local iterations=5

	for i in $(seq 1 ${iterations}); do
		local start_time=$(date +%s%N)

		emu_load_module ${inst_id} test_module >/dev/null 2>&1

		local end_time=$(date +%s%N)
		local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

		total_time=$((total_time + elapsed))

		emu_cleanup_modules ${inst_id}
	done

	local avg_time=$((total_time / iterations))
	record_metric "module_load" ${avg_time} "ms" "emulator"

	emu_cleanup_test_instance ${inst_id}
}
module_load_time_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 3: Stack capture time
atf_test_case stack_capture_time cleanup
stack_capture_time_head() {
	atf_set "descr" "Benchmark stack capture time"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
stack_capture_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Warm up
	emu stack capture ${inst_id} >/dev/null 2>&1

	# Measure capture time (average of 10 runs)
	local total_time=0
	local iterations=10

	for i in $(seq 1 ${iterations}); do
		local start_time=$(date +%s%N)

		emu stack capture ${inst_id} >/dev/null 2>&1

		local end_time=$(date +%s%N)
		local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

		total_time=$((total_time + elapsed))
	done

	local avg_time=$((total_time / iterations))
	record_metric "stack_capture" ${avg_time} "ms" "emulator"

	emu_cleanup_test_instance ${inst_id}
}
stack_capture_time_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 4: Memory usage measurement
atf_test_case memory_usage cleanup
memory_usage_head() {
	atf_set "descr" "Benchmark memory usage"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
memory_usage_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get memory usage
	local memory_kb
	memory_kb=$(sysctl -n "kern.emulation.instance.${inst_id}.memory" 2>/dev/null)

	if [ -z "${memory_kb}" ]; then
		atf_skip "Memory sysctl not available"
	fi

	# Convert to MB
	local memory_mb=$((memory_kb / 1024))
	record_metric "instance_memory" ${memory_mb} "MB" "emulator"

	emu_cleanup_test_instance ${inst_id}
}
memory_usage_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 5: CPU utilization
atf_test_case cpu_utilization cleanup
cpu_utilization_head() {
	atf_set "descr" "Benchmark CPU utilization"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
cpu_utilization_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get PID
	local pid
	pid=$(emu_get_pid ${inst_id})

	if [ -z "${pid}" ] || [ "${pid}" = "0" ]; then
		atf_skip "Could not get instance PID"
	fi

	# Measure CPU usage over 5 seconds
	local cpu_before
	cpu_before=$(ps -o %cpu= -p ${pid} 2>/dev/null || echo "0")

	sleep 5

	local cpu_after
	cpu_after=$(ps -o %cpu= -p ${pid} 2>/dev/null || echo "0")

	# Average CPU usage
	local cpu_avg=$(( (cpu_before + cpu_after) / 2 ))
	record_metric "cpu_usage" ${cpu_avg} "percent" "emulator"

	emu_cleanup_test_instance ${inst_id}
}
cpu_utilization_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 6: Instance list time
atf_test_case instance_list_time cleanup
instance_list_time_head() {
	atf_set "descr" "Benchmark instance list operation"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
instance_list_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Create some instances
	local inst1 inst2 inst3
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)
	inst3=$(emu_start_test_instance)

	# Warm up
	emu list >/dev/null 2>&1

	# Measure list time (average of 10 runs)
	local total_time=0
	local iterations=10

	for i in $(seq 1 ${iterations}); do
		local start_time=$(date +%s%N)

		emu list >/dev/null 2>&1

		local end_time=$(date +%s%N)
		local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

		total_time=$((total_time + elapsed))
	done

	local avg_time=$((total_time / iterations))
	record_metric "instance_list" ${avg_time} "ms" "emulator"

	emu_cleanup_test_instance ${inst1}
	emu_cleanup_test_instance ${inst2}
	emu_cleanup_test_instance ${inst3}
}
instance_list_time_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
	emu_cleanup_test_instance ${inst3:-}
}

# Test 7: Instance status time
atf_test_case instance_status_time cleanup
instance_status_time_head() {
	atf_set "descr" "Benchmark instance status operation"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
instance_status_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Warm up
	emu status ${inst_id} >/dev/null 2>&1

	# Measure status time (average of 10 runs)
	local total_time=0
	local iterations=10

	for i in $(seq 1 ${iterations}); do
		local start_time=$(date +%s%N)

		emu status ${inst_id} >/dev/null 2>&1

		local end_time=$(date +%s%N)
		local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

		total_time=$((total_time + elapsed))
	done

	local avg_time=$((total_time / iterations))
	record_metric "instance_status" ${avg_time} "ms" "emulator"

	emu_cleanup_test_instance ${inst_id}
}
instance_status_time_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 8: Instance destroy time
atf_test_case instance_destroy_time cleanup
instance_destroy_time_head() {
	atf_set "descr" "Benchmark instance destroy time"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
instance_destroy_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Measure destroy time (average of 5 runs)
	local total_time=0
	local iterations=5

	for i in $(seq 1 ${iterations}); do
		# Create instance
		local inst_id
		inst_id=$(emu_start_test_instance)

		# Wait a moment for instance to be fully started
		sleep 0.1

		local start_time=$(date +%s%N)

		emu destroy ${inst_id}

		local end_time=$(date +%s%N)
		local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

		total_time=$((total_time + elapsed))
	done

	local avg_time=$((total_time / iterations))
	record_metric "instance_destroy" ${avg_time} "ms" "emulator"
}
instance_destroy_time_cleanup() {
	:
}

# Test 9: OOM score adjustment time
atf_test_case oom_score_adjust_time cleanup
oom_score_adjust_time_head() {
	atf_set "descr" "Benchmark OOM score adjustment"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
oom_score_adjust_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get PID
	local pid
	pid=$(emu_get_pid ${inst_id})

	if [ -z "${pid}" ] || [ "${pid}" = "0" ]; then
		atf_skip "Could not get instance PID"
	fi

	# Warm up
	emu_set_oom_score ${pid} 100 >/dev/null 2>&1
	emu_reset_oom_score ${pid} >/dev/null 2>&1

	# Measure adjustment time (average of 10 runs)
	local total_time=0
	local iterations=10

	for i in $(seq 1 ${iterations}); do
		local start_time=$(date +%s%N)

		emu_set_oom_score ${pid} 100 >/dev/null 2>&1
		emu_reset_oom_score ${pid} >/dev/null 2>&1

		local end_time=$(date +%s%N)
		local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

		total_time=$((total_time + elapsed))
	done

	local avg_time=$((total_time / iterations))
	record_metric "oom_score_adjust" ${avg_time} "ms" "emulator"

	emu_cleanup_test_instance ${inst_id}
}
oom_score_adjust_time_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 10: Console log read time
atf_test_case console_log_read_time cleanup
console_log_read_time_head() {
	atf_set "descr" "Benchmark console log read time"
	atf_set "require.user" "root"
	atf_set "timeout" "60"
}
console_log_read_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Warm up
	emu console ${inst_id} >/dev/null 2>&1

	# Measure log read time (average of 10 runs)
	local total_time=0
	local iterations=10

	for i in $(seq 1 ${iterations}); do
		local start_time=$(date +%s%N)

		emu console ${inst_id} >/dev/null 2>&1

		local end_time=$(date +%s%N)
		local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

		total_time=$((total_time + elapsed))
	done

	local avg_time=$((total_time / iterations))
	record_metric "console_log_read" ${avg_time} "ms" "emulator"

	emu_cleanup_test_instance ${inst_id}
}
console_log_read_time_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 11: Multi-instance overhead
atf_test_case multi_instance_overhead cleanup
multi_instance_overhead_head() {
	atf_set "descr" "Benchmark overhead of multiple instances"
	atf_set "require.user" "root"
	atf_set "timeout" "120"
}
multi_instance_overhead_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Create instances and measure status time with different counts
	for count in 1 5 10; do
		# Create instances
		local inst_ids=""
		for i in $(seq 1 ${count}); do
			local inst_id
			inst_id=$(emu_start_test_instance)
			inst_ids="${inst_ids} ${inst_id}"
		done

		# Measure status time (average of 5 runs)
		local total_time=0
		local iterations=5
		local first_inst=$(echo ${inst_ids} | cut -d' ' -f1)

		for j in $(seq 1 ${iterations}); do
			local start_time=$(date +%s%N)

			emu status ${first_inst} >/dev/null 2>&1

			local end_time=$(date +%s%N)
			local elapsed=$(( (end_time - start_time) / 1000000 ))  # ms

			total_time=$((total_time + elapsed))
		done

		local avg_time=$((total_time / iterations))
		record_metric "status_with_${count}_instances" ${avg_time} "ms" "emulator"

		# Clean up
		for inst_id in ${inst_ids}; do
			emu_cleanup_test_instance ${inst_id}
		done
	done
}
multi_instance_overhead_cleanup() {
	:
}

# Test 12: Snapshot/restore time
atf_test_case snapshot_restore_time cleanup
snapshot_restore_time_head() {
	atf_set "descr" "Benchmark snapshot and restore time"
	atf_set "require.user" "root"
	atf_set "timeout" "120"
}
snapshot_restore_time_body() {
	atf_skip "Emulator binary not yet available for testing"

	init_benchmark_results

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	emu_create_test_dir
	local snapshot_file="${EMU_TEST_DIR}/snapshot_${inst_id}.bin"

	# Measure snapshot time
	local snapshot_start=$(date +%s%N)
	emu snapshot save ${inst_id} -o ${snapshot_file} >/dev/null 2>&1
	local snapshot_end=$(date +%s%N)
	local snapshot_time=$(( (snapshot_end - snapshot_start) / 1000000 ))  # ms

	record_metric "snapshot_save" ${snapshot_time} "ms" "emulator"

	# Measure restore time
	local restore_start=$(date +%s%N)
	emu snapshot restore ${inst_id} -i ${snapshot_file} >/dev/null 2>&1
	local restore_end=$(date +%s%N)
	local restore_time=$(( (restore_end - restore_start) / 1000000 ))  # ms

	record_metric "snapshot_restore" ${restore_time} "ms" "emulator"

	# Clean up
	rm -f ${snapshot_file}
	emu_cleanup_test_instance ${inst_id}
}
snapshot_restore_time_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Main test harness
atf_init_test_cases() {
	atf_add_test_case instance_creation_time
	atf_add_test_case module_load_time
	atf_add_test_case stack_capture_time
	atf_add_test_case memory_usage
	atf_add_test_case cpu_utilization
	atf_add_test_case instance_list_time
	atf_add_test_case instance_status_time
	atf_add_test_case instance_destroy_time
	atf_add_test_case oom_score_adjust_time
	atf_add_test_case console_log_read_time
	atf_add_test_case multi_instance_overhead
	atf_add_test_case snapshot_restore_time
}
