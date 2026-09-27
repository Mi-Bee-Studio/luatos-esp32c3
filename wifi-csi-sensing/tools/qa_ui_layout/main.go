// qa_ui_layout — luatos wifi-csi-sensing 屏幕布局碰撞检查器（编译型 CLI）。
//
// 背景：UI_ROW_ASSERT 只防纵向越界，横向重叠靠估必翻车（v2.2.1/v2.2.2 两次
// 真机翻车）。本工具从字体 C 文件解析真实字宽与字形墨迹，声明式复刻三页
// （Sense/NET/SYS）+ 标题栏的"最坏情形"文案铺位，检查：
//   1. 标签盒越界（水平 = 步进宽，保守正确；垂直 = 墨迹上下缘）；
//   2. 同页同时可见标签的两两墨迹交叠。
// 改布局必须跑到 0 issues 再烧机。页面模型是手写镜像（与 ui/*.c 对应）——
// 改了布局代码必须同步这里的模型。
//
// 用法（wifi-csi-sensing/ 目录下）：
//   go run ./tools/qa_ui_layout
//   go build -o qa_ui_layout.exe ./tools/qa_ui_layout && ./qa_ui_layout.exe
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

// ---- 屏幕与布局常量（与 ui/ui_common.h 对应）----
const (
	screenW   = 160
	screenH   = 80
	titleH    = 15
	contentY  = titleH
	colLLabel = 2
	colLValue = 36
	colRLabel = 86
	colRValue = 122
)

// ---- 字体 ----
type font struct {
	adv       map[rune]float64 // 步进宽（px）
	inkTop    map[rune]float64 // baseline 上方最高墨迹（ofs_y + box_h）
	inkBot    map[rune]float64 // baseline 上方最低墨迹（ofs_y）
	lineH     float64
	baseLine  float64
	spaceDef  float64 // 字表外字符的兜底步进
}

var advRe = regexp.MustCompile(`\.adv_w = (\d+)`)
var boxRe = regexp.MustCompile(`\.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)`)
var lhRe = regexp.MustCompile(`\.line_height = (\d+)`)
var blRe = regexp.MustCompile(`\.base_line = (\d+)`)
var cmapRe = regexp.MustCompile(`\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+)`)

func loadFont(path string) font {
	src, err := os.ReadFile(path)
	must(err != nil, "read font "+path)
	s := string(src)

	f := font{adv: map[rune]float64{}, inkTop: map[rune]float64{},
		inkBot: map[rune]float64{}, spaceDef: 6.0}
	m := lhRe.FindStringSubmatch(s)
	must(m == nil, "line_height not found")
	f.lineH, _ = strconv.ParseFloat(m[1], 64)
	if m = blRe.FindStringSubmatch(s); m != nil {
		f.baseLine, _ = strconv.ParseFloat(m[1], 64)
	}
	advs := numAll(advRe, s)
	var boxes [][4]int
	for _, g := range boxRe.FindAllStringSubmatch(s, -1) {
		var v [4]int
		for i := 0; i < 4; i++ {
			v[i], _ = strconv.Atoi(g[i+1])
		}
		boxes = append(boxes, v)
	}
	m = cmapRe.FindStringSubmatch(s)
	must(m == nil, "cmap not found")
	start, _ := strconv.Atoi(m[1])
	length, _ := strconv.Atoi(m[2])
	gid0, _ := strconv.Atoi(m[3])
	for cp := start; cp < start+length; cp++ {
		gi := cp - start + gid0
		if gi >= len(advs) {
			break
		}
		c := rune(cp)
		f.adv[c] = float64(advs[gi]) / 16.0 // adv_w 单位 1/16 px
		if boxes[gi][0] > 0 {               // box_w>0 才有墨迹（空格无）
			f.inkTop[c] = float64(boxes[gi][1] + boxes[gi][3]) // ofs_y + box_h
			f.inkBot[c] = float64(boxes[gi][3])                // ofs_y
		}
	}
	return f
}

func numAll(re *regexp.Regexp, s string) []int {
	var out []int
	for _, g := range re.FindAllStringSubmatch(s, -1) {
		n, _ := strconv.Atoi(g[1])
		out = append(out, n)
	}
	return out
}

func must(cond bool, msg string) {
	if cond {
		fmt.Fprintln(os.Stderr, "fatal:", msg)
		os.Exit(2)
	}
}

// ---- 标签模型 ----
type label struct {
	page string
	x, y float64
	text string
	ft   font
	// 占位盒（页点/条/钳宽 SSID）：宽高手工定，不走字宽
	fixedW, fixedH float64
	hasFixed       bool
}

func (l label) w() float64 {
	if l.hasFixed {
		return l.fixedW
	}
	sum := 0.0
	for _, c := range l.text {
		if a, ok := l.ft.adv[c]; ok {
			sum += a
		} else {
			sum += l.ft.spaceDef
		}
	}
	return sum
}

// inkBox: 水平 = 步进宽（保守正确）；垂直 = 字形墨迹上下缘。
func (l label) inkBox() (x1, y1, x2, y2 float64) {
	if l.hasFixed {
		return l.x, l.y, l.x + l.fixedW, l.y + l.fixedH
	}
	base := l.y + l.ft.lineH - l.ft.baseLine
	top, bot := base, base
	hasInk := false
	for _, c := range l.text {
		if t, ok := l.ft.inkTop[c]; ok {
			if !hasInk || base-t < top {
				top = base - t
			}
			if b := base - l.ft.inkBot[c]; b > bot {
				bot = b
			}
			hasInk = true
		}
	}
	if !hasInk {
		return l.x, l.y, l.x + l.w(), l.y + l.ft.lineH
	}
	return l.x, top, l.x + l.w(), bot
}

func alignRight(page string, y float64, text string, ft font) label {
	return label{page: page, x: screenW - 2 - wOf(text, ft), y: y, text: text, ft: ft}
}

func center(page string, y float64, text string, ft font) label {
	return label{page: page, x: (screenW - wOf(text, ft)) / 2, y: y, text: text, ft: ft}
}

func wOf(text string, ft font) float64 {
	return label{text: text, ft: ft}.w()
}

func buildModel(ver string) []label {
	var L []label
	// ---- 标题栏（每页都画；y 相对标题栏顶）----
	for _, page := range []string{"Sense", "NET", "SYS"} {
		L = append(L, label{page, 2, 1, page, m12, 0, 0, false})
		L = append(L, label{page, 42, (titleH - 5) / 2, "[dots]", m12, 27, 5, true})
		L = append(L, alignRight(page, 1, "25Hz -100 LOC", m12)) // 链路最坏
	}
	// ---- Sense 页 ----
	L = append(L, label{"Sense", 1, contentY + 4, "CALIBRATING", m22, 0, 0, false})
	// BR 行两态互斥（宽同为 16 字 ~96px），模型取其一：
	L = append(L, center("Sense", contentY+31, "needs empty room", m12))
	L = append(L, label{"Sense", 2, contentY + 48, "MOT", m12, 0, 0, false})
	L = append(L, label{"Sense", 32, contentY + 48 + 4, "[bar48]", m12, 48, 7, true})
	L = append(L, label{"Sense", 84, contentY + 48, "100", m12, 0, 0, false}) // 去 %
	L = append(L, alignRight("Sense", contentY+48, "motion", m12))
	// ---- NET 页 ----
	L = append(L, label{"NET", colLLabel, contentY + 0, "WIFI", m12, 0, 0, false})
	L = append(L, label{"NET", colLValue, contentY + 0, "[ssid]", m12, 120, 12, true})
	L = append(L, label{"NET", colLLabel, contentY + 13, "IP", m12, 0, 0, false})
	L = append(L, label{"NET", colLValue, contentY + 13, "255.255.255.255", m12, 0, 0, false})
	L = append(L, label{"NET", colLLabel, contentY + 26, "LINK", m12, 0, 0, false})
	L = append(L, label{"NET", colLValue, contentY + 26, "LOC", m12, 0, 0, false})
	L = append(L, alignRight("NET", contentY+26, "STRM OFF", m12))
	L = append(L, label{"NET", colLLabel, contentY + 39, "RSSI", m12, 0, 0, false})
	L = append(L, label{"NET", colLValue, contentY + 39, "-100 dBm", m12, 0, 0, false})
	// ---- SYS 页 ----
	L = append(L, label{"SYS", colLLabel, contentY + 0, "UP", m12, 0, 0, false})
	L = append(L, label{"SYS", colLValue, contentY + 0, "99h59m", m12, 0, 0, false})
	L = append(L, label{"SYS", colRLabel, contentY + 0, "HEAP", m12, 0, 0, false})
	L = append(L, label{"SYS", colRValue, contentY + 0, "999K", m12, 0, 0, false})
	L = append(L, label{"SYS", colLLabel, contentY + 13, "CSI", m12, 0, 0, false})
	L = append(L, label{"SYS", colLValue, contentY + 13, "99999k dr999", m12, 0, 0, false})
	L = append(L, alignRight("SYS", contentY+13, ver, m12))
	L = append(L, label{"SYS", colLLabel, contentY + 26, "FW", m12, 0, 0, false})
	L = append(L, label{"SYS", colLValue, contentY + 26, ver, m12, 0, 0, false})
	L = append(L, alignRight("SYS", contentY+26, "ESP32-C3", m12))
	L = append(L, center("SYS", contentY+55, "L/R PAGE U STRM D DIAG L+R LCD", lat8))
	return L
}

// ---- 字体实例 ----
var (
	m12, m22, lat8 font
)

func main() {
	root := findRoot()
	m12 = loadFont(filepath.Join(root, "managed_components", "lvgl__lvgl",
		"src", "font", "lv_font_montserrat_12.c"))
	m22 = loadFont(filepath.Join(root, "managed_components", "lvgl__lvgl",
		"src", "font", "lv_font_montserrat_22.c"))
	lat8 = loadFont(filepath.Join(root, "main", "fonts", "ui_font_lat8.c"))
	ver := parseVersion(filepath.Join(root, "main", "ui", "ui_common.h"))

	labels := buildModel(ver)
	issues := 0
	for _, l := range labels {
		x1, y1, x2, y2 := l.inkBox()
		if x1 < 0 || y1 < 0 || x2 > screenW || y2 > screenH {
			fmt.Printf("OUT-OF-BOUNDS [%s] '%s' ink=(%.0f,%.0f)..(%.0f,%.0f)\n",
				l.page, l.text, x1, y1, x2, y2)
			issues++
		}
	}
	byPage := map[string][]label{}
	for _, l := range labels {
		byPage[l.page] = append(byPage[l.page], l)
	}
	for page, lbs := range byPage {
		for i := 0; i < len(lbs); i++ {
			for j := i + 1; j < len(lbs); j++ {
				ax1, ay1, ax2, ay2 := lbs[i].inkBox()
				bx1, by1, bx2, by2 := lbs[j].inkBox()
				if ax1 < bx2 && bx1 < ax2 && ay1 < by2 && by1 < ay2 {
					fmt.Printf("OVERLAP [%s] '%s' (%.0f,%.0f)..(%.0f,%.0f) x "+
						"'%s' (%.0f,%.0f)..(%.0f,%.0f)\n",
						page, lbs[i].text, ax1, ay1, ax2, ay2,
						lbs[j].text, bx1, by1, bx2, by2)
					issues++
				}
			}
		}
	}
	fmt.Printf("%d issue(s)\n", issues)
	if issues > 0 {
		os.Exit(1)
	}
}

// findRoot: 从当前目录向上找 wifi-csi-sensing 根（含 main/ui/ui_common.h）。
func findRoot() string {
	dir, err := os.Getwd()
	must(err != nil, "getwd")
	for i := 0; i < 6; i++ {
		if _, err := os.Stat(filepath.Join(dir, "main", "ui", "ui_common.h")); err == nil {
			return dir
		}
		dir = filepath.Dir(dir)
	}
	must(true, "wifi-csi-sensing root not found (run inside the repo)")
	return ""
}

func parseVersion(path string) string {
	src, err := os.ReadFile(path)
	must(err != nil, "read ui_common.h")
	m := regexp.MustCompile(`#define APP_FW_VERSION "([^"]+)"`).FindStringSubmatch(string(src))
	must(m == nil, "APP_FW_VERSION not found")
	return "v" + strings.TrimPrefix(m[1], "v")
}
