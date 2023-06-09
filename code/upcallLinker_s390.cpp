/*
 * Copyright (c) 2020, Red Hat, Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "precompiled.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "logging/logStream.hpp"
#include "memory/resourceArea.hpp"
#include "prims/upcallLinker.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/signature.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/formatBuffer.hpp"
#include "utilities/globalDefinitions.hpp"

#define __ _masm->

// for callee saved regs, according to the caller's ABI
static int compute_reg_save_area_size(const ABIDescriptor& abi) {
  int size = 0;
  for (int i = 0; i < Register::number_of_registers; i++) {
    Register reg = as_Register(i);
    // R1 saved/restored by prologue/epilogue, R13 (system thread) won't get modified!
    if (reg == Z_SP) continue;
    if (!abi.is_volatile_reg(reg)) {
      size += 8; // bytes
    }
  }

  for (int i = 0; i < FloatRegister::number_of_registers; i++) {
    FloatRegister reg = as_FloatRegister(i);
    if (!abi.is_volatile_reg(reg)) {
      size += 8; // bytes
    }
  }

  return size;
}

static void preserve_callee_saved_registers(MacroAssembler* _masm, const ABIDescriptor& abi, int reg_save_area_offset) {
  // 1. iterate all registers in the architecture
  //     - check if they are volatile or not for the given abi
  //     - if NOT, we need to save it here

  int offset = reg_save_area_offset;

  __ block_comment("{ preserve_callee_saved_regs ");
  for (int i = 0; i < Register::number_of_registers; i++) {
    Register reg = as_Register(i);
    // R1 saved/restored by prologue/epilogue, R13 (system thread) won't get modified!
    if (reg == Z_SP) continue;
    if (!abi.is_volatile_reg(reg)) {
      __ z_stg(reg, Address(Z_SP, offset));
      offset += 8;
    }
  }

  for (int i = 0; i < FloatRegister::number_of_registers; i++) {
    FloatRegister reg = as_FloatRegister(i);
    if (!abi.is_volatile_reg(reg)) {
      __ z_std(reg, Address(Z_SP, offset));
      offset += 8;
    }
  }

  __ block_comment("} preserve_callee_saved_regs ");
}

static void restore_callee_saved_registers(MacroAssembler* _masm, const ABIDescriptor& abi, int reg_save_area_offset) {
  // 1. iterate all registers in the architecture
  //     - check if they are volatile or not for the given abi
  //     - if NOT, we need to restore it here

  int offset = reg_save_area_offset;

  __ block_comment("{ restore_callee_saved_regs ");
  for (int i = 0; i < Register::number_of_registers; i++) {
    Register reg = as_Register(i);
    // R1 saved/restored by prologue/epilogue, R13 (system thread) won't get modified!
    if (reg == Z_SP) continue;
    if (!abi.is_volatile_reg(reg)) {
      __ z_lg(reg, Address(Z_SP, offset));
      offset += 8;
    }
  }

  for (int i = 0; i < FloatRegister::number_of_registers; i++) {
    FloatRegister reg = as_FloatRegister(i);
    if (!abi.is_volatile_reg(reg)) {
      __ z_ld(reg, Address(Z_SP, offset));
      offset += 8;
    }
  }

  __ block_comment("} restore_callee_saved_regs ");
}

static const int upcall_stub_code_base_size = 1536; // depends on GC (resolve_jobject)
static const int upcall_stub_size_per_arg = 16; // arg save & restore + move
address UpcallLinker::make_upcall_stub(jobject receiver, Method* entry,
                                       BasicType* in_sig_bt, int total_in_args,
                                       BasicType* out_sig_bt, int total_out_args,
                                       BasicType ret_type,
                                       jobject jabi, jobject jconv,
                                       bool needs_return_buffer, int ret_buf_size) {
  ResourceMark rm;
  const ABIDescriptor abi = ForeignGlobals::parse_abi_descriptor(jabi);
  const CallRegs call_regs = ForeignGlobals::parse_call_regs(jconv);
  int code_size = upcall_stub_code_base_size + (total_in_args * upcall_stub_size_per_arg);
  CodeBuffer buffer("upcall_stub", code_size, /* locs_size = */ 1);

  Register call_target_address = Z_R1_scratch,
           callerSP = Z_tmp_1,
           tmp = Z_R0_scratch;

  VMStorage shuffle_reg = abi._scratch1;
  JavaCallingConvention out_conv;
  NativeCallingConvention in_conv(call_regs._arg_regs);
  ArgumentShuffle arg_shuffle(in_sig_bt, total_in_args, out_sig_bt, total_out_args, &in_conv, &out_conv, shuffle_reg);
  int preserved_bytes = SharedRuntime::out_preserve_stack_slots() * VMRegImpl::stack_slot_size;
  int stack_bytes = preserved_bytes + arg_shuffle.out_arg_bytes();
  int out_arg_area = align_up(stack_bytes, 8);

#ifndef PRODUCT
  LogTarget(Trace, foreign, upcall) lt;
  if (lt.is_enabled()) {
    ResourceMark rm;
    LogStream ls(lt);
    arg_shuffle.print_on(&ls);
  }
#endif

 // out_arg_area (for stack arguments) doubles as shadow space for native calls.
  // make sure it is big enough.
  if (out_arg_area < frame::z_common_abi_size) {
    out_arg_area = frame::z_abi_160_size;
  }

  int reg_save_area_size = compute_reg_save_area_size(abi);
  RegSpiller arg_spiller(call_regs._arg_regs);
  RegSpiller result_spiller(call_regs._ret_regs);

  int shuffle_area_offset   = 0;
  int res_save_area_offset  = shuffle_area_offset   + out_arg_area;
  int arg_save_area_offset  = res_save_area_offset  + result_spiller.spill_size_bytes();
  int reg_save_area_offset  = arg_save_area_offset  + arg_spiller.spill_size_bytes();
  int frame_data_offset     = reg_save_area_offset  + reg_save_area_size;
  int frame_bottom_offset   = frame_data_offset     + sizeof(UpcallStub::FrameData);

  StubLocations locs;
  int ret_buf_offset = -1;
  if (needs_return_buffer) {
    assert(0, "ShouldNotReachHere  needs_return_buffer not required on s390x");
    ret_buf_offset = frame_bottom_offset;
    frame_bottom_offset += ret_buf_size;
    // use a free register for shuffling code to pick up return
    // buffer address from
    locs.set(StubLocations::RETURN_BUFFER, abi._scratch2);
  }

  int frame_size = align_up(frame_bottom_offset, 8);
 

  // The space we have allocated will look like:
  //
  //
  // FP-> |                     |
  //      |---------------------| = frame_bottom_offset = frame_size
  //      | (optional)          |
  //      | ret_buf             |
  //      |---------------------| = ret_buf_offset
  //      |                     |
  //      | FrameData           |
  //      |---------------------| = frame_data_offset
  //      |                     |
  //      | reg_save_area       |
  //      |---------------------| = reg_save_are_offset
  //      |                     |
  //      | arg_save_area       |
  //      |---------------------| = arg_save_are_offset
  //      |                     |
  //      | res_save_area       |
  //      |---------------------| = res_save_are_offset
  //      |                     |
  // SP-> | out_arg_area        |   needs to be at end for shadow space
  //
  //

  //////////////////////////////////////////////////////////////////////////////

  MacroAssembler* _masm = new MacroAssembler(&buffer);
  address start = __ pc();

  __ save_return_pc();
  assert((abi._stack_alignment_bytes % 8) == 0, "must be 8 byte aligned");
  // allocate frame (frame_size is also aligned, so stack is still aligned)
  __ push_frame(frame_size, tmp);

  // we have to always spill args since we need to do a call to get the thread
  // (and maybe attach it).
  arg_spiller.generate_spill(_masm, arg_save_area_offset);
  // Java methods won't preserve them, so save them here:
  preserve_callee_saved_registers(_masm, abi, reg_save_area_offset);

  __ block_comment("{ on_entry");
  __ load_const_optimized(call_target_address, CAST_FROM_FN_PTR(uint64_t, UpcallLinker::on_entry));
  __ z_aghik(Z_ARG1, Z_SP, frame_data_offset);
  __ call(call_target_address);
  __ z_lgr(Z_thread, Z_RET);
  __ block_comment("} on_entry");

  __ block_comment("{ argument shuffle");
  arg_spiller.generate_fill(_masm, arg_save_area_offset);
  if (needs_return_buffer) {
    assert(0, "ShouldNotReachHere  needs_return_buffer not required on s390x");
    assert(ret_buf_offset != -1, "no return buffer allocated");
  }
  __ z_lg(callerSP, _z_abi(callers_sp), Z_SP); // preset (used to access caller frame argument slots)
  arg_shuffle.generate(_masm, as_VMStorage(callerSP), abi._shadow_space_bytes, frame::z_jit_out_preserve_size, locs);
  __ block_comment("} argument shuffle");

  __ block_comment("{ receiver ");
  __ load_const_optimized(Z_ARG1, (intptr_t)receiver);
  __ resolve_jobject(Z_ARG1, Z_R1, Z_tmp_1);
  __ block_comment("} receiver ");

  __ load_const_optimized(Z_method, (intptr_t)entry);
  __ z_stg(Z_method, Address(Z_thread, in_bytes(JavaThread::callee_target_offset())));

  __ z_lg(call_target_address, Address(Z_method, in_bytes(Method::from_compiled_offset())));
  __ call(call_target_address);

  // return value shuffle
    // return value shuffle
  if (!needs_return_buffer) {
    // CallArranger can pick a return type that goes in the same reg for both CCs.
    if (call_regs._ret_regs.length() > 0) { // 0 or 1
      VMStorage ret_reg = call_regs._ret_regs.at(0);
      // Check if the return reg is as expected.
      switch (ret_type) {
        case T_BOOLEAN:
        case T_BYTE:
        case T_SHORT:
        case T_CHAR:
        case T_INT:
          __ z_lgfr(Z_RET, Z_RET); // Clear garbage in high half.
          // fallthrough
        case T_LONG:
          assert(as_Register(ret_reg) == Z_RET, "unexpected result register");
          break;
        case T_FLOAT:
        case T_DOUBLE:
          assert(as_FloatRegister(ret_reg) == Z_FRET, "unexpected result register");
          break;
        default:
          fatal("unexpected return type: %s", type2name(ret_type));
      }
    }
  } else {
    // Load return values as required by UnboxBindingCalculator.

  assert(0, "ShouldNotReachHere  needs_return_buffer not required on s390x");  
  }

  result_spiller.generate_spill(_masm, res_save_area_offset);

  __ block_comment("{ on_exit");
  __ load_const_optimized(call_target_address, CAST_FROM_FN_PTR(uint64_t, UpcallLinker::on_exit));
  __ z_aghik(Z_ARG1, Z_SP, frame_data_offset);
  __ call(call_target_address);
  __ block_comment("} on_exit");

  restore_callee_saved_registers(_masm, abi, reg_save_area_offset);

  result_spiller.generate_fill(_masm, res_save_area_offset);

  __ pop_frame();
  __ restore_return_pc();
  __ z_br(Z_R14);  

  //////////////////////////////////////////////////////////////////////////////
   
  __ block_comment("{ exception handler");

  intptr_t exception_handler_offset = __ pc() - start;

  // Native caller has no idea how to handle exceptions,
  // so we just crash here. Up to callee to catch exceptions.
  __ verify_oop(Z_ARG1);
  __ load_const_optimized(call_target_address, CAST_FROM_FN_PTR(uint64_t, UpcallLinker::handle_uncaught_exception));
  __ call_c(call_target_address);
  __ should_not_reach_here();

  __ block_comment("} exception handler");

  _masm->flush();  

#ifndef PRODUCT
  stringStream ss;
  ss.print("upcall_stub_%s", entry->signature()->as_C_string());
  const char* name = _masm->code_string(ss.as_string());
#else // PRODUCT
  const char* name = "upcall_stub";
#endif // PRODUCT

  buffer.log_section_sizes(name);

  UpcallStub* blob
    = UpcallStub::create(name,
                         &buffer,
                         exception_handler_offset,
                         receiver,
                         in_ByteSize(frame_data_offset));
#ifndef PRODUCT
  if (lt.is_enabled()) {
    ResourceMark rm;
    LogStream ls(lt);
    blob->print_on(&ls);
  }
#endif

  return blob->code_begin();
}
