execute_process(
    COMMAND ${EMULATOR} ${MANAGER} --root ${FIXTURES} setup show workstation --json
    OUTPUT_FILE ${WORK}/workstation.json
    RESULT_VARIABLE manager_result)
if(NOT manager_result EQUAL 0)
    message(FATAL_ERROR "imprint setup show exited with ${manager_result}")
endif()

execute_process(
    COMMAND ${EMULATOR} ${ENGINE} plan ${WORK}/workstation.json
    OUTPUT_VARIABLE plan
    RESULT_VARIABLE engine_result)
if(NOT engine_result EQUAL 0)
    message(FATAL_ERROR "imprint-engine plan exited with ${engine_result}")
endif()

string(FIND "${plan}" "2. packages (12): git, base-devel" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the engine's plan does not list the workstation's packages:\n${plan}")
endif()
