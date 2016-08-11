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


#define TLB_SIZE 1024 // number of entries in the TLB

#define LPF_OF(laddr)      ((laddr) & 0xFFFFF000)
#define PAGE_OFFSET(laddr) (uint32_t(laddr) & 0xFFF)

enum PageProtection {
	PAGE_SUPER, PAGE_READ, PAGE_WRITE
};
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY    0x40

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
	std::stack<std::pair<uint32_t,std::string>> m_dos_prg;
	uint32_t m_dos_prg_int_exit; //the exit csip of INT 21/4B (used for CPU logging)

	typedef struct {
	  uint32_t lpf;   // linear page frame
	  uint32_t ppf;   // physical page frame
	  unsigned protection;
	} TLBEntry;

	TLBEntry m_TLB[TLB_SIZE];

	inline unsigned TLB_index(uint32_t _lpf, unsigned _len) const {
		return (((_lpf + _len) & ((TLB_SIZE-1) << 12)) >> 12);
	}
	uint32_t TLB_lookup(uint32_t _linear, unsigned _len, bool _user, bool _write);
	void TLB_miss(uint32_t _linear, TLBEntry *_tlbent, bool _user, bool _write);
	void page_fault(unsigned _fault, uint32_t _linear, bool _user, bool _write);

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
	uint32_t load_ed();
	void load_ed_mem(uint16_t &w1_, uint16_t &w2_);
	uint32_t load_rd();
	void store_eb(uint8_t _value);
	void store_rb(uint8_t _value);
	void store_rb_op(uint8_t _value);
	void store_ew(uint16_t _value);
	void store_rw(uint16_t _value);
	void store_rw_op(uint16_t _value);
	void store_ed(uint32_t _value);
	void store_rd(uint32_t _value);
	void store_rd_op(uint32_t _value);
	SegReg & EA_get_segreg_16();
	uint32_t EA_get_offset_16();
	SegReg & EA_get_segreg_32();
	uint32_t EA_get_offset_32();
	SegReg & (CPUExecutor::*EA_get_segreg)();
	uint32_t (CPUExecutor::*EA_get_offset)();

	void write_flags(uint16_t _flags, bool _change_IOPL, bool _change_IF, bool _change_NT=true);
	void write_flags(uint16_t _flags);

	void seg_check_read(SegReg & _seg, uint32_t _offset, unsigned _len, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	void seg_check_write(SegReg & _seg, uint32_t _offset, unsigned _len, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	void page_check(unsigned _protection, uint32_t _linear, bool _user, bool _write);
	void mem_access_check(SegReg & _seg, uint32_t _offset, unsigned _len, bool _user, bool _write, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);

	uint8_t read_byte();
	uint16_t read_word();
	uint32_t read_dword();
	uint32_t read_xpages();
	uint8_t read_byte(SegReg &_seg, uint32_t _offset, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	uint16_t read_word(SegReg &_seg, uint32_t _offset, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	uint32_t read_dword(SegReg &_seg, uint32_t _offset, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);

	void write_byte(uint8_t _data);
	void write_word(uint16_t _data);
	void write_dword(uint32_t _data);
	void write_xpages(uint32_t _data);
	void write_byte(SegReg &_seg, uint32_t _offset, uint8_t _data, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	void write_word(SegReg &_seg, uint32_t _offset, uint16_t _data, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);
	void write_dword(SegReg &_seg, uint32_t _offset, uint32_t _data, uint8_t _vector=CPU_INVALID_INT, uint16_t _errcode=0);

	void stack_push_word(uint16_t _value);
	void stack_push_dword(uint32_t _value);
	uint16_t stack_pop_word();
	uint32_t stack_pop_dword();
	void stack_write_word(uint16_t _value, uint32_t _offset);
	void stack_write_dword(uint32_t _value, uint32_t _offset);
	uint16_t stack_read_word(uint32_t _offset);
	uint32_t stack_read_dword(uint32_t _offset);

	void get_SS_SP_from_TSS(unsigned pl, uint16_t &ss, uint16_t &sp);

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
	uint32_t ADC_d(uint32_t op1, uint32_t op2);

	uint8_t ADD_b(uint8_t op1, uint8_t op2);
	uint16_t ADD_w(uint16_t op1, uint16_t op2);
	uint32_t ADD_d(uint32_t op1, uint32_t op2);

	uint8_t AND_b(uint8_t op1, uint8_t op2);
	uint16_t AND_w(uint16_t op1, uint16_t op2);
	uint32_t AND_d(uint32_t op1, uint32_t op2);

	void CALL_cd(uint16_t newip, uint16_t newcs);

	void CMP_b(uint8_t op1, uint8_t op2);
	void CMP_w(uint16_t op1, uint16_t op2);
	void CMP_d(uint32_t op1, uint32_t op2);

	void INT(uint8_t vector, unsigned _type);
	static bool INT_debug(bool call, uint8_t vector, uint16_t ax, CPUCore *core, Memory *mem);

	void IRET_pmode();

	void JMP_far(uint16_t _sel, uint16_t _disp);
	void JMP_pmode(uint16_t cs, uint16_t disp);
	void JMP_call_gate(Selector &selector, Descriptor &gate_descriptor);

	uint8_t OR_b(uint8_t op1, uint8_t op2);
	uint16_t OR_w(uint16_t op1, uint16_t op2);
	uint32_t OR_d(uint32_t op1, uint32_t op2);

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
	void AAD();
	void AAM();
	void AAS();

	void ADC_eb_rb();
	void ADC_ew_rw();
	void ADC_ed_rd();
	void ADC_rb_eb();
	void ADC_rw_ew();
	void ADC_rd_ed();
	void ADC_AL_db();
	void ADC_AX_dw();
	void ADC_EAX_dd();
	void ADC_eb_db();
	void ADC_ew_dw();
	void ADC_ed_dd();
	void ADC_ew_db();
	void ADC_ed_db();

	void ADD_eb_rb();
	void ADD_ew_rw();
	void ADD_ed_rd();
	void ADD_rb_eb();
	void ADD_rw_ew();
	void ADD_rd_ed();
	void ADD_AL_db();
	void ADD_AX_dw();
	void ADD_EAX_dd();
	void ADD_eb_db();
	void ADD_ew_dw();
	void ADD_ed_dd();
	void ADD_ew_db();
	void ADD_ed_db();

	void AND_eb_rb();
	void AND_ew_rw();
	void AND_ed_rd();
	void AND_rb_eb();
	void AND_rw_ew();
	void AND_rd_ed();
	void AND_AL_db();
	void AND_AX_dw();
	void AND_EAX_dd();
	void AND_eb_db();
	void AND_ew_dw();
	void AND_ed_dd();
	void AND_ew_db();
	void AND_ed_db();

	void ARPL_ew_rw();

	void BOUND_rw_md();

	void CALL_cw();
	void CALL_ew();
	void CALL_cd();
	void CALL_ed();

	void CBW();

	void CLC();
	void CLD();
	void CLI();
	void CLTS();

	void CMC();

	void CWD();

	void CMP_eb_rb();
	void CMP_ew_rw();
	void CMP_ed_rd();
	void CMP_rb_eb();
	void CMP_rw_ew();
	void CMP_rd_ed();
	void CMP_AL_db();
	void CMP_AX_dw();
	void CMP_EAX_dd();
	void CMP_eb_db();
	void CMP_ew_dw();
	void CMP_ed_dd();
	void CMP_ew_db();
	void CMP_ed_db();

	void CMPSB_16();
	void CMPSW_16();
	void CMPSD_16();
	void CMPSB_32();
	void CMPSW_32();
	void CMPSD_32();

	void DAA();
	void DAS();

	void DIV_eb();
	void DIV_ew();

	void DEC_eb();
	void DEC_ew();
	void DEC_rw();

	void ENTER();

	void FPU_ESC();

	void HLT();

	void IDIV_eb();
	void IDIV_ew();
	void IMUL_eb();
	void IMUL_ew();
	void IMUL_rw_ew_dw();

	void IN_AL_db();
	void IN_AL_DX();
	void IN_AX_db();
	void IN_AX_DX();

	void INC_eb();
	void INC_ew();
	void INC_rw();

	void INSB();
	void INSW();

	void INT3();
	void INT_db();
	void INTO();

	void IRET();

	void JA_cb();
	void JBE_cb();
	void JC_cb();
	void JNC_cb();
	void JE_cb();
	void JNE_cb();
	void JO_cb();
	void JNO_cb();
	void JPE_cb();
	void JPO_cb();
	void JS_cb();
	void JNS_cb();
	void JL_cb();
	void JNL_cb();
	void JLE_cb();
	void JNLE_cb();
	void JCXZ_cb();

	void JMP_ew();
	void JMP_ed();
	void JMP_cb();
	void JMP_cw();
	void JMP_cd();

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

	void LOOP();
	void LOOPZ();
	void LOOPNZ();

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
	void MOV_AL_xb();
	void MOV_AX_xw();
	void MOV_xb_AL();
	void MOV_xw_AX();
	void MOV_rb_db();
	void MOV_rw_dw();
	void MOV_eb_db();
	void MOV_ew_dw();
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
	void OR_ed_rd();
	void OR_rb_eb();
	void OR_rw_ew();
	void OR_rd_ed();
	void OR_AL_db();
	void OR_AX_dw();
	void OR_EAX_dd();
	void OR_eb_db();
	void OR_ew_dw();
	void OR_ed_dd();
	void OR_ew_db();
	void OR_ed_db();

	void OUT_db_AL();
	void OUT_db_AX();
	void OUT_DX_AL();
	void OUT_DX_AX();

	void OUTSB();
	void OUTSW();

	void POP_DS();
	void POP_DS_32();
	void POP_ES();
	void POP_ES_32();
	void POP_SS();
	void POP_SS_32();
	void POP_FS();
	void POP_FS_32();
	void POP_GS();
	void POP_GS_32();
	void POP_mw();
	void POP_md();
	void POP_rw();
	void POP_rd();
	void POPA();
	void POPAD();
	void POPF();
	void POPFD();

	void PUSH_ES();
	void PUSH_ES_32();
	void PUSH_CS();
	void PUSH_CS_32();
	void PUSH_SS();
	void PUSH_SS_32();
	void PUSH_DS();
	void PUSH_DS_32();
	void PUSH_FS();
	void PUSH_FS_32();
	void PUSH_GS();
	void PUSH_GS_32();
	void PUSH_rw();
	void PUSH_rd();
	void PUSH_mw();
	void PUSH_md();
	void PUSH_db();
	void PUSH_dw();
	void PUSH_dd();
	void PUSHA();
	void PUSHAD();
	void PUSHF();
	void PUSHFD();

	void ROL_eb_db();
	void ROL_ew_db();
	void ROL_eb_1();
	void ROL_ew_1();
	void ROL_eb_CL();
	void ROL_ew_CL();
	void ROR_eb_db();
	void ROR_ew_db();
	void ROR_eb_1();
	void ROR_ew_1();
	void ROR_eb_CL();
	void ROR_ew_CL();
	void RCL_eb_db();
	void RCL_ew_db();
	void RCL_eb_1();
	void RCL_ew_1();
	void RCL_eb_CL();
	void RCL_ew_CL();
	void RCR_eb_db();
	void RCR_ew_db();
	void RCR_eb_1();
	void RCR_ew_1();
	void RCR_eb_CL();
	void RCR_ew_CL();

	void RET_near();
	void RET_far();

	void SAL_eb_db();
	void SAL_ew_db();
	void SAL_eb_1();
	void SAL_ew_1();
	void SAL_eb_CL();
	void SAL_ew_CL();
	void SHR_eb_db();
	void SHR_ew_db();
	void SHR_eb_1();
	void SHR_ew_1();
	void SHR_eb_CL();
	void SHR_ew_CL();
	void SAR_eb_db();
	void SAR_ew_db();
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
	void SBB_AL_db();
	void SBB_AX_dw();
	void SBB_eb_db();
	void SBB_ew_dw();
	void SBB_ew_db();

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
	void SUB_AL_db();
	void SUB_AX_dw();
	void SUB_eb_db();
	void SUB_ew_dw();
	void SUB_ew_db();

	void TEST_eb_rb();
	void TEST_ew_rw();
	void TEST_AL_db();
	void TEST_AX_dw();
	void TEST_eb_db();
	void TEST_ew_dw();

	void VERR_ew();
	void VERW_ew();

	void WAIT();

	void XCHG_eb_rb();
	void XCHG_ew_rw();
	void XCHG_AX_rw();

	void XLATB();

	void XOR_rb_eb();
	void XOR_eb_rb();
	void XOR_AL_db();
	void XOR_rw_ew();
	void XOR_ew_rw();
	void XOR_AX_dw();
	void XOR_eb_db();
	void XOR_ew_dw();
	void XOR_ew_db();
};

#endif
