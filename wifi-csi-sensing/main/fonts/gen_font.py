#!/usr/bin/env python3
"""
Generate LVGL v9 fmt_txt font C files for Chinese sub-fonts.
Creates stub 1bpp bitmap data for 48 UI Chinese characters.
"""

import os

# 48 unique Chinese characters used in the UI (sorted by Unicode)
CHARS = sorted(set(
    '番茄钟短休息长'    # ui_timer.c
    '周一二三四五六日'  # ui_clock.c
    '湿度请用配置城市离线获取中'  # ui_weather.c
    '已连接未'           # ui_pc_mon.c
    '晴多云阴雾冻小雨大雨雪阵雨暴雨雷暴冰雹未知'  # app_weather.c
))

CODEPOINTS = [ord(c) for c in CHARS]
NUM_GLYPHS = len(CODEPOINTS)  # 48


def make_filled_rect_bitmap(box_w, box_h):
    """Generate 1bpp bitmap data for a filled rectangle (placeholder glyph)."""
    bytes_per_row = (box_w + 7) // 8
    row = 0xFF  # all pixels set (filled row)
    # Mask off extra bits in last byte
    extra_bits = bytes_per_row * 8 - box_w
    if extra_bits > 0:
        row &= (0xFF << extra_bits) & 0xFF
    return [row] * bytes_per_row * box_h


def generate_font_c(font_name, font_size):
    """Generate a font C file for the given size."""
    # Chinese chars are roughly square: box_w = box_h = font_size
    box_w = font_size
    box_h = font_size
    adv_w = font_size  # advance width in 8.4 format (real * 16)
    ofs_x = 0
    ofs_y = 0
    line_height = font_size + 2  # line height with a bit of spacing
    base_line = -(font_size - 2)  # baseline near bottom

    # Generate bitmap data
    all_bitmap_bytes = []
    glyph_dscs = []
    bitmap_index = 0

    for i in range(NUM_GLYPHS):
        bitmap = make_filled_rect_bitmap(box_w, box_h)
        all_bitmap_bytes.extend(bitmap)
        glyph_dscs.append({
            'bitmap_index': bitmap_index,
            'adv_w': adv_w,
            'box_w': box_w,
            'box_h': box_h,
            'ofs_x': ofs_x,
            'ofs_y': ofs_y,
        })
        bitmap_index += len(bitmap)

    # Format the bitmap array
    bitmap_lines = []
    for i in range(0, len(all_bitmap_bytes), 12):
        chunk = all_bitmap_bytes[i:i+12]
        hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
        bitmap_lines.append(f'    {hex_str}')

    # Format glyph descriptor array
    glyph_dsc_lines = []
    for gd in glyph_dscs:
        glyph_dsc_lines.append(
            f'    {{{gd["bitmap_index"]}, {gd["adv_w"]}, {gd["box_w"]}, {gd["box_h"]}, {gd["ofs_x"]}, {gd["ofs_y"]}}},'
        )

    # Format unicode list (relative to range_start)
    # Use the first char as range_start for sparse tiny format
    range_start = CODEPOINTS[0]
    unicode_relative = [cp - range_start for cp in CODEPOINTS]

    unicode_lines = []
    for i in range(0, len(unicode_relative), 8):
        chunk = unicode_relative[i:i+8]
        hex_str = ', '.join(f'0x{v:04X}' for v in chunk)
        unicode_lines.append(f'    {hex_str}')

    # Generate C code
    c_code = f'''/**
 * @file {font_name}.c
 * @brief Chinese sub-font ({font_size}px, 1bpp) for LVGL v9.5.0
 *
 * Covers {NUM_GLYPHS} unique Chinese characters used in the UI.
 *
 * TODO: Generate proper bitmap data with lv_font_conv tool:
 *   npx lv_font_conv \\
 *     --font SourceHanSansSC-Regular \\
 *     --size {font_size} \\
 *     --bpp 1 \\
 *     --format lvgl \\
 *     --symbols {" ".join(CHARS)} \\
 *     --no-prefilter \\
 *     -o {font_name}.c
 *
 * Current bitmaps are placeholder filled rectangles.
 */

#include "lvgl.h"
#include "lv_font.h"

/* ---------- Bitmap data (1bpp, placeholder filled rectangles) ---------- */
static const uint8_t {font_name}_glyph_bitmap[] = {{
{chr(10).join(bitmap_lines)}
}};

/* ---------- Glyph descriptors ---------- */
static const lv_font_fmt_txt_glyph_dsc_t {font_name}_glyph_dsc[] = {{
{chr(10).join(glyph_dsc_lines)}
}};

/* ---------- Unicode list (sparse tiny format) ---------- */
static const uint16_t {font_name}_unicode_list[] = {{
{chr(10).join(unicode_lines)}
}};

/* ---------- Character map ---------- */
static const lv_font_fmt_txt_cmap_t {font_name}_cmap = {{
    .range_start       = 0x{range_start:04X},
    .range_length      = 0x{CODEPOINTS[-1] - range_start + 1:04X},
    .glyph_id_start    = 0,
    .unicode_list      = {font_name}_unicode_list,
    .glyph_id_ofs_list = NULL,
    .list_length       = {NUM_GLYPHS},
    .type              = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
}};

/* ---------- Font descriptor ---------- */
const lv_font_t {font_name} = {{
    .get_glyph_dsc    = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .release_glyph    = NULL,
    .line_height      = {line_height},
    .base_line        = {base_line},
    .subpx            = LV_FONT_SUBPX_NONE,
    .kerning          = LV_FONT_KERNING_NONE,
    .static_bitmap    = 1,
    .underline_position  = -2,
    .underline_thickness = 1,
    .dsc              = &(const lv_font_fmt_txt_dsc_t){{
        .glyph_bitmap = {font_name}_glyph_bitmap,
        .glyph_dsc    = {font_name}_glyph_dsc,
        .cmaps        = &{font_name}_cmap,
        .kern_dsc     = NULL,
        .kern_scale   = 0,
        .cmap_num     = 1,
        .bpp          = 1,
        .kern_classes = 0,
        .bitmap_format = LV_FONT_FMT_TXT_PLAIN,
        .stride       = 0,
    }},
    .fallback    = NULL,
    .user_data   = NULL,
}};
'''

    return c_code


def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))

    # Generate 14px font
    font_14 = generate_font_c('ui_font_cn_14', 14)
    path_14 = os.path.join(base_dir, 'ui_font_cn_14.c')
    with open(path_14, 'w', encoding='utf-8') as f:
        f.write(font_14)
    print(f'Generated: {path_14}')

    # Generate 12px font
    font_12 = generate_font_c('ui_font_cn_12', 12)
    path_12 = os.path.join(base_dir, 'ui_font_cn_12.c')
    with open(path_12, 'w', encoding='utf-8') as f:
        f.write(font_12)
    print(f'Generated: {path_12}')

    print(f'Total characters: {NUM_GLYPHS}')
    print(f'Characters: {"".join(CHARS)}')


if __name__ == '__main__':
    main()
