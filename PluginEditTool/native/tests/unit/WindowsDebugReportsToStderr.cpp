#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdio>
#include <initializer_list>
#include <stdlib.h>
#include <windows.h>

namespace {
int exitOnReport(int type, char* message, int*) {
    if (type == _CRT_WARN) return FALSE;
    std::fputs(message ? message : "CRT report without a message\n", stderr);
    std::fflush(stderr);
    _exit(3);
}
[[maybe_unused]] const bool reportsGoToStderr = [] {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, exitOnReport);
    _set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    return true;
}();
}
#endif
