/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cmdline.h"
#include "nsi_perfetto_trace.h"
#include "nsi_tasks.h"

static void cmd_input_trace(char *argv, int offset)
{
	nsi_perfetto_trace_set_path(argv + offset);
}

static void register_input_trace_args(void)
{
	static struct args_struct_t input_trace_args[] = {
		{
			.option = "input-trace",
			.name = "file",
			.type = 's',
			.call_when_found = cmd_input_trace,
			.descript = "Perfetto trace file to replay input values."
		},
		ARG_TABLE_ENDMARKER
	};

	native_add_command_line_opts(input_trace_args);
}

NSI_TASK(register_input_trace_args, PRE_BOOT_1, 1);
