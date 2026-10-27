#pragma once


#pragma warning(push)
#pragma warning(disable:4146)

// TLB di-generate (MSVC #import butuh msado15.dll yang tidak ada di CI/Linux);
// pakai header pre-generated dari lib/ (msado15.tlh/.tli).
#include <comdef.h>
#include "msado15.tlh"

#pragma warning(pop)
