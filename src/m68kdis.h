// 68K disassembler: formats decoded instructions for tests and debugging.
#pragma once
#include "m68k_core.h"

// Format one decoded instruction (with pc for branch targets) into buf
// (at least 96 bytes). Returns buf.
char *m68k_format(char *buf, const m_insn_t *o, uint32_t pc);
