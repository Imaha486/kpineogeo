// Builds the SDK helper functions (kpi_CreateConfig etc.) from third_party/kpisdk/kpi.cpp.
// MinGW needs the __uuidof declarations from kpi_gcc_uuid.h in the same translation unit,
// so the SDK file is included here unmodified instead of being compiled directly.
#include <windows.h>
#include "kpi_gcc_uuid.h"
#include "kpi.cpp"
