# cmake -DDUMPBIN=<dumpbin.exe> -DFILE=<pe file> -P check_x86.cmake
# Fails the build unless FILE is a 32-bit (x86, machine 14C) PE: ztmnt.exe is
# a 32-bit process and cannot load anything else.
if(NOT EXISTS "${FILE}")
  message(FATAL_ERROR "check_x86: ${FILE} does not exist")
endif()
execute_process(COMMAND "${DUMPBIN}" /nologo /headers "${FILE}" OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "check_x86: dumpbin failed on ${FILE}")
endif()
if(NOT out MATCHES "14C machine \\(x86\\)")
  message(FATAL_ERROR "check_x86: ${FILE} is not x86 (need machine 14C)")
endif()
message(STATUS "check_x86: ${FILE}: 14C machine (x86)")
