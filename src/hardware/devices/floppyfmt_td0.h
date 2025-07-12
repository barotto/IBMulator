// license:BSD-3-Clause
// copyright-holders:Miodrag Milanovic, Marco Bortolin

// Based on MAME's "lib/formats/td0_dsk.h"

#ifndef IBMULATOR_HW_FLOPPYFMT_TD0_H
#define IBMULATOR_HW_FLOPPYFMT_TD0_H

#include "floppyfmt.h"
#include "floppydisk_raw.h"

class FloppyFmt_TD0 : public FloppyFmt
{
public:
	FloppyFmt_TD0() {}

	const char *name() const { return "TD0"; }
	const char *description() const { return "TD0 (TeleDisk)(*.td0)"; }
	const char *default_file_extension() const { return ".td0"; }
	std::vector<const char *> file_extensions() const { return {".td0"}; }
	bool can_save() const  { return false; }
	FloppyFmt *create() const { return new FloppyFmt_TD0(); }

	FloppyDisk::Properties identify(std::string file_path, uint64_t file_size,
			FloppyDisk::Size _disk_size);

	bool load(std::ifstream &_file, FloppyDisk &_disk);

	MediumInfoData get_preview_string(std::string _filepath);

protected:
	struct Header {
		uint16_t sig;     // 0-1   Signature               (2 bytes)
		uint8_t  seq;     // 2     Sequence                (1 byte)
		uint8_t  check;   // 3     Checksequence           (1 byte)
		uint8_t  ver;     // 4     Teledisk version        (1 byte)
		uint8_t  drate;   // 5     Data rate               (1 byte)
		uint8_t  drvtype; // 6     Drive type              (1 byte)
		uint8_t  step;    // 7     Stepping                (1 byte)
		uint8_t  dosall;  // 8     DOS allocation flag     (1 byte)
		uint8_t  sides;   // 9     Sides                   (1 byte)
		uint16_t crc;     // 10-11 Cyclic Redundancy Check (2 bytes)

		bool is_single_density() const {
			return drate & 0x80;
		}
		bool has_comment_block() const {
			return step & 0x80;
		}
	} GCC_ATTRIBUTE(packed);

	struct CommentBlock {
		uint16_t crc;     // 0-1 Cyclic Redundancy Check (2 bytes)
		uint16_t datalen; // 2-3 Data length             (2 bytes)
		uint8_t  year;    // 4 Year since 1900           (1 byte)
		uint8_t  month;   // 5 Month                     (1 byte)
		uint8_t  day;     // 6 Day                       (1 byte)
		uint8_t  hour;    // 7 Hour                      (1 byte)
		uint8_t  min;     // 8 Minite                    (1 byte)
		uint8_t  sec;     // 9 Second                    (1 byte)
	} GCC_ATTRIBUTE(packed);

	Header m_header = {};
	bool m_adv_comp = false;

	bool load_raw(std::ifstream &_file, FloppyDisk_Raw &_disk);
	bool load_flux(std::ifstream &_file, FloppyDisk &_disk);

	#define TD0_BUFSZ     512     // new input buffer

	// LZSS Parameters
	#define TD0_N         4096  // Size of string buffer
	#define TD0_F         60    // Size of look-ahead buffer
	#define TD0_THRESHOLD 2
	#define TD0_NIL       TD0_N // End of tree's node

	// Huffman coding parameters
	#define TD0_N_CHAR    (256 - TD0_THRESHOLD + TD0_F) // character code (= 0..N_CHAR-1)
	#define TD0_T         (TD0_N_CHAR * 2 - 1) // Size of table
	#define TD0_R         (TD0_T - 1) // root position
	#define TD0_MAX_FREQ  0x8000 // update when cumulative frequency reaches to this value

	struct td0dsk_t
	{
	public:
		td0dsk_t(std::ifstream *f);

		void set_floppy_file_offset(uint64_t o) { floppy_file_offset = o; }

		void init_Decode();
		int Decode(uint8_t *buf, int len);

	private:
		std::ifstream *floppy_file = nullptr;
		uint64_t floppy_file_size = 0;
		uint64_t floppy_file_offset = 0;

		struct tdlzhuf {
			uint16_t r = 0;
			uint16_t bufcnt = 0, bufndx = 0, bufpos = 0;  // string buffer
			// the following to allow block reads from input in next_word()
			uint16_t ibufcnt = 0, ibufndx = 0; // input buffer counters
			uint8_t  inbuf[TD0_BUFSZ]{};    // input buffer
		} tdctl;
		uint8_t text_buf[TD0_N + TD0_F - 1];
		uint16_t freq[TD0_T + 1]; // cumulative freq table
		int16_t prnt[TD0_T + TD0_N_CHAR]; // pointing parent nodes. area [T..(T + N_CHAR - 1)] are pointers for leaves
		int16_t son[TD0_T + 1]; // pointing children nodes (son[], son[] + 1)

		uint16_t getbuf = 0;
		uint8_t getlen = 0;

		int data_read(uint8_t *buf, uint16_t size);
		int next_word();
		int GetBit();
		int GetByte();
		void StartHuff();
		void reconst();
		void update(int c);
		int16_t DecodeChar();
		int16_t DecodePosition();
	};
};

#endif