#ifndef VOLAND_CPU_A64_DECODER_H
#define VOLAND_CPU_A64_DECODER_H

#include <stdbool.h>
#include <stdint.h>
#include "cpu/a64/insn.h"

bool a64_decode(uint32_t encoding, A64_Insn *out);

#endif
