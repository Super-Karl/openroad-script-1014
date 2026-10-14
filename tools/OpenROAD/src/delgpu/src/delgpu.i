// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

%{
#include "delgpu/DelGpu.h"
#include "ord/OpenRoad.hh"
%}

%include "Exception.i"

%inline %{

namespace delgpu {

void run_cmd()
{
  ord::OpenRoad::openRoad()->getDelGpu()->run();
}

}  // namespace delgpu

%}
