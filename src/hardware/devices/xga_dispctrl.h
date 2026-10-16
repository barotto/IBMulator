/*
 * Copyright (C) 2026  Marco Bortolin
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

// XGA Display Controller

enum XGA_DispCtrlOpMode {
	XGA_OPMODE_VGA = 0x1,
	XGA_OPMODE_132 = 0x2,
	XGA_OPMODE_EXT = 0x4,
	XGA_OPMODE_DM  = 0x7,
	XGA_OPMODE_RF  = 0x8,
	XGA_OPMODE_MFI = 0x10
};

enum XGA_DispCtrlApCtrl {
	XGA_APCTRL_ASL = 0x3
};

enum XGA_DispCtrlInt {
	XGA_INT_CC = 0x80,
	XGA_INT_CR = 0x40,
	XGA_INT_SC = 0x04,
	XGA_INT_SP = 0x02,
	XGA_INT_SB = 0x01,
};

enum XGA_DispCtrlVMCtrl {
	XGA_VMCTRL_NPE = 0x80,
	XGA_VMCTRL_PVE = 0x40,
	XGA_VMCTRL_US  = 0x04,
	XGA_VMCTRL_EV  = 0x01,
};

enum XGA_DispCtrlVMInt {
	XGA_VMINT_NPS = 0x80,
	XGA_VMINT_PVS = 0x40,
};

enum XGA_DispCtrlApIdx {
	XGA_APIDX_AP = 0x3F
};

enum XGA_DispCtrlMemMode {
	XGA_MEMMODE_4BIT  = 0x2,
	XGA_MEMMODE_8BIT  = 0x3,
	XGA_MEMMODE_16BIT = 0x4,
	XGA_MEMMODE_PO    = 0x8,
	XGA_MEMMODE_PS    = 0x7
};


struct XGA_DispCtrl
{
	struct {
		bool MFI;   // Enable MFI Function (4)
		bool RF;    // Coprocessor Register Interface Format (3)
		uint8_t DM; // Display Mode (0-2)

		operator uint8_t() const {
			return (MFI << 4) | (RF << 3) | DM;
		}
		void operator =(uint8_t _v) {
			MFI = _v & XGA_OPMODE_MFI;
			RF  = _v & XGA_OPMODE_RF;
			DM  = _v & XGA_OPMODE_DM;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
					{{3,"DM"},{1,"RF"},{1,"MFI"}}
			);
		}
	} op_mode;            // 21x0 - Operating Mode

	struct {
		uint8_t ASL; // Aperture Size And Location (0-1)
		operator uint8_t() const {
			return ASL;
		}
		void operator=(uint8_t _v) {
			ASL = _v & XGA_APCTRL_ASL;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
			{{2,"ASL"}});
		}
	} ap_ctrl;            // 21x1 - Aperture Control

	struct {
		bool CC; // Coprocessor Operation Complete Enable (7)
		bool CR; // Coprocessor Access Rejected Enable (6)
		bool SC; // Sprite Display Complete Enable (2)
		bool SP; // Start Of Picture (End Of Blanking) Enable (1)
		bool SB; // Start Of Blanking (End Of Picture) Enable (0)

		operator uint8_t() const {
			return (CC << 7) | (CR << 6) | (SC << 3) | (SP << 1) | SB;
		}
		void operator =(uint8_t _v) {
			CC = _v & XGA_INT_CC;
			CR = _v & XGA_INT_CR;
			SC = _v & XGA_INT_SC;
			SP = _v & XGA_INT_SP;
			SB = _v & XGA_INT_SB;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
					{{1,"SB"},{1,"SP"},{1,"SC"},{3,""},{1,"CR"},{1,"CC"}}
			);
		}
	} int_en;             // 21x4 - Interrupt Enable

	struct {
		bool CC; // Coprocessor Operation Complete Status (7)
		bool CR; // Coprocessor Access Rejected Status (6)
		bool SC; // Sprite Display Complete Status (2)
		bool SP; // Start Of Picture (End Of Blanking) Status (1)
		bool SB; // Start Of Blanking (End Of Picture) Status (0)

		operator uint8_t() const {
			return (CC << 7) | (CR << 6) | (SC << 3) | (SP << 1) | SB;
		}
		void operator =(uint8_t _v) {
			CC = _v & XGA_INT_CC;
			CR = _v & XGA_INT_CR;
			SC = _v & XGA_INT_SC;
			SP = _v & XGA_INT_SP;
			SB = _v & XGA_INT_SB;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
					{{1,"SB"},{1,"SP"},{1,"SC"},{3,""},{1,"CR"},{1,"CC"}}
			);
		}
	} int_status;         // 21x5 - Interrupt Status
	
	struct {
		bool NPE; // VM Page Not Present Interrupt Enable (7)
		bool PVE; // VM Protection Violation Interrupt Enable (6)
		bool US;  // User / Supervisor (1)
		bool EV;  // Enable Virtual Address Lookup (0)

		operator uint8_t() const {
			return (NPE << 7) | (PVE << 6) | (US << 1) | EV;
		}
		void operator =(uint8_t _v) {
			NPE = _v & XGA_VMCTRL_NPE;
			PVE = _v & XGA_VMCTRL_PVE;
			US = _v & XGA_VMCTRL_US;
			EV = _v & XGA_VMCTRL_EV;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
					{{1,"EV"},{1,"US"},{4,""},{1,"PVE"},{1,"NPE"}}
			);
		}
	} vm_ctrl;            // 21x6 - Virtual Memory Control

	struct {
		bool NPS; // VM Page Not Present Interrupt Status (7)
		bool PVS; // VM Protection Violation Interrupt Status (6)

		operator uint8_t() const {
			return (NPS << 7) | (PVS << 6);
		}
		void operator =(uint8_t _v) {
			NPS = _v & XGA_VMINT_NPS;
			PVS = _v & XGA_VMINT_PVS;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
					{{6,""},{1,"PVS"},{1,"NPS"}}
			);
		}
	} vm_int;             // 21x7 - Virtual Memory Interrupt Status
	
	struct {
		uint8_t AP; // Aperture Index (0-5)
		operator uint8_t() const {
			return AP;
		}
		void operator=(uint8_t _v) {
			AP = _v & XGA_APIDX_AP;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
					{{6,"AP"}});
		}
	} ap_idx;             // 21x8 - Aperture Index
	
	struct {
		bool    PO; // PEL Order (3)
		uint8_t PS; // PEL Size (0-2)

		operator uint8_t() const {
			return (PO << 3) | PS;
		}
		void operator =(uint8_t _v) {
			PO = _v & XGA_MEMMODE_PO;
			PS = _v & XGA_MEMMODE_PS;
		}
		operator const char*() const {
			return ::register_to_string((uint8_t)(*this),
					{{3,"PS"},{1,"PO"}});
		}
	} mem_mode;           // 21x9 - Memory Access Mode

	
	void get_memory_conf(uint32_t &offset_, uint32_t &aperture_, uint32_t &bank_) const
	{
		// ASL=0 no 64KB aperture (1MB or 4MB) (not emulated)
		// ASL=1 64KB at address 0xA0000
		// ASL=2 64KB at address 0xB0000
		if(ap_ctrl.ASL != 0) {
			offset_ = ap_ctrl.ASL == 1 ? 0xA0000 : 0xB0000;
			aperture_ = 0x10000;
			bank_ = ap_idx;
		} else {
			PDEBUGF(LOG_V2, LOG_VGA, "XGA: Memory aperture 1MB or 4MB (not supported).\n");
		}
	}
	
	static constexpr const std::array<const char*, 16> regnames = {{
		"Operating Mode",
		"Aperture Control",
		"Reserved",
		"Reserved",
		"Interrupt Enable",
		"Interrupt Status",
		"Virtual Memory Control",
		"Virtual Memory Interrupt Status",
		"Aperture Index",
		"Memory Access Mode",
		"Extended Graphics Index",
		"Extended Graphics Data",
		"Extended Graphics Data",
		"Extended Graphics Data",
		"Extended Graphics Data",
		"Extended Graphics Data",
	}};
};
