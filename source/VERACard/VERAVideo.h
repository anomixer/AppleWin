/*
  VERAVideo.h - Commander X16 VERA video core (Vera Video)

  Port of apple2ts's src/worker/devices/vera/video.ts
  (Commander X16 Emulator: (c) 2019 Michael Steil, (c) 2020 Frank van den Hoef,
   TypeScript port & mods by Michael Morrison) to C++ for AppleWin.

  License: 2-clause BSD
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "VERAAudio.h"	// For PSG/PCM integration hooks

class VERAVideo
{
public:
	VERAVideo();
	~VERAVideo();

	void Reset();
	bool Init();

	// Called each time the 6502 has executed some cycles (see VERACard::Update).
	// Returns true when a new frame has completed.
	bool Step(int mhz, int steps, bool midline);
	void Update();

	bool GetIRQOut();
	uint8_t Read(uint8_t reg, bool debugOn);
	void Write(uint8_t reg, uint8_t value);

	uint8_t* GetFramebuffer() { return m_framebuffer.data(); }
	int GetFramebufferWidth() const { return SCREEN_WIDTH; }
	int GetFramebufferHeight() const { return SCREEN_HEIGHT; }

	// Video RAM (128KB) access for save-state
	uint8_t* GetVideoRAM() { return m_video_ram.data(); }
	size_t GetVideoRAMSize() const { return m_video_ram.size(); }
	uint32_t GetVideoRAMAddrMask() const { return 0x1FFFF; }

	void SetAudio(VERAAudio* pAudio) { m_pAudio = pAudio; }

	// Register-state serialization for save-state (fixed-size byte buffer).
	static unsigned int GetRegisterStateSize();
	void SerializeRegisters(std::vector<uint8_t>& out) const;
	void DeserializeRegisters(const std::vector<uint8_t>& in);

	// True when video output (out_mode) is enabled
	bool IsVideoOutputEnabled() const { return (m_reg_composer[0] & 3) != 0; }

	// Reset the scan position so each frame starts at the top; prevents drift.
	void ResetScanPosition() { m_vga_scan_pos_x = 0; m_vga_scan_pos_y = 0; m_ntsc_scan_pos_y = 0; }

private:
	// ---- Constants ----
	static const int VERA_VERSION_MAJOR = 47;
	static const int VERA_VERSION_MINOR = 0;
	static const int VERA_VERSION_PATCH = 2;
	static const int ADDR_VRAM_START = 0x00000;
	static const int ADDR_VRAM_END = 0x20000;
	static const int ADDR_PSG_START = 0x1F9C0;
	static const int ADDR_PSG_END = 0x1FA00;
	static const int ADDR_PALETTE_START = 0x1FA00;
	static const int ADDR_PALETTE_END = 0x1FC00;
	static const int ADDR_SPRDATA_START = 0x1FC00;
	static const int ADDR_SPRDATA_END = 0x20000;
	static const int NUM_SPRITES = 128;
	static const int SCAN_HEIGHT = 525;
	static constexpr double PIXEL_FREQ = 25.0;
	static const int VGA_SCAN_WIDTH = 800;
	static const int VGA_Y_OFFSET = 0;
	static const int NTSC_HALF_SCAN_WIDTH = 794;
	static const int NTSC_Y_OFFSET_LOW = 42;
	static const int NTSC_Y_OFFSET_HIGH = 568;
	static constexpr double TITLE_SAFE_X = 0.067;
	static constexpr double TITLE_SAFE_Y = 0.05;
	static const int SCREEN_WIDTH = 640;
	static const int SCREEN_HEIGHT = 480;
	static const int NUM_LAYERS = 2;
	static const int COMPOSER_SLOTS = 4 * 64;	// 256
	static const int MHZ = 1;

	// ---- Layer properties ----
	struct LayerProps
	{
		int color_depth;
		uint32_t map_base;
		uint32_t tile_base;
		bool text_mode;
		bool text_mode_256c;
		bool tile_mode;
		bool bitmap_mode;
		int hscroll;
		int vscroll;
		int mapw_log2;
		int maph_log2;
		int tilew;
		int tileh;
		int tilew_log2;
		int tileh_log2;
		int mapw_max;
		int maph_max;
		int tilew_max;
		int tileh_max;
		int layerw_max;
		int layerh_max;
		int tile_size_log2;
		int min_eff_x;
		int max_eff_x;
		int bits_per_pixel;
		int first_color_pos;
		int color_mask;
		int color_fields_max;
	};

	// ---- Sprite properties ----
	struct SpriteProps
	{
		int sprite_zdepth;
		int sprite_collision_mask;
		int sprite_x;
		int sprite_y;
		int sprite_width_log2;
		int sprite_height_log2;
		int sprite_width;
		int sprite_height;
		bool hflip;
		bool vflip;
		int color_mode;
		uint32_t sprite_address;
		int palette_offset;
	};

	struct VideoPalette
	{
		uint32_t entries[256];
		bool dirty;
	};

	// ---- Helpers ----
	static int calc_layer_eff_x(const LayerProps& p, int x);
	static int calc_layer_eff_y(const LayerProps& p, int y);
	static int calc_layer_map_addr_base2(const LayerProps& p, int eff_x, int eff_y);
	void refresh_layer_properties(int layer);
	void refresh_sprite_properties(int sprite);
	void refresh_palette();
	void expand_4bpp_data(uint8_t* dst, uint32_t src_addr, int dst_size);
	void render_sprite_line(int y);
	void render_layer_line_text(int layer, int y);
	void render_layer_line_tile(int layer, int y);
	void render_layer_line_bitmap(int layer, int y);
	int calculate_line_col_index(int spr_zindex, int spr_col_index, int l1_col_index, int l2_col_index);
	void render_line(int y, int scan_pos_x);
	void update_isr_and_coll(int y, int compare);

	uint32_t get_and_inc_address(int sel, bool write);
	void fx_affine_prefetch();
	void write_psg(uint32_t address, uint8_t value);
	void write_pcm(int reg, uint8_t value);

	uint8_t video_space_read(uint32_t address);
	void video_space_read_range(uint8_t* dest, uint32_t address, uint32_t size);
	void fx_video_space_write(uint32_t address, bool nibble, uint8_t value);
	void fx_vram_cache_write(uint32_t address, uint8_t value, uint8_t mask);
	uint8_t video_get_dc_value(int reg);
	void check_not_readonly(uint8_t reg);
	void check_not_writeonly(uint8_t reg);

	// ---- State ----
	std::vector<uint8_t> m_video_ram;			// 0x20000
	std::vector<uint8_t> m_palette;				// 512
	std::vector<std::vector<uint8_t>> m_sprite_data;	// 128 x 8
	uint32_t m_io_addr[2];
	uint8_t m_io_rddata[2];
	uint8_t m_io_inc[2];
	int m_io_addrsel;
	int m_io_dcsel;
	int m_ien;
	int m_isr;
	int m_irq_line;
	uint8_t m_reg_layer[2][7];
	uint8_t m_reg_composer[COMPOSER_SLOTS];
	uint8_t m_prev_reg_composer[2][COMPOSER_SLOTS];
	uint8_t m_layer_line[2][SCREEN_WIDTH];
	uint8_t m_sprite_line_col[SCREEN_WIDTH];
	uint8_t m_sprite_line_z[SCREEN_WIDTH];
	uint8_t m_sprite_line_mask[SCREEN_WIDTH];
	int m_sprite_line_collisions;
	uint8_t m_layer_line_enable[2];
	uint8_t m_old_layer_line_enable[2];
	bool m_sprite_line_enable;
	bool m_old_sprite_line_enable;

	// FX registers
	int m_fx_addr1_mode;
	int32_t m_fx_x_pixel_increment;
	int32_t m_fx_y_pixel_increment;
	int32_t m_fx_x_pixel_position;
	int32_t m_fx_y_pixel_position;
	int m_fx_poly_fill_length;
	uint32_t m_fx_affine_tile_base;
	uint32_t m_fx_affine_map_base;
	int m_fx_affine_map_size;
	bool m_fx_4bit_mode;
	bool m_fx_16bit_hop;
	bool m_fx_cache_byte_cycling;
	bool m_fx_cache_fill;
	bool m_fx_cache_write;
	bool m_fx_trans_writes;
	bool m_fx_2bit_poly;
	bool m_fx_2bit_poking;
	bool m_fx_cache_increment_mode;
	bool m_fx_cache_nibble_index;
	int m_fx_cache_byte_index;
	bool m_fx_multiplier;
	bool m_fx_subtract;
	bool m_fx_affine_clip;
	int m_fx_16bit_hop_align;
	uint8_t m_fx_nibble_bit[2];
	uint8_t m_fx_nibble_incr[2];
	uint8_t m_fx_cache[4];
	int32_t m_fx_mult_accumulator;

	uint8_t m_vera_version_string[4];
	uint8_t m_col_line[SCREEN_WIDTH];
	uint8_t m_unpacked_sprite_line[64];
	int m_vga_scan_pos_x;
	int m_vga_scan_pos_y;
	int m_ntsc_half_cnt;
	int m_ntsc_scan_pos_y;
	int m_frame_count;
	std::vector<uint8_t> m_framebuffer;		// SCREEN_WIDTH*SCREEN_HEIGHT*4
	uint16_t m_default_palette[256];

	LayerProps m_layer_properties[NUM_LAYERS];
	LayerProps m_prev_layer_properties[2][NUM_LAYERS];
	SpriteProps m_sprite_properties[NUM_SPRITES];
	VideoPalette m_video_palette;

	int m_y_prev;
	int m_s_pos_x_p;
	int32_t m_eff_y_fp;
	int32_t m_eff_x_fp;

	// Audio hook
	VERAAudio* m_pAudio;
};
