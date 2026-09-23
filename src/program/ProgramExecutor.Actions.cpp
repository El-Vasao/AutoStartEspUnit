// src/program/ProgramExecutor.Actions.cpp
#include "program/ProgramExecutor.h"

ActionId ProgramExecutor::compileAction(const char* action) {
    return actionIdFromString(action);
}
