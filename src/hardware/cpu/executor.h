/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_CPU_EXECUTOR_H
#define IBMULATOR_CPU_EXECUTOR_H

class CPUExecutor;
extern CPUExecutor g_cpuexecutor;

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

	inttrap_intervalTree_t m_inttraps_tree;
	std::vector<inttrap_interval_t> m_inttraps_intervals;
	//TODO change this map to a stack
	std::map<uint32_t, std::vector<std::function<bool()>>> m_inttraps_ret;
	std::stack<pair<uint32_t,std::string>> m_dos_prg;
	uint32_t m_dos_prg_int_exit; //the exit csip of INT 21/4B (used for CPU logging)

	uint8_t load_eb();
	uint8_t load_rb();
	uint16_t load_ew();
	uint16_t load_rw();
	void load_ed(uint16_t &w1_, uint16_t &w2_);
	void store_eb(uint8_t _value);
	void store_rb(uint8_t _value);
	void store_rb_op(uint8_t _value);
	void store_ew(uint16_t _value);
	void store_rw(uint16_t _value);
	void store_rw_op(uint16_t _value);
	inline uint32_t EA_get_address();
	inline SegReg & EA_get_segreg();
	uint16_t EA_get_offset();

	void write_flags(uint16_t _flags, bool _change_IOPL, bool _change_IF, bool _change_NT=true);

	void write_word_pmode(SegReg & _seg, uint16_t _offset, uint16_t _data, uint8_t _exc, uint16_t _errcode);
	void write_word_pmode(SegReg &_seg, uint16_t _offset, uint16_t _data);
	uint16_t read_word_pmode(SegReg & _seg, uint16_t _offset, uint8_t _exc, uint16_t _errcode);

	void read_check_pmode(SegReg & _seg, uint16_t _offset, uint _len);
	void write_check_pmode(SegReg & _seg, uint16_t _offset, uint _len);
	void read_check_rmode(SegReg & _seg, uint16_t _offset, uint _len);
	void write_check_rmode(SegReg & _seg, uint16_t _offset, uint _len);

	uint8_t read_byte(SegReg &_seg, uint16_t _offset);
	uint16_t read_word(SegReg &_seg, uint16_t _offset);
	uint32_t read_dword(SegReg &_seg, uint16_t _offset);
	void write_byte(SegReg &_seg, uint16_t _offset, uint8_t _data);
	void write_word(SegReg &_seg, uint16_t _offset, uint16_t _data);

	uint8_t read_byte_nocheck(SegReg &_seg, uint16_t _offset);
	uint16_t read_word_nocheck(SegReg &_seg, uint16_t _offset);
	void write_byte_nocheck(SegReg &_seg, uint16_t _offset, uint8_t _data);
	void write_word_nocheck(SegReg &_seg, uint16_t _offset, uint16_t _data);

	void stack_push(uint16_t _value);
	uint16_t stack_pop();
	void stack_push_pmode(uint16_t _value);
	uint16_t stack_pop_pmode();
	uint16_t stack_read(uint16_t _offset);
	void stack_write(uint16_t _offset, uint16_t _data);

	void get_SS_SP_from_TSS(unsigned pl, uint16_t *ss, uint16_t *sp);

	void switch_tasks_load_selector(SegReg &_segreg, uint8_t _cs_rpl);
	void switch_tasks(Selector &selector, Descriptor &descriptor, unsigned source,
	                  bool push_error=false, uint16_t error_code=0);
	void task_gate(Selector &selector, Descriptor &gate_descriptor, unsigned source);
	void call_gate(Descriptor &gate_descriptor);

	void branch_far(Selector &selector, Descriptor &descriptor, uint16_t ip, uint8_t cpl);
	void branch_far(uint16_t cs, uint16_t ip);
	void branch_near(uint16_t newIP);
	void call_protected(uint16_t cs_raw, uint16_t disp);
	void return_protected(uint16_t pop_bytes);

	uint8_t ADC_b(uint8_t op1, uint8_t op2);
	uint16_t ADC_w(uint16_t op1, uint16_t op2);
	uint8_t ADD_b(uint8_t op1, uint8_t op2);
	uint16_t ADD_w(uint16_t op1, uint16_t op2);

	uint8_t AND_b(uint8_t op1, uint8_t op2);
	uint16_t AND_w(uint16_t op1, uint16_t op2);

	void CMP_b(uint8_t op1, uint8_t op2);
	void CMP_w(uint16_t op1, uint16_t op2);

	void INT(uint8_t vector, unsigned _type);
	static bool INT_debug(bool call, uint8_t vector, uint16_t ax, CPUCore *core, Memory *mem);

	void IRET_pmode();

	void JMP_far(uint16_t _sel, uint16_t _disp);
	void JMP_pmode(uint16_t cs, uint16_t disp);
	void JMP_call_gate(Selector &selector, Descriptor &gate_descriptor);

	uint8_t OR_b(uint8_t op1, uint8_t op2);
	uint16_t OR_w(uint16_t op1, uint16_t op2);

	void OUT_b(uint16_t _port, uint8_t _value);
	void OUT_w(uint16_t _port, uint16_t _value);

	uint8_t ROL_b(uint8_t _value, uint8_t _times);
	uint16_t ROL_w(uint16_t _value, uint8_t _times);
	uint8_t ROR_b(uint8_t _value, uint8_t _times);
	uint16_t ROR_w(uint16_t _value, uint8_t _times);
	uint8_t RCL_b(uint8_t _value, uint8_t _times);
	uint16_t RCL_w(uint16_t _value, uint8_t _times);
	uint8_t RCR_b(uint8_t _value, uint8_t _times);
	uint16_t RCR_w(uint16_t _value, uint8_t _times);

	uint8_t SHL_b(uint8_t _value, uint8_t _times);
	uint16_t SHL_w(uint16_t _value, uint8_t _times);
	uint8_t SHR_b(uint8_t _value, uint8_t _times);
	uint16_t SHR_w(uint16_t _value, uint8_t _times);
	uint8_t SAR_b(uint8_t _value, uint8_t _times);
	uint16_t SAR_w(uint16_t _value, uint8_t _times);

	uint8_t SBB_b(uint8_t op1, uint8_t op2);
	uint16_t SBB_w(uint16_t op1, uint16_t op2);

	uint8_t SUB_b(uint8_t op1, uint8_t op2);
	uint16_t SUB_w(uint16_t op1, uint16_t op2);

	void TEST_b(uint8_t _value1, uint8_t _value2);
	void TEST_w(uint16_t _value1, uint16_t _value2);

	uint8_t XOR_b(uint8_t _value1, uint8_t _value2);
	uint16_t XOR_w(uint16_t _value1, uint16_t _value2);

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
	void AAD(uint8_t imm);
	void AAM(uint8_t imm);
	void AAS();

	void ADC_eb_rb();
	void ADC_ew_rw();
	void ADC_rb_eb();
	void ADC_rw_ew();
	void ADC_AL_db(uint8_t imm);
	void ADC_AX_dw(uint16_t imm);
	void ADC_eb_db(uint8_t imm);
	void ADC_ew_dw(uint16_t imm);
	void ADC_ew_db(uint8_t imm);

	void ADD_eb_rb();
	void ADD_ew_rw();
	void ADD_rb_eb();
	void ADD_rw_ew();
	void ADD_AL_db(uint8_t imm);
	void ADD_AX_dw(uint16_t imm);
	void ADD_eb_db(uint8_t imm);
	void ADD_ew_dw(uint16_t imm);
	void ADD_ew_db(uint8_t imm);

	void AND_eb_rb();
	void AND_ew_rw();
	void AND_rb_eb();
	void AND_rw_ew();
	void AND_AL_db(uint8_t imm);
	void AND_AX_dw(uint16_t imm);
	void AND_eb_db(uint8_t imm);
	void AND_ew_dw(uint16_t imm);
	void AND_ew_db(uint8_t imm);

	void ARPL_ew_rw();

	void BOUND_rw_md();

	void CALL_cw(uint16_t offset);
	void CALL_ew();
	void CALL_cd(uint16_t newip, uint16_t newcs);
	void CALL_ed();

	void CBW();

	void CLC();
	void CLD();
	void CLI();
	void CLTS();

	void CMC();

	void CWD();

	void CMP_AL_db(uint8_t imm);
	void CMP_AX_dw(uint16_t imm);
	void CMP_eb_db(uint8_t imm);
	void CMP_eb_rb();
	void CMP_ew_db(uint8_t imm);
	void CMP_ew_dw(uint16_t imm);
	void CMP_ew_rw();
	void CMP_rb_eb();
	void CMP_rw_ew();
	void CMPSB();
	void CMPSW();

	void DAA();
	void DAS();

	void DIV_eb();
	void DIV_ew();

	void DEC_eb();
	void DEC_ew();
	void DEC_rw();

	void ENTER(uint16_t bytes, uint8_t level);

	void FPU_ESC();

	void HLT();

	void IDIV_eb();
	void IDIV_ew();
	void IMUL_eb();
	void IMUL_ew();
	void IMUL_rw_ew_dw(uint16_t imm);

	void IN_AL_db(uint8_t port);
	void IN_AL_DX();
	void IN_AX_db(uint8_t port);
	void IN_AX_DX();

	void INC_eb();
	void INC_ew();
	void INC_rw();

	void INSB();
	void INSW();

	void INT3();
	void INT_db(uint8_t vector);
	void INTO();

	void IRET();

	void JA_cb(int8_t disp);
	void JBE_cb(int8_t disp);
	void JC_cb(int8_t disp);
	void JNC_cb(int8_t disp);
	void JE_cb(int8_t disp);
	void JNE_cb(int8_t disp);
	void JO_cb(int8_t disp);
	void JNO_cb(int8_t disp);
	void JPE_cb(int8_t disp);
	void JPO_cb(int8_t disp);
	void JS_cb(int8_t disp);
	void JNS_cb(int8_t disp);
	void JL_cb(int8_t disp);
	void JNL_cb(int8_t disp);
	void JLE_cb(int8_t disp);
	void JNLE_cb(int8_t disp);
	void JCXZ_cb(int8_t disp);

	void JMP_ew();
	void JMP_ed();
	void JMP_cb(int8_t disp);
	void JMP_cw(uint16_t offset);
	void JMP_cd(uint16_t newcs, uint16_t newip);

	void LAHF();
	void LAR_rw_ew();
	void LES_rw_ed();
	void LDS_rw_ed();
	void LEA_rw_m();
	void LEAVE();
	void LGDT();
	void LIDT();
	void LLDT_ew();
	void LMSW_ew();
	void LOADALL();
	void LODSB();
	void LODSW();

	void LOOP(int8_t disp);
	void LOOPZ(int8_t disp);
	void LOOPNZ(int8_t disp);

	void LSL_rw_ew();

	void LTR_ew();

	void MOV_eb_rb();
	void MOV_ew_rw();
	void MOV_rb_eb();
	void MOV_rw_ew();
	void MOV_ew_ES();
	void MOV_ew_CS();
	void MOV_ew_SS();
	void MOV_ew_DS();
	void MOV_ES_ew();
	void MOV_SS_ew();
	void MOV_DS_ew();
	void MOV_AL_xb(uint16_t offset);
	void MOV_AX_xw(uint16_t offset);
	void MOV_xb_AL(uint16_t offset);
	void MOV_xw_AX(uint16_t offset);
	void MOV_rb_db(uint8_t data);
	void MOV_rw_dw(uint16_t data);
	void MOV_eb_db(uint8_t data);
	void MOV_ew_dw(uint16_t data);
	void MOVSB();
	void MOVSW();

	void MUL_eb();
	void MUL_ew();

	void NEG_eb();
	void NEG_ew();

	void NOP();

	void NOT_eb();
	void NOT_ew();

	void OR_eb_rb();
	void OR_ew_rw();
	void OR_rb_eb();
	void OR_rw_ew();
	void OR_AL_db(uint8_t imm);
	void OR_AX_dw(uint16_t imm);
	void OR_eb_db(uint8_t imm);
	void OR_ew_dw(uint16_t imm);
	void OR_ew_db(uint8_t imm);

	void OUT_db_AL(uint8_t port);
	void OUT_db_AX(uint8_t port);
	void OUT_DX_AL();
	void OUT_DX_AX();

	void OUTSB();
	void OUTSW();

	void POP_DS();
	void POP_ES();
	void POP_SS();
	void POP_mw();
	void POP_rw();
	void POPA();
	void POPF();

	void PUSH_ES();
	void PUSH_CS();
	void PUSH_SS();
	void PUSH_DS();
	void PUSH_rw();
	void PUSH_mw();
	void PUSH_dw(uint16_t imm);
	void PUSH_db(uint8_t imm);
	void PUSHA();
	void PUSHF();

	void ROL_eb_db(uint8_t db);
	void ROL_ew_db(uint8_t db);
	void ROL_eb_1();
	void ROL_ew_1();
	void ROL_eb_CL();
	void ROL_ew_CL();
	void ROR_eb_db(uint8_t db);
	void ROR_ew_db(uint8_t db);
	void ROR_eb_1();
	void ROR_ew_1();
	void ROR_eb_CL();
	void ROR_ew_CL();
	void RCL_eb_db(uint8_t db);
	void RCL_ew_db(uint8_t db);
	void RCL_eb_1();
	void RCL_ew_1();
	void RCL_eb_CL();
	void RCL_ew_CL();
	void RCR_eb_db(uint8_t db);
	void RCR_ew_db(uint8_t db);
	void RCR_eb_1();
	void RCR_ew_1();
	void RCR_eb_CL();
	void RCR_ew_CL();

	void RET_near(uint16_t popbytes);
	void RET_far(uint16_t popbytes);

	void SAL_eb_db(uint8_t db);
	void SAL_ew_db(uint8_t db);
	void SAL_eb_1();
	void SAL_ew_1();
	void SAL_eb_CL();
	void SAL_ew_CL();
	void SHR_eb_db(uint8_t db);
	void SHR_ew_db(uint8_t db);
	void SHR_eb_1();
	void SHR_ew_1();
	void SHR_eb_CL();
	void SHR_ew_CL();
	void SAR_eb_db(uint8_t db);
	void SAR_ew_db(uint8_t db);
	void SAR_eb_1();
	void SAR_ew_1();
	void SAR_eb_CL();
	void SAR_ew_CL();
	void SAHF();

	void SALC();

	void SBB_eb_rb();
	void SBB_ew_rw();
	void SBB_rb_eb();
	void SBB_rw_ew();
	void SBB_AL_db(uint8_t db);
	void SBB_AX_dw(uint16_t dw);
	void SBB_eb_db(uint8_t db);
	void SBB_ew_dw(uint16_t dw);
	void SBB_ew_db(uint8_t db);

	void SCASB();
	void SCASW();

	void SGDT();
	void SIDT();
	void SLDT_ew();

	void SMSW_ew();

	void STC();
	void STD();
	void STI();

	void STOSB();
	void STOSW();

	void STR_ew();

	void SUB_eb_rb();
	void SUB_ew_rw();
	void SUB_rb_eb();
	void SUB_rw_ew();
	void SUB_AL_db(uint8_t db);
	void SUB_AX_dw(uint16_t dw);
	void SUB_eb_db(uint8_t db);
	void SUB_ew_dw(uint16_t dw);
	void SUB_ew_db(uint8_t db);

	void TEST_eb_rb();
	void TEST_ew_rw();
	void TEST_AL_db(uint8_t db);
	void TEST_AX_dw(uint16_t dw);
	void TEST_eb_db(uint8_t db);
	void TEST_ew_dw(uint16_t dw);

	void VERR_ew();
	void VERW_ew();

	void WAIT();

	void XCHG_eb_rb();
	void XCHG_ew_rw();
	void XCHG_AX_rw();

	void XLATB();

	void XOR_rb_eb();
	void XOR_eb_rb();
	void XOR_AL_db(uint8_t db);
	void XOR_rw_ew();
	void XOR_ew_rw();
	void XOR_AX_dw(uint16_t dw);
	void XOR_eb_db(uint8_t db);
	void XOR_ew_dw(uint16_t dw);
	void XOR_ew_db(uint8_t db);
};

#endif
