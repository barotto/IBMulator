/*
 * Copyright (C) 2015, 2016  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_CPU_EXECUTOR_H
#define IBMULATOR_CPU_EXECUTOR_H

class CPUExecutor;
extern CPUExecutor g_cpuexecutor;

#include "hardware/cpu.h"
#include "decoder.h"
#include "interval_tree.h"
#include <stack>




enum {
	CPU_TASK_FROM_CALL = 0,
	CPU_TASK_FROM_IRET = 1,
	CPU_TASK_FROM_JUMP = 2,
	CPU_TASK_FROM_INT  = 3
};


typedef std::function<bool( // return true if INT should call interrupt, false if otherwise
		bool call,     // 1=call, 0=return
		uint8_t vector,// the INT vector
		uint16_t ax,   // the AX value at INT call
		CPUCore* cpu,  // the current CPU core
		Memory* mem    // the current Memory
	)> inttrap_fun_t;

typedef Interval<inttrap_fun_t> inttrap_interval_t;
typedef IntervalTree<inttrap_fun_t> inttrap_intervalTree_t;

class CPUExecutor
{
private:

	Instruction * m_instr;
	uint m_base_ds;
	uint m_base_ss;
	uint32_t m_addr_mask;

	inttrap_intervalTree_t m_inttraps_tree;
	std::vector<inttrap_interval_t> m_inttraps_intervals;
	//TODO change this map to a stack
	std::map<uint32_t, std::vector<std::function<bool()>>> m_inttraps_ret;
	std::stack<std::pair<uint32_t,std::string>> m_dos_prg;
	uint32_t m_dos_prg_int_exit; //the exit csip of INT 21/4B (used for CPU logging)

	struct {
		uint32_t phy1;
		uint32_t phy2;
		unsigned len1;
		unsigned len2;
		unsigned pages;
	} m_cached_phy;

	uint8_t load_eb();
	uint8_t load_rb();
	uint16_t load_ew();
	uint16_t load_rw();
	uint16_t load_rw_op();
	uint32_t load_ed();
	void load_m1616(uint16_t &w2_,  uint16_t &w1_);
	void load_m1632(uint32_t &dw1_, uint16_t &w2_);
	void load_m3232(uint32_t &dw2_, uint32_t &dw1_);
	uint32_t load_rd();
	uint32_t load_rd_op();
	uint16_t load_sr();
	void store_eb(uint8_t _value);
	void store_rb(uint8_t _value);
	void store_rb_op(uint8_t _value);
	void store_ew(uint16_t _value);
	void store_ew_rmw(uint16_t _value);
	void store_rw(uint16_t _value);
	void store_rw_op(uint16_t _value);
	void store_ed(uint32_t _value);
	void store_ed_rmw(uint32_t _value);
	void store_rd(uint32_t _value);
	void store_rd_op(uint32_t _value);
	void store_sr(uint16_t _value);
	SegReg & EA_get_segreg_16();
	uint32_t EA_get_offset_16();
	SegReg & EA_get_segreg_32();
	uint32_t EA_get_offset_32();
	SegReg & (CPUExecutor::*EA_get_segreg)();
	uint32_t (CPUExecutor::*EA_get_offset)();

	void write_flags(uint16_t _flags, bool _change_IOPL, bool _change_IF, bool _change_NT=true);
	void write_flags(uint16_t _flags);
	void write_eflags(uint32_t _eflags, bool _change_IOPL, bool _change_IF, bool _change_NT, bool _change_VM);

	void seg_check(SegReg & _seg, uint32_t _offset, unsigned _len, bool _write, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	void seg_check_read(SegReg & _seg, uint32_t _offset, unsigned _len, uint8_t _vector, uint16_t _errcode);
	void seg_check_write(SegReg & _seg, uint32_t _offset, unsigned _len, uint8_t _vector, uint16_t _errcode);
	void io_check(uint16_t _port, unsigned _len);

	void mem_access(uint32_t _linear, unsigned _len, bool _user, bool _write);

	uint8_t  read_byte();
	uint16_t read_word();
	uint32_t read_dword();
	uint64_t read_qword();
	uint32_t read_xpages();
	uint8_t  read_byte(SegReg &_seg, uint32_t _offset, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	uint16_t read_word(SegReg &_seg, uint32_t _offset, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	uint32_t read_dword(SegReg &_seg, uint32_t _offset, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	uint16_t read_word_rmw(SegReg &_seg, uint32_t _offset, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	uint32_t read_dword_rmw(SegReg &_seg, uint32_t _offset, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	uint8_t  read_byte(uint32_t _linear);
	uint16_t read_word(uint32_t _linear);
	uint32_t read_dword(uint32_t _linear);
	uint64_t read_qword(uint32_t _linear);

	void write_byte(uint8_t _data);
	void write_word(uint16_t _data);
	void write_dword(uint32_t _data);
	void write_xpages(uint32_t _data);
	void write_byte(SegReg &_seg, uint32_t _offset, uint8_t _data, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	void write_word(SegReg &_seg, uint32_t _offset, uint16_t _data, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	void write_dword(SegReg &_seg, uint32_t _offset, uint32_t _data, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	void write_word(SegReg &_seg, uint32_t _offset, uint16_t _data, unsigned _pl, uint8_t _vector, uint16_t _errcode);
	void write_dword(SegReg &_seg, uint32_t _offset, uint32_t _data, unsigned _pl, uint8_t _vector, uint16_t _errcode);
	void write_byte(uint32_t _linear, uint8_t _data);
	void write_word(uint32_t _linear, uint16_t _data);
	void write_dword(uint32_t _linear, uint32_t _data);

	void stack_push_word(uint16_t _value);
	void stack_push_dword(uint32_t _value);
	uint16_t stack_pop_word();
	uint32_t stack_pop_dword();
	void stack_write_word(uint16_t _value, uint32_t _offset);
	void stack_write_dword(uint32_t _value, uint32_t _offset);
	uint16_t stack_read_word(uint32_t _offset);
	uint32_t stack_read_dword(uint32_t _offset);

	void branch_relative(int32_t _offset);
	void branch_near(uint32_t new_EIP);
	void branch_far(Selector &selector, Descriptor &descriptor, uint32_t eip, uint8_t cpl);
	void branch_far(uint16_t cs, uint32_t eip);
	void branch_far_pmode(uint16_t cs, uint32_t eip);
	void call_relative(int32_t _offset);
	void call_16(uint16_t _cs, uint16_t _ip);
	void call_32(uint16_t _cs, uint32_t _eip);
	void call_pmode(uint16_t cs_raw, uint16_t disp);
	void call_gate(Descriptor &gate_descriptor);
	void return_near(uint32_t _newEIP, uint16_t _pop_bytes);
	void return_far_rmode(uint16_t _newCS, uint32_t _newEIP, uint16_t _pop_bytes);
	void return_far_pmode(uint16_t _pop_bytes, bool _32bit);
	void jump_call_gate(Selector &selector, Descriptor &gate_descriptor);
	void iret_pmode(bool _32bit);
	void stack_return_to_v86(Selector &_cs, uint32_t _eip, uint32_t _eflags);

	void get_SS_ESP_from_TSS(unsigned pl, uint16_t &ss, uint32_t &sp);
	void switch_tasks_load_selector(SegReg &_segreg, uint8_t _cs_rpl);
	void switch_tasks(Selector &selector, Descriptor &descriptor, unsigned source,
	                  bool push_error=false, uint16_t error_code=0);
	void task_gate(Selector &selector, Descriptor &gate_descriptor, unsigned source);

	template<typename T>
	T interrupt_prepare_stack(SegReg &new_stack, T temp_ESP,
			unsigned _pl, unsigned _gate_type, bool _push_error, uint16_t _error_code);
	void interrupt_inner_privilege(Descriptor &gate_descriptor,
			Selector &cs_selector, Descriptor &cs_descriptor,
			bool _push_error, uint16_t _error_code);
	void interrupt_same_privilege(Descriptor &gate_descriptor,
			Selector &cs_selector, Descriptor &cs_descriptor,
			bool _push_error, uint16_t _error_code);

	void check_CPL_privilege(bool _mode_cond, const char *_opstr);

	uint8_t  ADC_b(uint8_t  op1, uint8_t  op2);
	uint16_t ADC_w(uint16_t op1, uint16_t op2);
	uint32_t ADC_d(uint32_t op1, uint32_t op2);

	uint8_t  ADD_b(uint8_t  op1, uint8_t  op2);
	uint16_t ADD_w(uint16_t op1, uint16_t op2);
	uint32_t ADD_d(uint32_t op1, uint32_t op2);

	uint8_t  AND_b(uint8_t  op1, uint8_t  op2);
	uint16_t AND_w(uint16_t op1, uint16_t op2);
	uint32_t AND_d(uint32_t op1, uint32_t op2);

	uint16_t BT_ew(uint16_t _op2, bool _rmw);
	uint32_t BT_ed(uint32_t _op2, bool _rmw);

	void CMP_b(uint8_t op1, uint8_t op2);
	void CMP_w(uint16_t op1, uint16_t op2);
	void CMP_d(uint32_t op1, uint32_t op2);

	uint16_t DEC_w(uint16_t _op1);
	uint32_t DEC_d(uint32_t _op1);

	int16_t IMUL_w(int16_t op1, int16_t op2);
	int32_t IMUL_d(int32_t op1, int32_t op2);

	void INSB(uint32_t _offset);
	void INSW(uint32_t _offset);
	void INSD(uint32_t _offset);

	uint16_t INC_w(uint16_t _op1);
	uint32_t INC_d(uint32_t _op1);

	void INT(uint8_t vector, unsigned _type);
	static bool INT_debug(bool call, uint8_t vector, uint16_t ax, CPUCore *core, Memory *mem);

	void Jcc(bool _cond, int32_t _offset);

	uint32_t LAR(uint16_t raw_sel);

	void LDT_m(uint32_t &base_, uint16_t &limit_);

	uint32_t LOOP(uint32_t _count);
	uint32_t LOOPZ(uint32_t _count);
	uint32_t LOOPNZ(uint32_t _count);

	uint32_t LSL();

	uint8_t  OR_b(uint8_t  op1, uint8_t  op2);
	uint16_t OR_w(uint16_t op1, uint16_t op2);
	uint32_t OR_d(uint32_t op1, uint32_t op2);

	void OUT_b(uint16_t _port, uint8_t  _value);
	void OUT_w(uint16_t _port, uint16_t _value);
	void OUT_d(uint16_t _port, uint32_t _value);

	void OUTSB(uint8_t  _value);
	void OUTSW(uint16_t _value);
	void OUTSD(uint32_t _value);

	uint8_t  ROL_b(uint8_t  _op1, uint8_t _count);
	uint16_t ROL_w(uint16_t _op1, uint8_t _count);
	uint32_t ROL_d(uint32_t _op1, uint8_t _count);
	uint8_t  ROR_b(uint8_t  _op1, uint8_t _count);
	uint16_t ROR_w(uint16_t _op1, uint8_t _count);
	uint32_t ROR_d(uint32_t _op1, uint8_t _count);
	uint8_t  RCL_b(uint8_t  _op1, uint8_t _count);
	uint16_t RCL_w(uint16_t _op1, uint8_t _count);
	uint32_t RCL_d(uint32_t _op1, uint8_t _count);
	uint8_t  RCR_b(uint8_t  _op1, uint8_t _count);
	uint16_t RCR_w(uint16_t _op1, uint8_t _count);
	uint32_t RCR_d(uint32_t _op1, uint8_t _count);

	uint8_t  SHL_b(uint8_t  _op1, uint8_t _count);
	uint16_t SHL_w(uint16_t _op1, uint8_t _count);
	uint32_t SHL_d(uint32_t _op1, uint8_t _count);
	uint8_t  SHR_b(uint8_t  _op1, uint8_t _count);
	uint16_t SHR_w(uint16_t _op1, uint8_t _count);
	uint32_t SHR_d(uint32_t _op1, uint8_t _count);
	uint8_t  SAR_b(uint8_t  _op1, uint8_t _count);
	uint16_t SAR_w(uint16_t _op1, uint8_t _count);
	uint32_t SAR_d(uint32_t _op1, uint8_t _count);

	void SDT(unsigned _reg, uint32_t _base_mask);

	uint16_t SHLD_w(uint16_t _op1, uint16_t _op2, uint8_t _count);
	uint32_t SHLD_d(uint32_t _op1, uint32_t _op2, uint8_t _count);

	uint16_t SHRD_w(uint16_t _op1, uint16_t _op2, uint8_t _count);
	uint32_t SHRD_d(uint32_t _op1, uint32_t _op2, uint8_t _count);

	uint8_t  SBB_b(uint8_t  _op1, uint8_t  _op2);
	uint16_t SBB_w(uint16_t _op1, uint16_t _op2);
	uint32_t SBB_d(uint32_t _op1, uint32_t _op2);

	uint8_t  SUB_b(uint8_t  _op1, uint8_t  _op2);
	uint16_t SUB_w(uint16_t _op1, uint16_t _op2);
	uint32_t SUB_d(uint32_t _op1, uint32_t _op2);

	void TEST_b(uint8_t  _value1, uint8_t  _value2);
	void TEST_w(uint16_t _value1, uint16_t _value2);
	void TEST_d(uint32_t _value1, uint32_t _value2);

	uint8_t  XOR_b(uint8_t  _value1, uint8_t  _value2);
	uint16_t XOR_w(uint16_t _value1, uint16_t _value2);
	uint32_t XOR_d(uint32_t _value1, uint32_t _value2);

	void rep_16();
	void rep_32();
	void illegal_opcode();

public:

	CPUExecutor();

	void reset(uint _signal);

	void execute(Instruction * _instr);
	Instruction * get_current_instruction() { return m_instr; }

	void interrupt(uint8_t _vector);
	void interrupt_pmode(uint8_t _vector, bool _soft_int,
			bool _push_error, uint16_t _error_code);

	void register_INT_trap(uint8_t _lo_vec, uint8_t _hi_vec, inttrap_fun_t _fn);

	void AAA();
	void AAD();
	void AAM();
	void AAS();

	void ADC_eb_rb();
	void ADC_ew_rw();
	void ADC_ed_rd();
	void ADC_rb_eb();
	void ADC_rw_ew();
	void ADC_rd_ed();
	void ADC_AL_ib();
	void ADC_AX_iw();
	void ADC_EAX_id();
	void ADC_eb_ib();
	void ADC_ew_iw();
	void ADC_ed_id();
	void ADC_ew_ib();
	void ADC_ed_ib();

	void ADD_eb_rb();
	void ADD_ew_rw();
	void ADD_ed_rd();
	void ADD_rb_eb();
	void ADD_rw_ew();
	void ADD_rd_ed();
	void ADD_AL_ib();
	void ADD_AX_iw();
	void ADD_EAX_id();
	void ADD_eb_ib();
	void ADD_ew_iw();
	void ADD_ed_id();
	void ADD_ew_ib();
	void ADD_ed_ib();

	void AND_eb_rb();
	void AND_ew_rw();
	void AND_ed_rd();
	void AND_rb_eb();
	void AND_rw_ew();
	void AND_rd_ed();
	void AND_AL_ib();
	void AND_AX_iw();
	void AND_EAX_id();
	void AND_eb_ib();
	void AND_ew_iw();
	void AND_ed_id();
	void AND_ew_ib();
	void AND_ed_ib();

	void ARPL_ew_rw();

	void BOUND_rw_md();
	void BOUND_rd_mq();

	void BSF_rw_ew();
	void BSF_rd_ed();

	void BSR_rw_ew();
	void BSR_rd_ed();

	void BT_ew_rw();
	void BT_ed_rd();
	void BT_ew_ib();
	void BT_ed_ib();

	void BTC_ew_rw();
	void BTC_ed_rd();
	void BTC_ew_ib();
	void BTC_ed_ib();

	void BTR_ew_rw();
	void BTR_ed_rd();
	void BTR_ew_ib();
	void BTR_ed_ib();

	void BTS_ew_rw();
	void BTS_ed_rd();
	void BTS_ew_ib();
	void BTS_ed_ib();

	void CALL_rel16();
	void CALL_rel32();
	void CALL_ew();
	void CALL_ed();
	void CALL_ptr1616();
	void CALL_ptr1632();
	void CALL_m1616();
	void CALL_m1632();

	void CBW();
	void CWD();
	void CWDE();
	void CDQ();

	void CLC();
	void CLD();
	void CLI();
	void CLTS();

	void CMC();

	void CMP_eb_rb();
	void CMP_ew_rw();
	void CMP_ed_rd();
	void CMP_rb_eb();
	void CMP_rw_ew();
	void CMP_rd_ed();
	void CMP_AL_ib();
	void CMP_AX_iw();
	void CMP_EAX_id();
	void CMP_eb_ib();
	void CMP_ew_iw();
	void CMP_ed_id();
	void CMP_ew_ib();
	void CMP_ed_ib();

	void CMPSB_a16();
	void CMPSB_a32();
	void CMPSW_a16();
	void CMPSW_a32();
	void CMPSD_a16();
	void CMPSD_a32();

	void DAA();
	void DAS();

	void DIV_eb();
	void DIV_ew();
	void DIV_ed();

	void DEC_eb();
	void DEC_ew();
	void DEC_ed();
	void DEC_rw_op();
	void DEC_rd_op();

	void ENTER_o16();
	void ENTER_o32();

	void FPU_ESC();

	void HLT();

	void IDIV_eb();
	void IDIV_ew();
	void IDIV_ed();

	void IMUL_eb();
	void IMUL_ew();
	void IMUL_ed();
	void IMUL_rw_ew();
	void IMUL_rd_ed();
	void IMUL_rw_ew_ib();
	void IMUL_rd_ed_ib();
	void IMUL_rw_ew_iw();
	void IMUL_rd_ed_id();

	void IN_AL_ib();
	void IN_AL_DX();
	void IN_AX_ib();
	void IN_EAX_ib();
	void IN_AX_DX();
	void IN_EAX_DX();

	void INC_eb();
	void INC_ew();
	void INC_ed();
	void INC_rw_op();
	void INC_rd_op();

	void INSB_a16();
	void INSB_a32();
	void INSW_a16();
	void INSW_a32();
	void INSD_a16();
	void INSD_a32();

	void INT3();
	void INT_ib();
	void INTO();

	void IRET();
	void IRETD();

	void JO_cb();
	void JNO_cb();
	void JC_cb();
	void JNC_cb();
	void JE_cb();
	void JNE_cb();
	void JBE_cb();
	void JA_cb();
	void JS_cb();
	void JNS_cb();
	void JPE_cb();
	void JPO_cb();
	void JL_cb();
	void JNL_cb();
	void JLE_cb();
	void JNLE_cb();

	void JO_cw();
	void JNO_cw();
	void JC_cw();
	void JNC_cw();
	void JE_cw();
	void JNE_cw();
	void JBE_cw();
	void JA_cw();
	void JS_cw();
	void JNS_cw();
	void JPE_cw();
	void JPO_cw();
	void JL_cw();
	void JNL_cw();
	void JLE_cw();
	void JNLE_cw();

	void JO_cd();
	void JNO_cd();
	void JC_cd();
	void JNC_cd();
	void JE_cd();
	void JNE_cd();
	void JBE_cd();
	void JA_cd();
	void JS_cd();
	void JNS_cd();
	void JPE_cd();
	void JPO_cd();
	void JL_cd();
	void JNL_cd();
	void JLE_cd();
	void JNLE_cd();

	void JCXZ_cb();
	void JECXZ_cb();

	void JMP_rel8();
	void JMP_rel16();
	void JMP_rel32();
	void JMP_ptr1616();
	void JMP_ptr1632();
	void JMP_ew();
	void JMP_ed();
	void JMP_m1616();
	void JMP_m1632();

	void LAHF();

	void LAR_rw_ew();
	void LAR_rd_ed();

	void LEA_rw_m();
	void LEA_rd_m();

	void LEAVE_o16();
	void LEAVE_o32();

	void LGDT_o16();
	void LGDT_o32();
	void LIDT_o16();
	void LIDT_o32();
	void LLDT_ew();

	void LDS_rw_mp();
	void LDS_rd_mp();
	void LSS_rw_mp();
	void LSS_rd_mp();
	void LES_rw_mp();
	void LES_rd_mp();
	void LFS_rw_mp();
	void LFS_rd_mp();
	void LGS_rw_mp();
	void LGS_rd_mp();

	void LMSW_ew();

	void LOADALL_286();

	void LODSB_a16();
	void LODSB_a32();
	void LODSW_a16();
	void LODSW_a32();
	void LODSD_a16();
	void LODSD_a32();

	void LOOP_a16();
	void LOOP_a32();
	void LOOPZ_a16();
	void LOOPZ_a32();
	void LOOPNZ_a16();
	void LOOPNZ_a32();

	void LSL_rw_ew();
	void LSL_rd_ew();

	void LTR_ew();

	void MOV_eb_rb();
	void MOV_ew_rw();
	void MOV_ed_rd();
	void MOV_rb_eb();
	void MOV_rw_ew();
	void MOV_rd_ed();
	void MOV_ew_SR();
	void MOV_SR_ew();
	void MOV_AL_xb();
	void MOV_AX_xw();
	void MOV_EAX_xd();
	void MOV_xb_AL();
	void MOV_xw_AX();
	void MOV_xd_EAX();
	void MOV_rb_ib();
	void MOV_rw_iw();
	void MOV_rd_id();
	void MOV_eb_ib();
	void MOV_ew_iw();
	void MOV_ed_id();

	void MOV_CR0_rd();
	void MOV_CR2_rd();
	void MOV_CR3_rd();
	void MOV_rd_CR();
	void MOV_DR_rd();
	void MOV_rd_DR();
	void MOV_TR_rd();
	void MOV_rd_TR();

	void MOVSB_a16();
	void MOVSB_a32();
	void MOVSW_a16();
	void MOVSW_a32();
	void MOVSD_a16();
	void MOVSD_a32();

	void MOVSX_rw_eb();
	void MOVSX_rd_eb();
	void MOVSX_rd_ew();

	void MOVZX_rw_eb();
	void MOVZX_rd_eb();
	void MOVZX_rd_ew();

	void MUL_eb();
	void MUL_ew();
	void MUL_ed();

	void NEG_eb();
	void NEG_ew();
	void NEG_ed();

	void NOP();

	void NOT_eb();
	void NOT_ew();
	void NOT_ed();

	void OR_eb_rb();
	void OR_ew_rw();
	void OR_ed_rd();
	void OR_rb_eb();
	void OR_rw_ew();
	void OR_rd_ed();
	void OR_AL_ib();
	void OR_AX_iw();
	void OR_EAX_id();
	void OR_eb_ib();
	void OR_ew_iw();
	void OR_ed_id();
	void OR_ew_ib();
	void OR_ed_ib();

	void OUT_ib_AL();
	void OUT_ib_AX();
	void OUT_ib_EAX();
	void OUT_DX_AL();
	void OUT_DX_AX();
	void OUT_DX_EAX();

	void OUTSB_a16();
	void OUTSB_a32();
	void OUTSW_a16();
	void OUTSW_a32();
	void OUTSD_a16();
	void OUTSD_a32();

	void POP_SR_w();
	void POP_SR_dw();
	void POP_mw();
	void POP_md();
	void POP_rw_op();
	void POP_rd_op();

	void POPA();
	void POPAD();
	void POPF();
	void POPFD();

	void PUSH_SR_w();
	void PUSH_SR_dw();
	void PUSH_rw_op();
	void PUSH_rd_op();
	void PUSH_mw();
	void PUSH_md();
	void PUSH_ib();
	void PUSH_iw();
	void PUSH_id();

	void PUSHA();
	void PUSHAD();
	void PUSHF();
	void PUSHFD();

	void ROL_eb_ib();
	void ROL_ew_ib();
	void ROL_ed_ib();
	void ROL_eb_1();
	void ROL_ew_1();
	void ROL_ed_1();
	void ROL_eb_CL();
	void ROL_ew_CL();
	void ROL_ed_CL();
	void ROR_eb_ib();
	void ROR_ew_ib();
	void ROR_ed_ib();
	void ROR_eb_1();
	void ROR_ew_1();
	void ROR_ed_1();
	void ROR_eb_CL();
	void ROR_ew_CL();
	void ROR_ed_CL();
	void RCL_eb_ib();
	void RCL_ew_ib();
	void RCL_ed_ib();
	void RCL_eb_1();
	void RCL_ew_1();
	void RCL_ed_1();
	void RCL_eb_CL();
	void RCL_ew_CL();
	void RCL_ed_CL();
	void RCR_eb_ib();
	void RCR_ew_ib();
	void RCR_ed_ib();
	void RCR_eb_1();
	void RCR_ew_1();
	void RCR_ed_1();
	void RCR_eb_CL();
	void RCR_ew_CL();
	void RCR_ed_CL();

	void RET_near_o16();
	void RET_near_o32();
	void RET_far_o16();
	void RET_far_o32();

	void SAL_eb_ib();
	void SAL_ew_ib();
	void SAL_ed_ib();
	void SAL_eb_1();
	void SAL_ew_1();
	void SAL_ed_1();
	void SAL_eb_CL();
	void SAL_ew_CL();
	void SAL_ed_CL();
	void SHR_eb_ib();
	void SHR_ew_ib();
	void SHR_ed_ib();
	void SHR_eb_1();
	void SHR_ew_1();
	void SHR_ed_1();
	void SHR_eb_CL();
	void SHR_ew_CL();
	void SHR_ed_CL();
	void SAR_eb_ib();
	void SAR_ew_ib();
	void SAR_ed_ib();
	void SAR_eb_1();
	void SAR_ew_1();
	void SAR_ed_1();
	void SAR_eb_CL();
	void SAR_ew_CL();
	void SAR_ed_CL();

	void SAHF();

	void SALC();

	void SBB_eb_rb();
	void SBB_ew_rw();
	void SBB_ed_rd();
	void SBB_rb_eb();
	void SBB_rw_ew();
	void SBB_rd_ed();
	void SBB_AL_ib();
	void SBB_AX_iw();
	void SBB_EAX_id();
	void SBB_eb_ib();
	void SBB_ew_iw();
	void SBB_ed_id();
	void SBB_ew_ib();
	void SBB_ed_ib();

	void SCASB_a16();
	void SCASB_a32();
	void SCASW_a16();
	void SCASW_a32();
	void SCASD_a16();
	void SCASD_a32();

	void SETO_eb();
	void SETNO_eb();
	void SETB_eb();
	void SETNB_eb();
	void SETE_eb();
	void SETNE_eb();
	void SETBE_eb();
	void SETNBE_eb();
	void SETS_eb();
	void SETNS_eb();
	void SETP_eb();
	void SETNP_eb();
	void SETL_eb();
	void SETNL_eb();
	void SETLE_eb();
	void SETNLE_eb();

	void SGDT_o16();
	void SGDT_o32();
	void SIDT_o16();
	void SIDT_o32();
	void SLDT_ew();

	void SHLD_ew_rw_ib();
	void SHLD_ed_rd_ib();
	void SHLD_ew_rw_CL();
	void SHLD_ed_rd_CL();

	void SHRD_ew_rw_ib();
	void SHRD_ed_rd_ib();
	void SHRD_ew_rw_CL();
	void SHRD_ed_rd_CL();

	void SMSW_ew();

	void STC();
	void STD();
	void STI();

	void STOSB_a16();
	void STOSB_a32();
	void STOSW_a16();
	void STOSW_a32();
	void STOSD_a16();
	void STOSD_a32();

	void STR_ew();

	void SUB_eb_rb();
	void SUB_ew_rw();
	void SUB_ed_rd();
	void SUB_rb_eb();
	void SUB_rw_ew();
	void SUB_rd_ed();
	void SUB_AL_ib();
	void SUB_AX_iw();
	void SUB_EAX_iw();
	void SUB_eb_ib();
	void SUB_ew_iw();
	void SUB_ed_id();
	void SUB_ew_ib();
	void SUB_ed_ib();

	void TEST_eb_rb();
	void TEST_ew_rw();
	void TEST_ed_rd();
	void TEST_AL_ib();
	void TEST_AX_iw();
	void TEST_EAX_iw();
	void TEST_eb_ib();
	void TEST_ew_iw();
	void TEST_ed_id();

	void VERR_ew();
	void VERW_ew();

	void WAIT();

	void XCHG_eb_rb();
	void XCHG_ew_rw();
	void XCHG_ed_rd();
	void XCHG_AX_rw();
	void XCHG_EAX_rd();

	void XLATB_a16();
	void XLATB_a32();

	void XOR_rb_eb();
	void XOR_rw_ew();
	void XOR_rd_ed();
	void XOR_eb_rb();
	void XOR_ew_rw();
	void XOR_ed_rd();
	void XOR_AL_ib();
	void XOR_AX_iw();
	void XOR_EAX_ie();
	void XOR_eb_ib();
	void XOR_ew_iw();
	void XOR_ed_id();
	void XOR_ew_ib();
	void XOR_ed_ib();
};

#endif
