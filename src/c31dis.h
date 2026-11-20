// C31 disassembler: formats decoded instructions for tests and debugging.
#pragma once
#include "c31_core.h"

// Format one decoded instruction (with pc for branch targets) into buf
// (at least 128 bytes). Returns buf.
char *c31_format(char *buf, const d_insn_t *o, uint32_t pc);
