// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

%{
#include "delgpu/DelGpu.h"
#include "ord/OpenRoad.hh"
%}

%include "Exception.i"

%inline %{

namespace delgpu {

void run_cmd(bool gui, bool verify, bool gpu, int iterations,
             double max_step, double jump_offset_nm, int frame_ms, const char* report,
             int max_backtracks, double learning_rate, double learning_rate_end, const char* learning_rate_mode,
             bool postprocess, int post_sweeps, int post_pair_rounds, int post_retract_sweeps, const char* post_retract_mode)
{
  ord::OpenRoad::openRoad()->getDelGpu()->run(gui, verify, gpu, iterations,
                                           max_step, jump_offset_nm, frame_ms, report,
                                           max_backtracks, learning_rate, learning_rate_end, learning_rate_mode,
                                           postprocess, post_sweeps, post_pair_rounds, post_retract_sweeps, post_retract_mode);
}

}  // namespace delgpu

%}
