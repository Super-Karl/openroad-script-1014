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
             int max_backtracks)
{
  ord::OpenRoad::openRoad()->getDelGpu()->run(gui, verify, gpu, iterations,
                                           max_step, jump_offset_nm, frame_ms, report,
                                           max_backtracks);
}

}  // namespace delgpu

%}
