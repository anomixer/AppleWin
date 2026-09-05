/*
  VERAVideo.cpp - Commander X16 VERA video core (Vera Video)

  Port of apple2ts's src/worker/devices/vera/video.ts
  (Commander X16 Emulator: (c) 2019 Michael Steil, (c) 2020 Frank van den Hoef,
   TypeScript port & mods by Michael Morrison) to C++ for AppleWin.

  License: 2-clause BSD
*/
#include "VERAVideo.h"

#include <climits>
#include <cmath>
#include <cstring>

// The default 256-colour palette (16-bit RGB565)
static const uint16_t s_defaultPalette[256] = {
0x000,0xfff,0x800,0xafe,0xc4c,0x0c5,0x00a,0xee7,0xd85,0x640,0xf77,0x333,0x777,0xaf6,0x08f,0xbbb,0x000,0x111,0x222,0x333,0x444,0x555,0x666,0x777,0x888,0x999,0xaaa,0xbbb,0xccc,0xddd,0xeee,0xfff,0x211,0x433,0x644,0x866,0xa88,0xc99,0xfbb,0x211,0x422,0x633,0x844,0xa55,0xc66,0xf77,0x200,0x411,0x611,0x822,0xa22,0xc33,0xf33,0x200,0x400,0x600,0x800,0xa00,0xc00,0xf00,0x221,0x443,0x664,0x886,0xaa8,0xcc9,0xfeb,0x211,0x432,0x653,0x874,0xa95,0xcb6,0xfd7,0x210,0x431,0x651,0x862,0xa82,0xca3,0xfc3,0x210,0x430,0x640,0x860,0xa80,0xc90,0xfb0,0x121,0x343,0x564,0x786,0x9a8,0xbc9,0xdfb,0x121,0x342,0x463,0x684,0x8a5,0x9c6,0xbf7,0x120,0x241,0x461,0x582,0x6a2,0x8c3,0x9f3,0x120,0x240,0x360,0x480,0x5a0,0x6c0,0x7f0,0x121,0x343,0x465,0x686,0x8a8,0x9ca,0xbfc,0x121,0x242,0x364,0x485,0x5a6,0x6c8,0x7f9,0x020,0x141,0x162,0x283,0x2a4,0x3c5,0x3f6,0x020,0x041,0x061,0x082,0x0a2,0x0c3,0x0f3,0x122,0x344,0x466,0x688,0x8aa,0x9cc,0xbff,0x122,0x244,0x366,0x488,0x5aa,0x6cc,0x7ff,0x022,0x144,0x166,0x288,0x2aa,0x3cc,0x3ff,0x022,0x044,0x066,0x088,0x0aa,0x0cc,0x0ff,0x112,0x334,0x456,0x668,0x88a,0x9ac,0xbcf,0x112,0x224,0x346,0x458,0x56a,0x68c,0x79f,0x002,0x114,0x126,0x238,0x24a,0x35c,0x36f,0x002,0x014,0x016,0x028,0x02a,0x03c,0x03f,0x112,0x334,0x546,0x768,0x98a,0xb9c,0xdbf,0x112,0x324,0x436,0x648,0x85a,0x96c,0xb7f,0x102,0x214,0x416,0x528,0x62a,0x83c,0x93f,0x102,0x204,0x306,0x408,0x50a,0x60c,0x70f,0x212,0x434,0x646,0x868,0xa8a,0xc9c,0xfbe,0x211,0x423,0x635,0x847,0xa59,0xc6b,0xf7d,0x201,0x413,0x615,0x826,0xa28,0xc3a,0xf3c,0x201,0x403,0x604,0x806,0xa08,0xc09,0xf0b
};

// Address increment table (signed 16-bit)
static const int16_t s_increments[32] = {
	0,   0,
	1,   -1,
	2,   -2,
	4,   -4,
	8,   -8,
	16,  -16,
	32,  -32,
	64,  -64,
	128, -128,
	256, -256,
	512, -512,
	40,  -40,
	80,  -80,
	160, -160,
	320, -320,
	640, -640,
};

VERAVideo::VERAVideo()
	: m_video_ram(0x20000)
	, m_palette(512)
	, m_framebuffer(SCREEN_WIDTH * SCREEN_HEIGHT * 4)
	, m_pAudio(nullptr)
{
	m_sprite_data.resize(NUM_SPRITES);
	for (int i = 0; i < NUM_SPRITES; i++)
		m_sprite_data[i].resize(8);

	memcpy(m_default_palette, s_defaultPalette, sizeof(m_default_palette));
	m_vera_version_string[0] = 'V';
	m_vera_version_string[1] = VERA_VERSION_MAJOR;
	m_vera_version_string[2] = VERA_VERSION_MINOR;
	m_vera_version_string[3] = VERA_VERSION_PATCH;

	Reset();
}

VERAVideo::~VERAVideo()
{
}

void VERAVideo::Reset()
{
	// init I/O registers
	memset(m_io_addr, 0, sizeof(m_io_addr));
	memset(m_io_inc, 0, sizeof(m_io_inc));
	m_io_addrsel = 0;
	m_io_dcsel = 0;
	memset(m_io_rddata, 0, sizeof(m_io_rddata));
	m_ien = 0;
	m_isr = 0;
	m_irq_line = 0;
	// init Layer registers
	memset(m_reg_layer, 0, sizeof(m_reg_layer));
	// init composer registers
	memset(m_reg_composer, 0, sizeof(m_reg_composer));
	m_reg_composer[1] = 128; // hscale = 1.0
	m_reg_composer[2] = 128; // vscale = 1.0
	m_reg_composer[5] = 640 >> 2;
	m_reg_composer[7] = 480 >> 1;
	// Initialize FX registers
	m_fx_addr1_mode = 0;
	m_fx_x_pixel_position = 0x8000;
	m_fx_y_pixel_position = 0x8000;
	m_fx_x_pixel_increment = 0;
	m_fx_y_pixel_increment = 0;
	m_fx_cache_write = false;
	m_fx_cache_fill = false;
	m_fx_4bit_mode = false;
	m_fx_16bit_hop = false;
	m_fx_subtract = false;
	m_fx_cache_byte_cycling = false;
	m_fx_trans_writes = false;
	m_fx_multiplier = false;
	m_fx_mult_accumulator = 0;
	m_fx_2bit_poly = false;
	m_fx_2bit_poking = false;
	m_fx_cache_nibble_index = false;
	m_fx_cache_byte_index = 0;
	m_fx_cache_increment_mode = false;
	memset(m_fx_cache, 0, sizeof(m_fx_cache));
	m_fx_16bit_hop_align = 0;
	memset(m_fx_nibble_bit, 0, sizeof(m_fx_nibble_bit));
	memset(m_fx_nibble_incr, 0, sizeof(m_fx_nibble_incr));
	m_fx_poly_fill_length = 0;
	m_fx_affine_tile_base = 0;
	m_fx_affine_map_base = 0;
	m_fx_affine_map_size = 2;
	m_fx_affine_clip = false;
	// init sprite data
	for (int i = 0; i < NUM_SPRITES; i++)
		memset(m_sprite_data[i].data(), 0, 8);
	// copy palette
	for (int i = 0; i < 256; i++)
	{
		m_palette[i * 2 + 0] = m_default_palette[i] & 0xff;
		m_palette[i * 2 + 1] = m_default_palette[i] >> 8;
	}

	refresh_palette();
	// fill video RAM with pseudo-random data (deterministic-ish)
	for (size_t i = 0; i < m_video_ram.size(); i++)
		m_video_ram[i] = static_cast<uint8_t>((i * 7) % 256);

	m_sprite_line_collisions = 0;
	m_vga_scan_pos_x = 0;
	m_vga_scan_pos_y = 0;
	m_ntsc_half_cnt = 0;
	m_ntsc_scan_pos_y = 0;

	memset(m_prev_reg_composer, 0, sizeof(m_prev_reg_composer));
	memset(m_prev_layer_properties, 0, sizeof(m_prev_layer_properties));
	memset(m_layer_line_enable, 0, sizeof(m_layer_line_enable));
	memset(m_old_layer_line_enable, 0, sizeof(m_old_layer_line_enable));
	m_sprite_line_enable = false;
	m_old_sprite_line_enable = false;
	m_y_prev = -1;
	m_s_pos_x_p = 0;
	m_eff_y_fp = 0;
	m_eff_x_fp = 0;
	m_frame_count = 0;

	if (m_pAudio)
		m_pAudio->Reset();
}

bool VERAVideo::Init()
{
	Reset();
	return true;
}

// ---- Helpers ----

int VERAVideo::calc_layer_eff_x(const LayerProps& p, int x)
{
	return (x + p.hscroll) & p.layerw_max;
}

int VERAVideo::calc_layer_eff_y(const LayerProps& p, int y)
{
	return (y + p.vscroll) & p.layerh_max;
}

int VERAVideo::calc_layer_map_addr_base2(const LayerProps& p, int eff_x, int eff_y)
{
	return static_cast<int>(p.map_base + ((((eff_y >> p.tileh_log2) << p.mapw_log2) + (eff_x >> p.tilew_log2)) << 1));
}

void VERAVideo::refresh_layer_properties(int layer)
{
	LayerProps& props = m_layer_properties[layer];
	const int prev_layerw_max = props.layerw_max;
	const int prev_hscroll = props.hscroll;
	props.color_depth = m_reg_layer[layer][0] & 0x3;
	props.map_base = static_cast<uint32_t>(m_reg_layer[layer][1]) << 9;
	props.tile_base = static_cast<uint32_t>(m_reg_layer[layer][2] & 0xFC) << 9;
	props.bitmap_mode = (m_reg_layer[layer][0] & 0x4) != 0;
	props.text_mode = (props.color_depth == 0) && !props.bitmap_mode;
	props.text_mode_256c = (m_reg_layer[layer][0] & 8) != 0;
	props.tile_mode = !props.bitmap_mode && !props.text_mode;
	if (!props.bitmap_mode)
	{
		props.hscroll = m_reg_layer[layer][3] | ((m_reg_layer[layer][4] & 0xf) << 8);
		props.vscroll = m_reg_layer[layer][5] | ((m_reg_layer[layer][6] & 0xf) << 8);
	}
	else
	{
		props.hscroll = 0;
		props.vscroll = 0;
	}

	int mapw = 0;
	int maph = 0;
	props.tilew = 0;
	props.tileh = 0;
	if (props.tile_mode || props.text_mode)
	{
		props.mapw_log2 = 5 + ((m_reg_layer[layer][0] >> 4) & 3);
		props.maph_log2 = 5 + ((m_reg_layer[layer][0] >> 6) & 3);
		mapw = 1 << props.mapw_log2;
		maph = 1 << props.maph_log2;
		props.tilew_log2 = 3 + (m_reg_layer[layer][2] & 1);
		props.tileh_log2 = 3 + ((m_reg_layer[layer][2] >> 1) & 1);
		props.tilew = 1 << props.tilew_log2;
		props.tileh = 1 << props.tileh_log2;
	}
	else if (props.bitmap_mode)
	{
		props.tilew = (m_reg_layer[layer][2] & 1) ? 640 : 320;
		props.tileh = SCREEN_HEIGHT;
	}

	props.mapw_max = mapw - 1;
	props.maph_max = maph - 1;
	props.tilew_max = props.tilew - 1;
	props.tileh_max = props.tileh - 1;
	props.layerw_max = (mapw * props.tilew) - 1;
	props.layerh_max = (maph * props.tileh) - 1;

	if (prev_layerw_max != props.layerw_max || prev_hscroll != props.hscroll)
	{
		int min_eff_x = INT_MAX;
		int max_eff_x = INT_MIN;
		for (int x = 0; x < SCREEN_WIDTH; ++x)
		{
			const int eff_x = calc_layer_eff_x(props, x);
			if (eff_x < min_eff_x) min_eff_x = eff_x;
			if (eff_x > max_eff_x) max_eff_x = eff_x;
		}
		props.min_eff_x = min_eff_x;
		props.max_eff_x = max_eff_x;
	}

	props.bits_per_pixel = 1 << props.color_depth;
	props.tile_size_log2 = props.tilew_log2 + props.tileh_log2 + props.color_depth - 3;
	props.first_color_pos = 8 - props.bits_per_pixel;
	props.color_mask = (1 << props.bits_per_pixel) - 1;
	props.color_fields_max = (8 >> props.color_depth) - 1;
}

void VERAVideo::refresh_sprite_properties(int sprite)
{
	SpriteProps& props = m_sprite_properties[sprite];
	uint8_t* sd = m_sprite_data[sprite].data();
	props.sprite_zdepth = (sd[6] >> 2) & 3;
	props.sprite_collision_mask = sd[6] & 0xf0;
	props.sprite_x = sd[2] | ((sd[3] & 3) << 8);
	props.sprite_y = sd[4] | ((sd[5] & 3) << 8);
	props.sprite_width_log2 = (((sd[7] >> 4) & 3) + 3);
	props.sprite_height_log2 = ((sd[7] >> 6) + 3);
	props.sprite_width = 1 << props.sprite_width_log2;
	props.sprite_height = 1 << props.sprite_height_log2;
	// fix up negative coordinates
	if (props.sprite_x >= 0x400 - props.sprite_width)
		props.sprite_x -= 0x400;
	if (props.sprite_y >= 0x400 - props.sprite_height)
		props.sprite_y -= 0x400;

	props.hflip = (sd[6] & 1) != 0;
	props.vflip = ((sd[6] >> 1) & 1) != 0;
	props.color_mode = (sd[1] >> 7) & 1;
	props.sprite_address = static_cast<uint32_t>(sd[0]) << 5 | (static_cast<uint32_t>(sd[1] & 0xf) << 13);
	props.palette_offset = (sd[7] & 0x0f) << 4;
}

void VERAVideo::refresh_palette()
{
	const int out_mode = m_reg_composer[0] & 3;
	const bool chroma_disable = ((m_reg_composer[0] & 0x07) == 6);
	for (int i = 0; i < 256; ++i)
	{
		int r = 0, g = 0, b = 0;
		if (out_mode == 0)
		{
			r = 0; g = 0; b = 255;	// blue screen
		}
		else
		{
			const int entry = m_palette[i * 2] | (m_palette[i * 2 + 1] << 8);
			r = ((entry >> 8) & 0xf) << 4 | ((entry >> 8) & 0xf);
			g = ((entry >> 4) & 0xf) << 4 | ((entry >> 4) & 0xf);
			b = (entry & 0xf) << 4 | (entry & 0xf);
			if (chroma_disable)
				r = g = b = (r + b + g) / 3;
		}
		m_video_palette.entries[i] = static_cast<uint32_t>(r << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
	}
	m_video_palette.dirty = false;
}

void VERAVideo::expand_4bpp_data(uint8_t* dst, uint32_t src_addr, int dst_size)
{
	int dst_idx = 0;
	uint32_t src_idx = src_addr;
	while (dst_size >= 2)
	{
		const uint8_t val = video_space_read(src_idx++);
		dst[dst_idx++] = val >> 4;
		dst[dst_idx++] = val & 0xf;
		dst_size -= 2;
	}
}

void VERAVideo::render_sprite_line(int y)
{
	memset(m_sprite_line_col, 0, SCREEN_WIDTH);
	memset(m_sprite_line_z, 0, SCREEN_WIDTH);
	memset(m_sprite_line_mask, 0, SCREEN_WIDTH);
	int sprite_budget = 800 + 1;
	for (int i = 0; i < NUM_SPRITES; i++)
	{
		// one clock per lookup
		if (--sprite_budget == 0) break;
		const SpriteProps& props = m_sprite_properties[i];
		if (props.sprite_zdepth == 0)
			continue;

		// check whether this line falls within the sprite
		if (y < props.sprite_y || y >= props.sprite_y + props.sprite_height)
			continue;

		const int eff_sy = props.vflip ? ((props.sprite_height - 1) - (y - props.sprite_y)) : (y - props.sprite_y);
		int eff_sx = props.hflip ? (props.sprite_width - 1) : 0;
		const int eff_sx_incr = props.hflip ? -1 : 1;
		const uint32_t bitmap_data = props.sprite_address + (eff_sy << (props.sprite_width_log2 - (1 - props.color_mode)));
		const int width = (props.sprite_width < 64 ? props.sprite_width : 64);
		const int vram_fetch_mask = ((2 - props.color_mode) << 2) - 1;
		if (props.color_mode == 0)
		{
			// 4bpp
			expand_4bpp_data(m_unpacked_sprite_line, bitmap_data, width);
		}
		else
		{
			// 8bpp
			for (int k = 0; k < width; k++)
				m_unpacked_sprite_line[k] = video_space_read(bitmap_data + k);
		}

		for (int sx = 0; sx < props.sprite_width; ++sx)
		{
			const int line_x = props.sprite_x + sx;
			// line_x can be negative when sprite_x is negative (JS typed arrays no-op;
			// C++ would index out of bounds).
			if (line_x < 0 || line_x >= SCREEN_WIDTH)
			{
				eff_sx += eff_sx_incr;
				continue;
			}

			// one clock per fetched 32 bits
			if (!(sx & vram_fetch_mask))
			{
				if (--sprite_budget == 0) break;
			}

			// one clock per rendered pixel
			if (--sprite_budget == 0) break;
			int col_index = m_unpacked_sprite_line[eff_sx];
			eff_sx += eff_sx_incr;
			// palette offset
			if (col_index > 0)
			{
				m_sprite_line_collisions |= m_sprite_line_mask[line_x] & props.sprite_collision_mask;
				m_sprite_line_mask[line_x] |= props.sprite_collision_mask;
				if (props.sprite_zdepth > m_sprite_line_z[line_x])
				{
					if (col_index < 16)
						col_index += props.palette_offset;
					m_sprite_line_col[line_x] = static_cast<uint8_t>(col_index);
					m_sprite_line_z[line_x] = static_cast<uint8_t>(props.sprite_zdepth);
				}
			}
		}
	}
}

void VERAVideo::render_layer_line_text(int layer, int y)
{
	const LayerProps& props = m_prev_layer_properties[1][layer];
	const LayerProps& props0 = m_prev_layer_properties[0][layer];
	const int max_pixels_per_byte = (8 >> props.color_depth) - 1;
	const int eff_y = calc_layer_eff_y(props0, y);
	const int yy = eff_y & props.tileh_max;
	// additional bytes to reach the correct line of the tile
	const int y_add = (yy << props.tilew_log2) >> 3;
	const int map_addr_begin = calc_layer_map_addr_base2(props, props.min_eff_x, eff_y);
	const int map_addr_end = calc_layer_map_addr_base2(props, props.max_eff_x, eff_y);
	const int size = (map_addr_end - map_addr_begin) + 2;
	uint8_t tile_bytes[512];
	video_space_read_range(tile_bytes, static_cast<uint32_t>(map_addr_begin), static_cast<uint32_t>(size));

	int tile_start = 0;
	int fg_color = 0;
	int bg_color = 0;
	int s = 0;
	int color_shift = 0;
	{
		const int eff_x = calc_layer_eff_x(props, 0);
		const int xx = eff_x & props.tilew_max;
		const int map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;
		const int tile_index = tile_bytes[map_addr];
		const int byte1 = tile_bytes[map_addr + 1];
		if (!props.text_mode_256c)
		{
			fg_color = byte1 & 15;
			bg_color = byte1 >> 4;
		}
		else
		{
			fg_color = byte1;
			bg_color = 0;
		}
		tile_start = tile_index << props.tile_size_log2;
		const int x_add = xx >> 3;
		const int tile_offset = tile_start + y_add + x_add;
		s = video_space_read(props.tile_base + static_cast<uint32_t>(tile_offset));
		color_shift = max_pixels_per_byte - (xx & 0x7);
	}

	for (int x = 0; x < SCREEN_WIDTH; x++)
	{
		const int eff_x = calc_layer_eff_x(props, x);
		const int xx = eff_x & props.tilew_max;
		if ((eff_x & 0x7) == 0)
		{
			if ((eff_x & props.tilew_max) == 0)
			{
				const int map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;
				const int tile_index = tile_bytes[map_addr];
				const int byte1 = tile_bytes[map_addr + 1];
				if (!props.text_mode_256c)
				{
					fg_color = byte1 & 15;
					bg_color = byte1 >> 4;
				}
				else
				{
					fg_color = byte1;
					bg_color = 0;
				}
				tile_start = tile_index << props.tile_size_log2;
			}
			const int x_add = xx >> 3;
			const int tile_offset = tile_start + y_add + x_add;
			s = video_space_read(props.tile_base + static_cast<uint32_t>(tile_offset));
			color_shift = max_pixels_per_byte;
		}
		const int col_index = (s >> color_shift) & 1;
		--color_shift;
		m_layer_line[layer][x] = static_cast<uint8_t>(col_index ? fg_color : bg_color);
	}
}

void VERAVideo::render_layer_line_tile(int layer, int y)
{
	const LayerProps& props = m_prev_layer_properties[1][layer];
	const LayerProps& props0 = m_prev_layer_properties[0][layer];
	const int max_pixels_per_byte = (8 >> props.color_depth) - 1;
	const int eff_y = calc_layer_eff_y(props0, y);
	const int yy = eff_y & props.tileh_max;
	const int yy_flip = yy ^ props.tileh_max;
	const int y_add = yy << ((props.tilew_log2 + props.color_depth - 3) & 31);
	const int y_add_flip = yy_flip << ((props.tilew_log2 + props.color_depth - 3) & 31);
	const int map_addr_begin = calc_layer_map_addr_base2(props, props.min_eff_x, eff_y);
	const int map_addr_end = calc_layer_map_addr_base2(props, props.max_eff_x, eff_y);
	const int size = (map_addr_end - map_addr_begin) + 2;
	uint8_t tile_bytes[512];
	video_space_read_range(tile_bytes, static_cast<uint32_t>(map_addr_begin), static_cast<uint32_t>(size));

	int palette_offset = 0;
	bool vflip = false;
	bool hflip = false;
	int tile_start = 0;
	int s = 0;
	int color_shift = 0;
	int color_shift_incr = 0;
	{
		const int eff_x = calc_layer_eff_x(props, 0);
		const int map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;
		const int byte0 = tile_bytes[map_addr];
		const int byte1 = tile_bytes[map_addr + 1];
		vflip = ((byte1 >> 3) & 1) != 0;
		hflip = ((byte1 >> 2) & 1) != 0;
		palette_offset = byte1 & 0xf0;
		const int tile_index = byte0 | ((byte1 & 3) << 8);
		tile_start = tile_index << props.tile_size_log2;
		color_shift_incr = hflip ? props.bits_per_pixel : -props.bits_per_pixel;
		int xx = eff_x & props.tilew_max;
		if (hflip)
		{
			xx = xx ^ props.tilew_max;
			color_shift = 0;
		}
		else
		{
			color_shift = props.first_color_pos;
		}
		const int x_add = (xx << props.color_depth) >> 3;
		const int tile_offset = tile_start + (vflip ? y_add_flip : y_add) + x_add;
		s = video_space_read(props.tile_base + static_cast<uint32_t>(tile_offset));
	}

	for (int x = 0; x < SCREEN_WIDTH; x++)
	{
		const int eff_x = calc_layer_eff_x(props, x);
		if ((eff_x & max_pixels_per_byte) == 0)
		{
			if ((eff_x & props.tilew_max) == 0)
			{
				const int map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;
				const int byte0 = tile_bytes[map_addr];
				const int byte1 = tile_bytes[map_addr + 1];
				vflip = ((byte1 >> 3) & 1) != 0;
				hflip = ((byte1 >> 2) & 1) != 0;
				palette_offset = byte1 & 0xf0;
				const int tile_index = byte0 | ((byte1 & 3) << 8);
				tile_start = tile_index << props.tile_size_log2;
				color_shift_incr = hflip ? props.bits_per_pixel : -props.bits_per_pixel;
			}
			int xx = eff_x & props.tilew_max;
			if (hflip)
			{
				xx = xx ^ props.tilew_max;
				color_shift = 0;
			}
			else
			{
				color_shift = props.first_color_pos;
			}
			const int x_add = (xx << props.color_depth) >> 3;
			const int tile_offset = tile_start + (vflip ? y_add_flip : y_add) + x_add;
			s = video_space_read(props.tile_base + static_cast<uint32_t>(tile_offset));
		}

		int col_index = (s >> color_shift) & props.color_mask;
		color_shift += color_shift_incr;
		// Apply Palette Offset
		if (col_index > 0 && col_index < 16)
		{
			col_index += palette_offset;
			if (props.text_mode_256c)
				col_index |= 0x80;
		}
		m_layer_line[layer][x] = static_cast<uint8_t>(col_index);
	}
}

void VERAVideo::render_layer_line_bitmap(int layer, int y)
{
	const LayerProps& props = m_prev_layer_properties[1][layer];
	const int yy = y % props.tileh;
	const int y_add = (yy * props.tilew * props.bits_per_pixel) >> 3;
	for (int x = 0; x < SCREEN_WIDTH; x++)
	{
		const int xx = x % props.tilew;
		const int palette_offset = m_reg_layer[layer][4] & 0xf;
		const int x_add = (xx * props.bits_per_pixel) >> 3;
		const int tile_offset = y_add + x_add;
		const int s = video_space_read(props.tile_base + static_cast<uint32_t>(tile_offset));
		int col_index = (s >> (props.first_color_pos - ((xx & props.color_fields_max) << props.color_depth))) & props.color_mask;
		// Apply Palette Offset
		if (col_index > 0 && col_index < 16)
		{
			col_index += palette_offset << 4;
			if (props.text_mode_256c)
				col_index |= 0x80;
		}
		m_layer_line[layer][x] = static_cast<uint8_t>(col_index);
	}
}

int VERAVideo::calculate_line_col_index(int spr_zindex, int spr_col_index, int l1_col_index, int l2_col_index)
{
	int col_index = 0;
	switch (spr_zindex)
	{
	case 3:
		col_index = spr_col_index ? spr_col_index : (l2_col_index ? l2_col_index : l1_col_index);
		break;
	case 2:
		col_index = l2_col_index ? l2_col_index : (spr_col_index ? spr_col_index : l1_col_index);
		break;
	case 1:
		col_index = l2_col_index ? l2_col_index : (l1_col_index ? l1_col_index : spr_col_index);
		break;
	case 0:
		col_index = l2_col_index ? l2_col_index : l1_col_index;
		break;
	}
	return col_index;
}

void VERAVideo::render_line(int y, int scan_pos_x)
{
	const int dc_video = m_reg_composer[0];
	const int vstart = m_reg_composer[6] << 1;
	const int vstop = m_reg_composer[7] << 1;
	if (y != m_y_prev)
	{
		m_y_prev = y;
		m_s_pos_x_p = 0;
		// Copy the composer array to 2-line history buffer
		memcpy(m_prev_reg_composer[1], m_prev_reg_composer[0], 1 * COMPOSER_SLOTS);
		memcpy(m_prev_reg_composer[0], m_reg_composer, 1 * COMPOSER_SLOTS);
		// Same with the layer properties
		memcpy(m_prev_layer_properties[1], m_prev_layer_properties[0], NUM_LAYERS * sizeof(LayerProps));
		memcpy(m_prev_layer_properties[0], m_layer_properties, NUM_LAYERS * sizeof(LayerProps));
		if ((dc_video & 3) > 1) // 480i or 240p
		{
			if ((y >> 1) == 0)
				m_eff_y_fp = y * (m_prev_reg_composer[1][2] << 9);
			else if (((y & 0xfffe) >= vstart) && ((y & 0xfffe) < vstop))
				m_eff_y_fp += (m_prev_reg_composer[1][2] << 10);
		}
		else
		{
			if (y == 0)
				m_eff_y_fp = 0;
			else if ((y >= vstart) && (y < vstop))
				m_eff_y_fp += (m_prev_reg_composer[1][2] << 9);
		}
	}

	if ((dc_video & 8) && (dc_video & 3) > 1) // progressive NTSC/RGB mode
		y &= 0xfffe;

	// refresh palette for next entry
	if (m_video_palette.dirty)
		refresh_palette();

	if (y >= SCREEN_HEIGHT)
		return;

	int s_pos_x = static_cast<int>(std::round(scan_pos_x));
	if (s_pos_x > SCREEN_WIDTH)
		s_pos_x = SCREEN_WIDTH;
	if (s_pos_x < 0)
		s_pos_x = 0;

	// scan_pos_x can go negative (e.g. after a ULONG cycle underflow); clamp so we
	// never index the framebuffer with a negative x.
	if (m_s_pos_x_p < 0)
		m_s_pos_x_p = 0;
	if (m_s_pos_x_p == 0)
		m_eff_x_fp = 0;

	const int out_mode = m_reg_composer[0] & 3;
	const int border_color = m_reg_composer[3];
	int hstart = m_reg_composer[4] << 2;
	int hstop = m_reg_composer[5] << 2;
	int eff_y = m_eff_y_fp >> 16;
	if (eff_y >= 480)
		eff_y = 480 - (y & 1);
	m_layer_line_enable[0] = (dc_video & 0x10) ? 1 : 0;
	m_layer_line_enable[1] = (dc_video & 0x20) ? 1 : 0;
	m_sprite_line_enable = (dc_video & 0x40) != 0;

	// clear layer_line if layer gets disabled
	for (int layer = 0; layer < 2; layer++)
	{
		if (!m_layer_line_enable[layer] && m_old_layer_line_enable[layer])
		{
			for (int i = m_s_pos_x_p; i < SCREEN_WIDTH; i++)
				m_layer_line[layer][i] = 0;
		}
		if (m_s_pos_x_p == 0)
			m_old_layer_line_enable[layer] = m_layer_line_enable[layer];
	}

	// clear sprite_line if sprites get disabled
	if (!m_sprite_line_enable && m_old_sprite_line_enable)
	{
		for (int i = m_s_pos_x_p; i < SCREEN_WIDTH; i++)
		{
			m_sprite_line_col[i] = 0;
			m_sprite_line_z[i] = 0;
			m_sprite_line_mask[i] = 0;
		}
	}
	if (m_s_pos_x_p == 0)
		m_old_sprite_line_enable = m_sprite_line_enable;
	if (m_sprite_line_enable)
		render_sprite_line(eff_y);

	if (m_layer_line_enable[0])
	{
		if (m_prev_layer_properties[1][0].text_mode)
			render_layer_line_text(0, eff_y);
		else if (m_prev_layer_properties[1][0].bitmap_mode)
			render_layer_line_bitmap(0, eff_y);
		else
			render_layer_line_tile(0, eff_y);
	}
	if (m_layer_line_enable[1])
	{
		if (m_prev_layer_properties[1][1].text_mode)
			render_layer_line_text(1, eff_y);
		else if (m_prev_layer_properties[1][1].bitmap_mode)
			render_layer_line_bitmap(1, eff_y);
		else
			render_layer_line_tile(1, eff_y);
	}

	// If video output is enabled, calculate color indices for line.
	if (out_mode != 0)
	{
		// Add border after if required.
		if (y < vstart || y >= vstop)
		{
			int border_fill = border_color;
			border_fill = border_fill | (border_fill << 8);
			border_fill = border_fill | (border_fill << 16);
			memset(m_col_line, border_fill & 0xff, SCREEN_WIDTH);
		}
		else
		{
			hstart = hstart < 640 ? hstart : 640;
			hstop = hstop < 640 ? hstop : 640;
			for (int x = m_s_pos_x_p; x < hstart && x < s_pos_x; ++x)
				m_col_line[x] = static_cast<uint8_t>(border_color);

			const int scale = m_reg_composer[1];
			for (int x = (hstart > m_s_pos_x_p ? hstart : m_s_pos_x_p); x < hstop && x < s_pos_x; ++x)
			{
				const int eff_x = m_eff_x_fp >> 16;
				m_col_line[x] = static_cast<uint8_t>((eff_x < SCREEN_WIDTH) ?
					calculate_line_col_index(m_sprite_line_z[eff_x], m_sprite_line_col[eff_x], m_layer_line[0][eff_x], m_layer_line[1][eff_x]) : 0);
				m_eff_x_fp += (scale << 9);
			}
			for (int x = hstop; x < s_pos_x; ++x)
				m_col_line[x] = static_cast<uint8_t>(border_color);
		}
	}

	// Look up all color indices. (y can be negative for NTSC overscan lines;
	// in that case the original JS simply no-ops the framebuffer writes.)
	if (y >= 0 && m_s_pos_x_p >= 0)
	{
		const int fb_idx = (y * SCREEN_WIDTH + m_s_pos_x_p) * 4;
		uint32_t* dst = reinterpret_cast<uint32_t*>(&m_framebuffer[fb_idx]);
		for (int x = m_s_pos_x_p; x < s_pos_x; x++, dst++)
		{
			const uint32_t entry = m_video_palette.entries[m_col_line[x]];
			// Pack RGBA so the little-endian memory layout is R,G,B,A (byte0=R).
			*dst = 0xFF000000 |
				((entry & 0xFF) << 16) |
				(((entry >> 8) & 0xFF) << 8) |
				((entry >> 16) & 0xFF);
		}
	}

	// NTSC overscan
	if (out_mode == 2 && y >= 0 && m_s_pos_x_p >= 0)
	{
		int fb_idx = (y * SCREEN_WIDTH + m_s_pos_x_p) * 4;
		for (int x = m_s_pos_x_p; x < s_pos_x; x++)
		{
			if (x < SCREEN_WIDTH * TITLE_SAFE_X ||
				x > SCREEN_WIDTH * (1 - TITLE_SAFE_X) ||
				y < SCREEN_HEIGHT * TITLE_SAFE_Y ||
				y > SCREEN_HEIGHT * (1 - TITLE_SAFE_Y))
			{
				// Divide RGB elements by 4.
				m_framebuffer[fb_idx] >>= 2;
				m_framebuffer[fb_idx + 1] >>= 2;
				m_framebuffer[fb_idx + 2] >>= 2;
			}
			fb_idx += 4;
		}
	}

	m_s_pos_x_p = s_pos_x;
}

void VERAVideo::update_isr_and_coll(int y, int compare)
{
	if (y == SCREEN_HEIGHT)
	{
		if (m_sprite_line_collisions != 0)
			m_isr |= 4;
		m_isr = (m_isr & 0xf) | m_sprite_line_collisions;
		m_sprite_line_collisions = 0;
		m_isr |= 1; // VSYNC IRQ
	}
	if (y == compare) // LINE IRQ
		m_isr |= 2;
}

bool VERAVideo::Step(int mhz, int steps, bool midline)
{
	int y = 0;
	const bool ntsc_mode = (m_reg_composer[0] & 2) != 0;
	bool new_frame = false;
	m_vga_scan_pos_x += static_cast<int>(PIXEL_FREQ * steps / mhz);
	while (m_vga_scan_pos_x > VGA_SCAN_WIDTH)
	{
		m_vga_scan_pos_x -= VGA_SCAN_WIDTH;
		if (!ntsc_mode)
			render_line(m_vga_scan_pos_y - VGA_Y_OFFSET, VGA_SCAN_WIDTH);
		m_vga_scan_pos_y++;
		if (m_vga_scan_pos_y == SCAN_HEIGHT)
		{
			m_vga_scan_pos_y = 0;
			if (!ntsc_mode)
			{
				new_frame = true;
				m_frame_count++;
			}
		}
		if (!ntsc_mode)
			update_isr_and_coll(m_vga_scan_pos_y - VGA_Y_OFFSET, m_irq_line);
	}
	if (midline)
	{
		if (!ntsc_mode)
			render_line(m_vga_scan_pos_y - VGA_Y_OFFSET, m_vga_scan_pos_x);
	}
	m_ntsc_half_cnt += static_cast<int>(PIXEL_FREQ * steps / mhz);
	while (m_ntsc_half_cnt > NTSC_HALF_SCAN_WIDTH)
	{
		m_ntsc_half_cnt -= NTSC_HALF_SCAN_WIDTH;
		if (ntsc_mode)
		{
			if (m_ntsc_scan_pos_y < SCAN_HEIGHT)
			{
				y = m_ntsc_scan_pos_y - NTSC_Y_OFFSET_LOW;
				if ((y & 1) == 0)
					render_line(y, NTSC_HALF_SCAN_WIDTH);
			}
			else
			{
				y = m_ntsc_scan_pos_y - NTSC_Y_OFFSET_HIGH;
				if ((y & 1) == 0)
					render_line(y | 1, NTSC_HALF_SCAN_WIDTH);
			}
		}
		m_ntsc_scan_pos_y++;
		if (m_ntsc_scan_pos_y == SCAN_HEIGHT)
		{
			m_reg_composer[0] |= 0x80;
			if (ntsc_mode)
			{
				new_frame = true;
				m_frame_count++;
			}
		}
		if (m_ntsc_scan_pos_y == SCAN_HEIGHT * 2)
		{
			m_reg_composer[0] &= ~0x80;
			m_ntsc_scan_pos_y = 0;
			if (ntsc_mode)
			{
				new_frame = true;
				m_frame_count++;
			}
		}
		if (ntsc_mode)
		{
			if (m_ntsc_scan_pos_y < SCAN_HEIGHT)
				update_isr_and_coll(m_ntsc_scan_pos_y - NTSC_Y_OFFSET_LOW, m_irq_line & ~1);
			else
				update_isr_and_coll(m_ntsc_scan_pos_y - NTSC_Y_OFFSET_HIGH, m_irq_line & ~1);
		}
	}
	if (midline)
	{
		if (ntsc_mode)
		{
			if (m_ntsc_scan_pos_y < SCAN_HEIGHT)
			{
				y = m_ntsc_scan_pos_y - NTSC_Y_OFFSET_LOW;
				if ((y & 1) == 0)
					render_line(y, m_ntsc_half_cnt);
			}
			else
			{
				y = m_ntsc_scan_pos_y - NTSC_Y_OFFSET_HIGH;
				if ((y & 1) == 0)
					render_line(y | 1, m_ntsc_half_cnt);
			}
		}
	}

	return new_frame;
}

bool VERAVideo::GetIRQOut()
{
	// audio_render() is handled by the audio subsystem (SoundCore) in AppleWin.
	const int tmp_isr = m_isr | (m_pAudio && m_pAudio->IsFifoAlmostEmpty() ? 8 : 0);
	return (tmp_isr & m_ien) != 0;
}

void VERAVideo::Update()
{
	// Framebuffer is consumed by VERACard for display integration.
	// Nothing extra to do here.
}

// ---- Internal video address space ----

uint8_t VERAVideo::video_space_read(uint32_t address)
{
	return m_video_ram[address & 0x1FFFF];
}

void VERAVideo::video_space_read_range(uint8_t* dest, uint32_t address, uint32_t size)
{
	if (address >= ADDR_VRAM_START && (address + size) <= static_cast<uint32_t>(ADDR_VRAM_END))
	{
		memcpy(dest, &m_video_ram[address], size);
	}
	else
	{
		for (uint32_t i = 0; i < size; ++i)
			dest[i] = video_space_read(address + i);
	}
}

void VERAVideo::write_psg(uint32_t address, uint8_t value)
{
	const uint8_t reg = address & 0x3f;
	if (m_pAudio)
		m_pAudio->WritePSGReg(reg, value);
}

void VERAVideo::write_pcm(int reg, uint8_t value)
{
	if (!m_pAudio)
		return;
	switch (reg)
	{
	case 0: m_pAudio->WritePcmCtrl(value); break;
	case 1: m_pAudio->WritePcmRate(value); break;
	case 2: m_pAudio->WritePcmFifo(value); break;
	}
}

void VERAVideo::fx_video_space_write(uint32_t address, bool nibble, uint8_t value)
{
	if (m_fx_4bit_mode)
	{
		if (nibble)
		{
			if (!m_fx_trans_writes || (value & 0x0f) > 0)
				m_video_ram[address & 0x1FFFF] = (m_video_ram[address & 0x1FFFF] & 0xf0) | (value & 0x0f);
		}
		else
		{
			if (!m_fx_trans_writes || (value & 0xf0) > 0)
				m_video_ram[address & 0x1FFFF] = (m_video_ram[address & 0x1FFFF] & 0x0f) | (value & 0xf0);
		}
	}
	else
	{
		if (!m_fx_trans_writes || value > 0)
			m_video_ram[address & 0x1FFFF] = value;
	}
	if (address >= ADDR_PSG_START && address < ADDR_PSG_END)
	{
		write_psg(address, value);
	}
	else if (address >= ADDR_PALETTE_START && address < ADDR_PALETTE_END)
	{
		m_palette[address & 0x1ff] = value;
		m_video_palette.dirty = true;
	}
	else if (address >= ADDR_SPRDATA_START && address < ADDR_SPRDATA_END)
	{
		const int spr = (address >> 3) & 0x7f;
		m_sprite_data[spr][address & 0x7] = value;
		refresh_sprite_properties(spr);
	}
}

void VERAVideo::fx_vram_cache_write(uint32_t address, uint8_t value, uint8_t mask)
{
	if (!m_fx_trans_writes || value > 0)
	{
		switch (mask)
		{
		case 0:
			m_video_ram[address & 0x1FFFF] = value;
			break;
		case 1:
			m_video_ram[address & 0x1FFFF] = (m_video_ram[address & 0x1FFFF] & 0x0f) | (value & 0xf0);
			break;
		case 2:
			m_video_ram[address & 0x1FFFF] = (m_video_ram[address & 0x1FFFF] & 0xf0) | (value & 0x0f);
			break;
		case 3:
			break;
		}
	}
}

uint8_t VERAVideo::video_get_dc_value(int reg)
{
	switch (reg & 0x1F)
	{
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x05: case 0x06: case 0x07:
	case 0x08: case 0x09: case 0x0a: case 0x0c:
	case 0x0d: case 0x0e: case 0x0f:
		return m_reg_composer[reg];
	case 0x0b:
		return m_reg_composer[reg] & 0x3f;
	case 0x10: // DCSEL=4, $9F29
		return (m_fx_x_pixel_position >> 16) & 0xff;
	case 0x11: // DCSEL=4, $9F2A
		return ((m_fx_x_pixel_position >> 24) & 0x07) | (m_fx_x_pixel_position & 0x80);
	case 0x12: // DCSEL=4, $9F2B
		return (m_fx_y_pixel_position >> 16) & 0xff;
	case 0x13: // DCSEL=4, $9F2C
		return ((m_fx_y_pixel_position >> 24) & 0x07) | (m_fx_y_pixel_position & 0x80);
	case 0x14: // DCSEL=5, $9F29
		return (m_fx_x_pixel_position >> 8) & 0xff;
	case 0x15: // DCSEL=5, $9F2A
		return (m_fx_y_pixel_position >> 8) & 0xff;
	case 0x16: // DCSEL=5, $9F2B
		if (m_fx_poly_fill_length >= 768)
			return ((m_fx_2bit_poly && m_fx_addr1_mode == 2) ? 0x00 : 0x80);
		if (m_fx_4bit_mode)
		{
			if (m_fx_2bit_poly && m_fx_addr1_mode == 2)
			{
				return ((m_fx_y_pixel_position & 0x00008000) >> 8) |
					((m_fx_x_pixel_position >> 11) & 0x60) |
					((m_fx_x_pixel_position >> 14) & 0x10) |
					((m_fx_poly_fill_length & 0x0007) << 1) |
					((m_fx_x_pixel_position & 0x00008000) >> 15);
			}
			else
			{
				return ((m_fx_poly_fill_length & 0xfff8) != 0) << 7 |
					((m_fx_x_pixel_position >> 11) & 0x60) |
					((m_fx_x_pixel_position >> 14) & 0x10) |
					((m_fx_poly_fill_length & 0x0007) << 1);
			}
		}
		else
		{
			return ((m_fx_poly_fill_length & 0xfff0) != 0) << 7 |
				((m_fx_x_pixel_position >> 11) & 0x60) |
				((m_fx_poly_fill_length & 0x000f) << 1);
		}
	case 0x17: // DCSEL=5, 0x9F2C
		return (m_fx_poly_fill_length & 0x03f8) >> 2;
	case 0x18: // DCSEL=6, 0x9F29
		return m_fx_cache[0];
	case 0x19: // DCSEL=6, 0x9F2A
		return m_fx_cache[1];
	case 0x1a: // DCSEL=6, 0x9F2B
		return m_fx_cache[2];
	case 0x1b: // DCSEL=6, 0x9F2C
		return m_fx_cache[3];
	default:
		break;
	}

	return m_vera_version_string[reg % 4];
}

void VERAVideo::check_not_readonly(uint8_t reg)
{
	bool wrong = false;
	switch (m_io_dcsel)
	{
	case 5:
		switch (reg)
		{
		case 0x0b: case 0x0c: wrong = true; break;
		}
		break;
	case 63:
		switch (reg)
		{
		case 0x09: case 0x0a: case 0x0b: case 0x0c: wrong = true; break;
		}
		break;
	}
	// (warning logging omitted; matches silent behaviour in non-debug build)
}

void VERAVideo::check_not_writeonly(uint8_t reg)
{
	bool wrong = false;
	switch (m_io_dcsel)
	{
	case 2:
		switch (reg)
		{
		case 0x0a: case 0x0b: case 0x0c: wrong = true; break;
		}
		break;
	case 3:
	case 4:
		switch (reg)
		{
		case 0x09: case 0x0a: case 0x0b: case 0x0c: wrong = true; break;
		}
		break;
	case 5:
		switch (reg)
		{
		case 0x09: case 0x0a: wrong = true; break;
		}
		break;
	case 6:
		switch (reg)
		{
		case 0x0b: case 0x0c: wrong = true; break;
		}
		break;
	}
	// (warning logging omitted)
}

uint32_t VERAVideo::get_and_inc_address(int sel, bool write)
{
	const uint32_t address = m_io_addr[sel];
	int32_t incr = s_increments[m_io_inc[sel]];
	if (m_fx_4bit_mode && m_fx_nibble_incr[sel] && !incr)
	{
		if (m_fx_nibble_bit[sel])
		{
			if ((m_io_inc[sel] & 1) == 0) m_io_addr[sel] += 1;
			m_fx_nibble_bit[sel] = 0;
		}
		else
		{
			if (m_io_inc[sel] & 1) m_io_addr[sel] -= 1;
			m_fx_nibble_bit[sel] = 1;
		}
	}

	if (sel == 1 && m_fx_16bit_hop)
	{
		if (incr == 4)
		{
			if (m_fx_16bit_hop_align == static_cast<int>(address & 0x3))
				incr = 1;
			else
				incr = 3;
		}
		else if (incr == 320)
		{
			if (m_fx_16bit_hop_align == static_cast<int>(address & 0x3))
				incr = 1;
			else
				incr = 319;
		}
	}

	m_io_addr[sel] += incr;
	if (sel == 1 && m_fx_addr1_mode == 1) // FX line draw mode
	{
		m_fx_x_pixel_position += m_fx_x_pixel_increment;
		if (m_fx_x_pixel_position & 0x10000)
		{
			m_fx_x_pixel_position &= ~0x10000;
			if (m_fx_4bit_mode && m_fx_nibble_incr[0])
			{
				if (m_fx_nibble_bit[1])
				{
					if ((m_io_inc[0] & 1) == 0) m_io_addr[1] += 1;
					m_fx_nibble_bit[1] = 0;
				}
				else
				{
					if (m_io_inc[0] & 1) m_io_addr[1] -= 1;
					m_fx_nibble_bit[1] = 1;
				}
			}
			m_io_addr[1] += s_increments[m_io_inc[0]];
		}
	}
	else if (m_fx_addr1_mode == 2 && write == false) // FX polygon fill mode
	{
		m_fx_x_pixel_position += m_fx_x_pixel_increment;
		m_fx_y_pixel_position += m_fx_y_pixel_increment;
		m_fx_poly_fill_length = (m_fx_y_pixel_position >> 16) - (m_fx_x_pixel_position >> 16);
		if (sel == 0 && m_fx_cache_byte_cycling && !m_fx_cache_fill)
			m_fx_cache_byte_index = (m_fx_cache_byte_index + 1) & 3;
		if (sel == 1)
		{
			if (m_fx_4bit_mode)
			{
				m_io_addr[1] = m_io_addr[0] + (m_fx_x_pixel_position >> 17);
				m_fx_nibble_bit[1] = (m_fx_x_pixel_position >> 16) & 1;
			}
			else
			{
				m_io_addr[1] = m_io_addr[0] + (m_fx_x_pixel_position >> 16);
			}
		}
	}
	else if (sel == 1 && m_fx_addr1_mode == 3 && write == false) // FX affine mode
	{
		m_fx_x_pixel_position += m_fx_x_pixel_increment;
		m_fx_y_pixel_position += m_fx_y_pixel_increment;
	}
	return address;
}

void VERAVideo::fx_affine_prefetch()
{
	if (m_fx_addr1_mode != 3) return;

	uint32_t address = 0;
	int affine_x_tile = (m_fx_x_pixel_position >> 19) & 0xff;
	int affine_y_tile = (m_fx_y_pixel_position >> 19) & 0xff;
	const int affine_x_sub_tile = (m_fx_x_pixel_position >> 16) & 0x07;
	const int affine_y_sub_tile = (m_fx_y_pixel_position >> 16) & 0x07;
	if (!m_fx_affine_clip) // wrap
	{
		affine_x_tile &= m_fx_affine_map_size - 1;
		affine_y_tile &= m_fx_affine_map_size - 1;
	}

	if (affine_x_tile >= m_fx_affine_map_size || affine_y_tile >= m_fx_affine_map_size)
	{
		address = m_fx_affine_tile_base + (affine_y_sub_tile << (3 - m_fx_4bit_mode)) + (affine_x_sub_tile >> static_cast<int>(m_fx_4bit_mode));
		m_fx_nibble_bit[1] = (affine_x_sub_tile & 1) >> (1 - m_fx_4bit_mode);
	}
	else
	{
		address = m_fx_affine_map_base + (affine_y_tile * m_fx_affine_map_size) + affine_x_tile;
		const int affine_tile_idx = video_space_read(address);
		address = m_fx_affine_tile_base + (affine_tile_idx << (6 - m_fx_4bit_mode));
		address += (affine_y_sub_tile << (3 - m_fx_4bit_mode)) + (affine_x_sub_tile >> static_cast<int>(m_fx_4bit_mode));
		m_fx_nibble_bit[1] = (affine_x_sub_tile & 1) >> (1 - m_fx_4bit_mode);
	}
	m_io_addr[1] = address;
	m_io_rddata[1] = video_space_read(address);
}

// ---- 6502 I/O Interface ----

uint8_t VERAVideo::Read(uint8_t reg, bool debugOn)
{
	const bool ntsc_mode = (m_reg_composer[0] & 2) != 0;
	int scanline = ntsc_mode ? m_ntsc_scan_pos_y % SCAN_HEIGHT : m_vga_scan_pos_y;
	if (scanline >= 512) scanline = 511;
	check_not_writeonly(reg);
	switch (reg & 0x1F)
	{
	case 0x00: return m_io_addr[m_io_addrsel] & 0xff;
	case 0x01: return (m_io_addr[m_io_addrsel] >> 8) & 0xff;
	case 0x02: return (m_io_addr[m_io_addrsel] >> 16) | (m_fx_nibble_bit[m_io_addrsel] << 1) | (m_fx_nibble_incr[m_io_addrsel] << 2) | (m_io_inc[m_io_addrsel] << 3);
	case 0x03:
	case 0x04:
	{
		if (debugOn)
			return m_io_rddata[reg - 3];

		const bool addr_nibble = m_fx_nibble_bit[reg - 3] != 0;
		const uint32_t address = get_and_inc_address(reg - 3, false);
		const uint8_t value = m_io_rddata[reg - 3];
		if (reg == 4 && m_fx_addr1_mode == 3)
			fx_affine_prefetch();
		else
			m_io_rddata[reg - 3] = video_space_read(m_io_addr[reg - 3]);
		if (m_fx_cache_fill)
		{
			if (m_fx_4bit_mode)
			{
				const int nibble_read = (addr_nibble ? ((value & 0x0f) << 4) : (value & 0xf0));
				if (m_fx_cache_nibble_index)
				{
					m_fx_cache[m_fx_cache_byte_index] = (m_fx_cache[m_fx_cache_byte_index] & 0xf0) | (nibble_read >> 4);
					m_fx_cache_nibble_index = false;
					m_fx_cache_byte_index = ((m_fx_cache_byte_index + 1) & 0x3);
				}
				else
				{
					m_fx_cache[m_fx_cache_byte_index] = (m_fx_cache[m_fx_cache_byte_index] & 0x0f) | (nibble_read);
					m_fx_cache_nibble_index = true;
				}
			}
			else
			{
				m_fx_cache[m_fx_cache_byte_index] = value;
				if (m_fx_cache_increment_mode)
					m_fx_cache_byte_index = (m_fx_cache_byte_index & 0x2) | ((m_fx_cache_byte_index + 1) & 0x1);
				else
					m_fx_cache_byte_index = ((m_fx_cache_byte_index + 1) & 0x3);
			}
		}
		return value;
	}
	case 0x05: return static_cast<uint8_t>((m_io_dcsel << 1) | m_io_addrsel);
	case 0x06: return static_cast<uint8_t>(((m_irq_line & 0x100) >> 1) | ((scanline & 0x100) >> 2) | (m_ien & 0xF));
	case 0x07: return static_cast<uint8_t>(m_isr | (m_pAudio && m_pAudio->IsFifoAlmostEmpty() ? 8 : 0));
	case 0x08: return static_cast<uint8_t>(scanline & 0xFF);
	case 0x09:
	case 0x0A:
	case 0x0B:
	case 0x0C:
	{
		const int i = reg - 0x09 + (m_io_dcsel << 2);
		if (debugOn) return video_get_dc_value(i);
		switch (i)
		{
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x04: case 0x05: case 0x06: case 0x07:
		case 0x08: case 0x16: case 0x17:
			return video_get_dc_value(i);
		case 0x18: // DCSEL=6, 0x9F29
			m_fx_mult_accumulator = 0;
			break;
		case 0x19: // DCSEL=6, 0x9F2A
		{
			const int16_t a = static_cast<int16_t>((m_fx_cache[1] << 8) | m_fx_cache[0]);
			const int16_t b = static_cast<int16_t>((m_fx_cache[3] << 8) | m_fx_cache[2]);
			const int32_t m_result = a * b;
			if (m_fx_subtract)
				m_fx_mult_accumulator -= m_result;
			else
				m_fx_mult_accumulator += m_result;
			break;
		}
		default:
			break;
		}
		return m_vera_version_string[i % 4];
	}
	case 0x0D: case 0x0E: case 0x0F: case 0x10:
	case 0x11: case 0x12: case 0x13:
		return m_reg_layer[0][reg - 0x0D];
	case 0x14: case 0x15: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1A:
		return m_reg_layer[1][reg - 0x14];
	case 0x1B: return m_pAudio ? m_pAudio->ReadPcmCtrl() : 0;
	case 0x1C: return m_pAudio ? m_pAudio->ReadPcmRate() : 0;
	case 0x1D: return 0;
	case 0x1E: case 0x1F: return 0xff;	// SPI: no SD card yet (returns idle)
	}
	return 0;
}

void VERAVideo::Write(uint8_t reg, uint8_t value)
{
	check_not_readonly(reg);
	switch (reg & 0x1F)
	{
	case 0x00:
		if (m_fx_2bit_poly && m_fx_4bit_mode && m_fx_addr1_mode == 2 && m_io_addrsel == 1)
		{
			m_fx_2bit_poking = true;
			m_io_addr[1] = (m_io_addr[1] & 0x1fffc) | (value & 0x3);
		}
		else
		{
			m_io_addr[m_io_addrsel] = (m_io_addr[m_io_addrsel] & 0x1ff00) | value;
			if (m_fx_16bit_hop && m_io_addrsel == 1)
				m_fx_16bit_hop_align = value & 3;
		}
		m_io_rddata[m_io_addrsel] = video_space_read(m_io_addr[m_io_addrsel]);
		break;
	case 0x01:
		m_io_addr[m_io_addrsel] = (m_io_addr[m_io_addrsel] & 0x100ff) | (value << 8);
		m_io_rddata[m_io_addrsel] = video_space_read(m_io_addr[m_io_addrsel]);
		break;
	case 0x02:
		m_io_addr[m_io_addrsel] = (m_io_addr[m_io_addrsel] & 0x0ffff) | ((value & 0x1) << 16);
		m_fx_nibble_bit[m_io_addrsel] = (value >> 1) & 0x1;
		m_fx_nibble_incr[m_io_addrsel] = (value >> 2) & 0x1;
		m_io_inc[m_io_addrsel] = value >> 3;
		m_io_rddata[m_io_addrsel] = video_space_read(m_io_addr[m_io_addrsel]);
		break;
	case 0x03:
	case 0x04:
	{
		if (m_fx_2bit_poking && m_fx_addr1_mode)
		{
			m_fx_2bit_poking = false;
			const int mask = value >> 6;
			switch (mask)
			{
			case 0x00:
				m_video_ram[m_io_addr[1] & 0x1FFFF] = (m_fx_cache[m_fx_cache_byte_index] & 0xc0) | (m_io_rddata[1] & 0x3f);
				break;
			case 0x01:
				m_video_ram[m_io_addr[1] & 0x1FFFF] = (m_fx_cache[m_fx_cache_byte_index] & 0x30) | (m_io_rddata[1] & 0xcf);
				break;
			case 0x02:
				m_video_ram[m_io_addr[1] & 0x1FFFF] = (m_fx_cache[m_fx_cache_byte_index] & 0x0c) | (m_io_rddata[1] & 0xf3);
				break;
			case 0x03:
				m_video_ram[m_io_addr[1] & 0x1FFFF] = (m_fx_cache[m_fx_cache_byte_index] & 0x03) | (m_io_rddata[1] & 0xfc);
				break;
			}
			break;
		}

		Step(MHZ, 0, true); // potential midline raster effect
		const bool nibble = m_fx_nibble_bit[reg - 3] != 0;
		const uint32_t address = get_and_inc_address(reg - 3, true);

		uint8_t wrdata_to_use = 0;
		uint8_t ram_wrdata[4];
		uint8_t nibble_mask[4];
		uint8_t cache_to_use[4];
		if (m_fx_multiplier)
		{
			const int16_t a = static_cast<int16_t>((m_fx_cache[1] << 8) | m_fx_cache[0]);
			const int16_t b = static_cast<int16_t>((m_fx_cache[3] << 8) | m_fx_cache[2]);
			int32_t m_result = a * b;
			if (m_fx_subtract)
				m_result = m_fx_mult_accumulator - m_result;
			else
				m_result = m_fx_mult_accumulator + m_result;
			cache_to_use[0] = m_result & 0xff;
			cache_to_use[1] = (m_result >> 8) & 0xff;
			cache_to_use[2] = (m_result >> 16) & 0xff;
			cache_to_use[3] = (m_result >> 24) & 0xff;
		}
		else
		{
			memcpy(cache_to_use, m_fx_cache, sizeof(m_fx_cache));
		}

		if (m_fx_cache_byte_cycling)
			wrdata_to_use = m_fx_cache[m_fx_cache_byte_index];
		else
			wrdata_to_use = value;

		if (m_fx_cache_write && !m_fx_cache_byte_cycling)
		{
			ram_wrdata[0] = cache_to_use[0];
			ram_wrdata[1] = cache_to_use[1];
			ram_wrdata[2] = cache_to_use[2];
			ram_wrdata[3] = cache_to_use[3];
		}
		else
		{
			ram_wrdata[0] = wrdata_to_use;
			ram_wrdata[1] = wrdata_to_use;
			ram_wrdata[2] = wrdata_to_use;
			ram_wrdata[3] = wrdata_to_use;
		}

		uint32_t addr_aligned = address & 0x1fffc;
		if (m_fx_cache_write)
		{
			if (m_fx_trans_writes)
			{
				if (m_fx_4bit_mode)
				{
					nibble_mask[0] = static_cast<uint8_t>(((ram_wrdata[0] & 0xf0) == 0) << 1 | ((ram_wrdata[0] & 0x0f) == 0));
					nibble_mask[1] = static_cast<uint8_t>(((ram_wrdata[1] & 0xf0) == 0) << 1 | ((ram_wrdata[1] & 0x0f) == 0));
					nibble_mask[2] = static_cast<uint8_t>(((ram_wrdata[2] & 0xf0) == 0) << 1 | ((ram_wrdata[2] & 0x0f) == 0));
					nibble_mask[3] = static_cast<uint8_t>(((ram_wrdata[3] & 0xf0) == 0) << 1 | ((ram_wrdata[3] & 0x0f) == 0));
				}
				else
				{
					nibble_mask[0] = (ram_wrdata[0] != 0) ? 0 : 3;
					nibble_mask[1] = (ram_wrdata[1] != 0) ? 0 : 3;
					nibble_mask[2] = (ram_wrdata[2] != 0) ? 0 : 3;
					nibble_mask[3] = (ram_wrdata[3] != 0) ? 0 : 3;
				}
			}
			else
			{
				nibble_mask[0] = value & 0x3;
				nibble_mask[1] = (value >> 2) & 0x3;
				nibble_mask[2] = (value >> 4) & 0x3;
				nibble_mask[3] = (value >> 6) & 0x3;
			}

			fx_vram_cache_write(addr_aligned + 0, ram_wrdata[0], nibble_mask[0]);
			fx_vram_cache_write(addr_aligned + 1, ram_wrdata[1], nibble_mask[1]);
			fx_vram_cache_write(addr_aligned + 2, ram_wrdata[2], nibble_mask[2]);
			fx_vram_cache_write(addr_aligned + 3, ram_wrdata[3], nibble_mask[3]);
		}
		else
		{
			fx_video_space_write(address, nibble, wrdata_to_use); // Normal write
		}

		m_io_rddata[reg - 3] = video_space_read(m_io_addr[reg - 3]);
		break;
	}
	case 0x05:
		if (value & 0x80)
			Reset();
		m_io_dcsel = (value >> 1) & 0x3f;
		m_io_addrsel = value & 1;
		break;
	case 0x06:
		m_irq_line = (m_irq_line & 0xFF) | ((value >> 7) << 8);
		m_ien = value & 0xF;
		break;
	case 0x07:
		m_isr &= value ^ 0xff;
		break;
	case 0x08:
		m_irq_line = (m_irq_line & 0x100) | value;
		break;
	case 0x09:
	case 0x0A:
	case 0x0B:
	case 0x0C:
	{
		Step(MHZ, 0, true); // potential midline raster effect
		const int i = reg - 0x09 + (m_io_dcsel << 2);
		if (i == 0)
		{
			if (((m_reg_composer[0] & 0x8) == 0 && (value & 0x8)) ||
				((m_reg_composer[0] & 0x3) == 1 && (value & 0x3) > 1 && (value & 0x8)))
			{
				memset(m_framebuffer.data(), 0x00, m_framebuffer.size());
			}
			// interlace field bit is read-only
			m_reg_composer[0] = (m_reg_composer[0] & ~0x7f) | (value & 0x7f);
			m_video_palette.dirty = true;
		}
		else
		{
			m_reg_composer[i] = value;
		}

		switch (i)
		{
		case 0x08: // DCSEL=2, $9F29
			m_fx_addr1_mode = value & 0x03;
			m_fx_4bit_mode = (value & 0x04) >> 2;
			m_fx_16bit_hop = (value & 0x08) >> 3;
			m_fx_cache_byte_cycling = (value & 0x10) >> 4;
			m_fx_cache_fill = (value & 0x20) >> 5;
			m_fx_cache_write = (value & 0x40) >> 6;
			m_fx_trans_writes = (value & 0x80) >> 7;
			break;
		case 0x09: // DCSEL=2, $9F2A
			m_fx_affine_tile_base = (value & 0xfc) << 9;
			m_fx_affine_clip = (value & 0x02) >> 1;
			m_fx_2bit_poly = (value & 0x01) != 0;
			break;
		case 0x0a: // DCSEL=2, $9F2B
			m_fx_affine_map_base = (value & 0xfc) << 9;
			m_fx_affine_map_size = 2 << ((value & 0x03) << 1);
			break;
		case 0x0b: // DCSEL=2, $9F2C
			m_fx_cache_increment_mode = (value & 0x01) != 0;
			m_fx_cache_nibble_index = ((value & 0x02) >> 1) != 0;
			m_fx_cache_byte_index = (value & 0x0c) >> 2;
			m_fx_multiplier = (value & 0x10) >> 4;
			m_fx_subtract = (value & 0x20) >> 5;
			if (value & 0x40) // accumulate
			{
				const int16_t a = static_cast<int16_t>((m_fx_cache[1] << 8) | m_fx_cache[0]);
				const int16_t b = static_cast<int16_t>((m_fx_cache[3] << 8) | m_fx_cache[2]);
				const int32_t m_result = a * b;
				if (m_fx_subtract)
					m_fx_mult_accumulator -= m_result;
				else
					m_fx_mult_accumulator += m_result;
			}
			if (value & 0x80) // reset accumulator
				m_fx_mult_accumulator = 0;
			break;
		case 0x0c: // DCSEL=3, $9F29
			m_fx_x_pixel_increment = static_cast<int32_t>(((((m_reg_composer[0x0d] & 0x7f) << 15) + (m_reg_composer[0x0c] << 7))
				| ((m_reg_composer[0x0d] & 0x40) ? 0xffc00000 : 0))
				<< 5 * ((m_reg_composer[0x0d] & 0x80) ? 1 : 0));
			break;
		case 0x0d: // DCSEL=3, $9F2A
			m_fx_x_pixel_increment = static_cast<int32_t>(((((m_reg_composer[0x0d] & 0x7f) << 15) + (m_reg_composer[0x0c] << 7))
				| ((m_reg_composer[0x0d] & 0x40) ? 0xffc00000 : 0))
				<< 5 * ((m_reg_composer[0x0d] & 0x80) ? 1 : 0));
			if (m_fx_addr1_mode == 1 || m_fx_addr1_mode == 2)
				m_fx_x_pixel_position = (m_fx_x_pixel_position & 0x07ff0000) | 0x00008000;
			break;
		case 0x0e: // DCSEL=3, $9F2B
			m_fx_y_pixel_increment = static_cast<int32_t>(((((m_reg_composer[0x0f] & 0x7f) << 15) + (m_reg_composer[0x0e] << 7))
				| ((m_reg_composer[0x0f] & 0x40) ? 0xffc00000 : 0))
				<< 5 * ((m_reg_composer[0x0f] & 0x80) ? 1 : 0));
			break;
		case 0x0f: // DCSEL=3, $9F2C
			m_fx_y_pixel_increment = static_cast<int32_t>(((((m_reg_composer[0x0f] & 0x7f) << 15) + (m_reg_composer[0x0e] << 7))
				| ((m_reg_composer[0x0f] & 0x40) ? 0xffc00000 : 0))
				<< 5 * ((m_reg_composer[0x0f] & 0x80) ? 1 : 0));
			if (m_fx_addr1_mode == 1 || m_fx_addr1_mode == 2)
				m_fx_y_pixel_position = (m_fx_y_pixel_position & 0x07ff0000) | 0x00008000;
			break;
		case 0x10: // DCSEL=4, $9F29
			m_fx_x_pixel_position = (m_fx_x_pixel_position & 0x0700ff80) | (value << 16);
			fx_affine_prefetch();
			break;
		case 0x11: // DCSEL=4, $9F2A
			m_fx_x_pixel_position = (m_fx_x_pixel_position & 0x00ffff00) | ((value & 0x7) << 24) | (value & 0x80);
			fx_affine_prefetch();
			break;
		case 0x12: // DCSEL=4, $9F2B
			m_fx_y_pixel_position = (m_fx_y_pixel_position & 0x0700ff80) | (value << 16);
			fx_affine_prefetch();
			break;
		case 0x13: // DCSEL=4, $9F2C
			m_fx_y_pixel_position = (m_fx_y_pixel_position & 0x00ffff00) | ((value & 0x7) << 24) | (value & 0x80);
			fx_affine_prefetch();
			break;
		case 0x14: // DCSEL=5, $9F29
			m_fx_x_pixel_position = (m_fx_x_pixel_position & 0x07ff0080) | (value << 8);
			break;
		case 0x15: // DCSEL=5, $9F2A
			m_fx_y_pixel_position = (m_fx_y_pixel_position & 0x07ff0080) | (value << 8);
			break;
		case 0x18: // DCSEL=6, $9F29
			m_fx_cache[0] = value;
			break;
		case 0x19: // DCSEL=6, $9F2A
			m_fx_cache[1] = value;
			break;
		case 0x1a: // DCSEL=6, $9F2B
			m_fx_cache[2] = value;
			break;
		case 0x1b: // DCSEL=6, $9F2C
			m_fx_cache[3] = value;
			break;
		}
		break;
	}

	case 0x0D: case 0x0E: case 0x0F: case 0x10:
	case 0x11: case 0x12: case 0x13:
		Step(MHZ, 0, true); // potential midline raster effect
		m_reg_layer[0][reg - 0x0D] = value;
		refresh_layer_properties(0);
		break;
	case 0x14: case 0x15: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1A:
		Step(MHZ, 0, true); // potential midline raster effect
		m_reg_layer[1][reg - 0x14] = value;
		refresh_layer_properties(1);
		break;
	case 0x1B: write_pcm(0, value); break;
	case 0x1C: write_pcm(1, value); break;
	case 0x1D: write_pcm(2, value); break;
	case 0x1E: case 0x1F: break;	// SPI: no SD card yet
	}
}

unsigned int VERAVideo::GetRegisterStateSize()
{
	// io_addr (2x4) + io_rddata (2) + io_inc (2) + addrsel/dcsel (2) + ien/isr (2)
	// + irq_line (2) + composer (256) + layer (2x7=14) + scan counters (4x4=16)
	return 8 + 2 + 2 + 2 + 2 + 2 + 256 + 14 + 16;	// = 304
}

void VERAVideo::SerializeRegisters(std::vector<uint8_t>& out) const
{
	auto put16 = [&](uint16_t v) { out.push_back((uint8_t)(v & 0xff)); out.push_back((uint8_t)((v >> 8) & 0xff)); };
	auto put32 = [&](uint32_t v) { out.push_back((uint8_t)(v & 0xff)); out.push_back((uint8_t)((v >> 8) & 0xff)); out.push_back((uint8_t)((v >> 16) & 0xff)); out.push_back((uint8_t)((v >> 24) & 0xff)); };

	for (int i = 0; i < 2; i++) put32(m_io_addr[i]);
	for (int i = 0; i < 2; i++) out.push_back(m_io_rddata[i]);
	for (int i = 0; i < 2; i++) out.push_back(m_io_inc[i]);
	out.push_back((uint8_t)m_io_addrsel);
	out.push_back((uint8_t)m_io_dcsel);
	out.push_back((uint8_t)m_ien);
	out.push_back((uint8_t)m_isr);
	put16((uint16_t)m_irq_line);
	for (int i = 0; i < COMPOSER_SLOTS; i++) out.push_back(m_reg_composer[i]);
	for (int layer = 0; layer < 2; layer++)
		for (int i = 0; i < 7; i++) out.push_back(m_reg_layer[layer][i]);
	put32((uint32_t)m_vga_scan_pos_x);
	put32((uint32_t)m_vga_scan_pos_y);
	put32((uint32_t)m_ntsc_half_cnt);
	put32((uint32_t)m_ntsc_scan_pos_y);
}

void VERAVideo::DeserializeRegisters(const std::vector<uint8_t>& in)
{
	if (in.size() < GetRegisterStateSize())
		return;

	size_t pos = 0;
	auto get16 = [&]() -> uint16_t { uint16_t v = in[pos] | ((uint16_t)in[pos + 1] << 8); pos += 2; return v; };
	auto get32 = [&]() -> uint32_t { uint32_t v = in[pos] | ((uint32_t)in[pos + 1] << 8) | ((uint32_t)in[pos + 2] << 16) | ((uint32_t)in[pos + 3] << 24); pos += 4; return v; };

	for (int i = 0; i < 2; i++) m_io_addr[i] = get32();
	for (int i = 0; i < 2; i++) m_io_rddata[i] = in[pos++];
	for (int i = 0; i < 2; i++) m_io_inc[i] = in[pos++];
	m_io_addrsel = in[pos++];
	m_io_dcsel = in[pos++];
	m_ien = in[pos++];
	m_isr = in[pos++];
	m_irq_line = get16();
	for (int i = 0; i < COMPOSER_SLOTS; i++) m_reg_composer[i] = in[pos++];
	for (int layer = 0; layer < 2; layer++)
		for (int i = 0; i < 7; i++) m_reg_layer[layer][i] = in[pos++];
	m_vga_scan_pos_x = (int)get32();
	m_vga_scan_pos_y = (int)get32();
	m_ntsc_half_cnt = (int)get32();
	m_ntsc_scan_pos_y = (int)get32();

	// Recompute derived layer properties from the restored layer registers.
	refresh_layer_properties(0);
	refresh_layer_properties(1);
}
