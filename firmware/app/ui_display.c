#include "ui_display.h"
#include "app_config.h"
#include "lcd_st7735.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
_Static_assert(UI_COLS * 6u >= LCD_W && UI_ROWS * 8u >= LCD_H,
               "UI maximum grid does not fit rotated panel");

/* Project-owned 5x7 bitmap glyphs: seven rows, most significant bit at left.
 * Each 6x8 cell includes a cleared spacer column and row. */
static const uint8_t s_digits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};
static const uint8_t s_letters[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
uint8_t ui_ascii_row(char c, unsigned row)
{
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return s_letters[c-'A'][row];
    if (c >= '0' && c <= '9') return s_digits[c-'0'][row];
    switch (c) {
    case ' ': return 0;
    case ':': return row == 2 || row == 5 ? 4 : 0;
    case '.': return row == 6 ? 4 : 0;
    case '-': return row == 3 ? 14 : 0;
    case '_': return row == 6 ? 31 : 0;
    case '/': return row > 0 && row < 6 ? 1u << (row-1) : 0;
    case '=': return row == 2 || row == 4 ? 14 : 0;
    case '+': return row == 3 ? 14 : row == 2 || row == 4 ? 4 : 0;
    case '*': return row == 2 || row == 4 ? 10 : row == 3 ? 4 : 0;
    case '#': return row == 2 || row == 4 ? 31 : row > 0 && row < 6 ? 10 : 0;
    case '>': return row < 4 ? 1u << (3-row) : 1u << (row-3);
    default: return row == 0 ? 14 : row == 1 ? 17 : row == 2 ? 2 : row == 3 || row == 5 ? 4 : 0;
    }
}

static ui_frame_t s_shown;
static unsigned s_cursor;
static const uint16_t s_colors[] = {LCD_WHITE, LCD_CYAN, LCD_GREEN, LCD_YELLOW, LCD_RED, LCD_GRAY,
                                  LCD_BLACK,LCD_WHITE};

void ui_cache_reset(void)
{
    s_shown.cols = (uint8_t)((lcd_width()+5)/6);
    s_shown.rows = (uint8_t)(lcd_height()/8);
    s_shown.theme=UI_THEME_BLACK;
    for (unsigned y = 0; y < UI_ROWS; ++y)
        for (unsigned x = 0; x < UI_COLS; ++x)
            s_shown.cells[y][x] = (ui_cell_t){.ch=' ',.color=UI_WHITE};
    s_cursor = 0;
}

static int equal_cell(ui_cell_t a, ui_cell_t b)
{
    if (a.flags!=b.flags) return 0;
    if (a.flags & UI_CELL_BITMAP) return a.color==b.color && !memcmp(a.pixels,b.pixels,sizeof a.pixels);
    return a.ch == b.ch && (a.ch == ' ' || a.color == b.color);
}

static void draw_cell(unsigned x, unsigned y, ui_cell_t cell, unsigned theme)
{
    int origin = 0;
    int px = origin + (int)x * 6, py = (int)y * 8;
    uint8_t pixels[8];
    uint16_t fg = s_colors[cell.color < sizeof s_colors / sizeof s_colors[0] ? cell.color : UI_WHITE];
    for (unsigned row = 0; row < 8; ++row) {
        pixels[row]=(cell.flags & UI_CELL_BITMAP) ? cell.pixels[row] : row<7 ? ui_ascii_row(cell.ch,row)<<1 : 0;
    }
    uint16_t bg=theme==UI_THEME_RETRO ? s_colors[UI_PAPER] : LCD_BLACK;
    if(px+6<=lcd_width()) lcd_draw_cell(px,py,pixels,fg,bg);
    else for(unsigned row=0;row<8;++row) for(unsigned col=0;col<6 && px+(int)col<lcd_width();++col)
        lcd_draw_pixel(px+(int)col,py+(int)row,(pixels[row]&(32u>>col))?fg:bg);
}

static void cell_bounds(const ui_frame_t *frame,unsigned x,unsigned y,unsigned cols,unsigned rows,int bounds[4])
{
    int left=(int)x*6,top=(int)y*8,right=(int)(x+cols)*6,bottom=(int)(y+rows)*8;
    const int panel_width=lcd_width();
    int min_x=right,min_y=bottom,max_x=-1,max_y=-1,trim=1;
    /* Bitmap tiles identify dirt cheaply; transfer bounds can still be tight
     * to the changed pixels. Every changed pixel must lie in this window. */
    for(unsigned row=y;row<y+rows && trim;++row) for(unsigned col=x;col<x+cols;++col) {
        const ui_cell_t *a=&s_shown.cells[row][col],*b=&frame->cells[row][col];
        if(a->flags!=UI_CELL_BITMAP || b->flags!=UI_CELL_BITMAP || a->color!=b->color) {trim=0;break;}
        for(unsigned dy=0;dy<8;++dy) {
            unsigned changed=(a->pixels[dy]^b->pixels[dy])&63u;
            int px=(int)col*6;
            if(px+6>panel_width) changed&=(63u<<(px+6-panel_width))&63u;
            if(!changed) continue;
            int py=(int)row*8+(int)dy;
            int first=px+__builtin_clz(changed)-26,last=px+5-__builtin_ctz(changed);
            if(first<min_x) min_x=first;
            if(last>max_x) max_x=last;
            if(py<min_y) min_y=py;
            if(py>max_y) max_y=py;
        }
    }
    if(trim && max_x>=min_x && max_y>=min_y) {
        left=min_x;top=min_y;right=max_x+1;bottom=max_y+1;
    }
    if(right>panel_width) right=panel_width;
    bounds[0]=left;bounds[1]=top;bounds[2]=right;bounds[3]=bottom;
}
static void send_cells(const ui_frame_t *frame,unsigned x,unsigned y,unsigned cols,unsigned rows,const int known_bounds[4])
{
    int computed[4];
    if(!known_bounds) {cell_bounds(frame,x,y,cols,rows,computed);known_bounds=computed;}
    for(unsigned row=y;row<y+rows;++row) for(unsigned col=x;col<x+cols;++col)
        draw_cell(col,row,frame->cells[row][col],frame->theme);
    lcd_flush_rect(known_bounds[0],known_bounds[1],known_bounds[2]-known_bounds[0],known_bounds[3]-known_bounds[1]);
}

int ui_flush_step(const ui_frame_t *frame, unsigned budget)
{
    if (lcd_get_error() != LCD_ERROR_NONE) return -1;
    if (frame->cols != s_shown.cols || frame->rows != s_shown.rows || frame->theme!=s_shown.theme) return -1;
    unsigned total = (unsigned)frame->cols * frame->rows;
    if(budget>=total) {
        /* Compact scrolling viewports benefit from one window. Sparse cursor
         * changes retain runs when their bounding box wastes too much data. */
        uint32_t dirty[UI_ROWS]={0};
        unsigned count=0,runs=0,min_x=frame->cols,min_y=frame->rows,max_x=0,max_y=0;
        for(unsigned y=0;y<frame->rows;++y) {
            int in_run=0;
            for(unsigned x=0;x<frame->cols;++x) {
                int changed=!equal_cell(s_shown.cells[y][x],frame->cells[y][x]);
                if(changed) {
                    dirty[y]|=1u<<x;
                    ++count; if(!in_run) ++runs;
                    if(x<min_x) min_x=x;
                    if(x>max_x) max_x=x;
                    if(y<min_y) min_y=y;
                    if(y>max_y) max_y=y;
                }
                in_run=changed;
            }
        }
        if(!count) return 0;
        int bounds[4];
        cell_bounds(frame,min_x,min_y,max_x-min_x+1u,max_y-min_y+1u,bounds);
        unsigned area=(unsigned)(bounds[2]-bounds[0])*(unsigned)(bounds[3]-bounds[1]);
        /* Keep bounding-box overdraw below ~16 ms of wire time at the
         * verified 6.25 MHz, leaving room for composition and RTOS work. */
        if(area<=6144u && area<=count*48u+runs*256u) {
            send_cells(frame,min_x,min_y,max_x-min_x+1u,max_y-min_y+1u,bounds);
            if(lcd_get_error()!=LCD_ERROR_NONE) return -1;
            s_shown=*frame; s_cursor=0; return (int)count;
        }
        /* Merge equal dirty spans vertically as well as horizontally. Narrow
         * rectangles are still packed by the unchanged driver in <=16-row
         * blocks. No clean cells are added by this merge. */
        for(unsigned y=0;y<frame->rows;++y) for(unsigned x=0;x<frame->cols;) {
            if(!(dirty[y]&(1u<<x))) {++x;continue;}
            unsigned width=1;
            while(x+width<frame->cols && (dirty[y]&(1u<<(x+width)))) ++width;
            uint32_t mask=((1u<<width)-1u)<<x;
            unsigned height=1;
            while(y+height<frame->rows && (dirty[y+height]&mask)==mask) ++height;
            send_cells(frame,x,y,width,height,NULL);
            if(lcd_get_error()!=LCD_ERROR_NONE) return -1;
            for(unsigned row=y;row<y+height;++row) {
                dirty[row]&=~mask;
                memcpy(&s_shown.cells[row][x],&frame->cells[row][x],width*sizeof(ui_cell_t));
            }
            x+=width;
        }
        s_cursor=0;return (int)count;
    }
    unsigned done = 0, scanned = 0;
    /* Fair cyclic scan: frequently changing counters cannot starve lower rows. */
    while (scanned < total && done < budget) {
        unsigned y = s_cursor / frame->cols, x = s_cursor % frame->cols;
        if (equal_cell(s_shown.cells[y][x], frame->cells[y][x])) {
            s_cursor = (s_cursor + 1u) % total; ++scanned;
            continue;
        }
        unsigned count = 0;
        while (x + count < frame->cols && done + count < budget &&
               scanned + count < total &&
               !equal_cell(s_shown.cells[y][x+count], frame->cells[y][x+count])) {
            draw_cell(x+count, y, frame->cells[y][x+count],frame->theme);
            ++count;
        }
        unsigned rows=1;
        if(x==0u && count==frame->cols && y+1u<frame->rows &&
           done+count*2u<=budget && scanned+count*2u<=total) {
            unsigned i=0;
            while(i<count && !equal_cell(s_shown.cells[y+1u][i],frame->cells[y+1u][i])) ++i;
            if(i==count) {
                rows=2;
                for(i=0;i<count;++i) draw_cell(i,y+1u,frame->cells[y+1u][i],frame->theme);
            }
        }
        int origin = 0;
        int width=(int)count*6;
        if((int)x*6+width>lcd_width()) width=lcd_width()-(int)x*6;
        lcd_flush_rect(origin+(int)x*6,(int)y*8,width,(int)rows*8);
        if (lcd_get_error() != LCD_ERROR_NONE) return -1;
        for(unsigned row=0;row<rows;++row)
            for (unsigned i = 0; i < count; ++i) s_shown.cells[y+row][x+i] = frame->cells[y+row][x+i];
        count*=rows;
        s_cursor = (s_cursor + count) % total;
        done += count; scanned += count;
    }
    return (int)done;
}

int ui_render_full(const ui_frame_t *f)
{
    if (lcd_get_error() != LCD_ERROR_NONE) return -1;
    if (f->cols != (lcd_width()+5)/6 || f->rows != lcd_height()/8) return -1;
    lcd_fill(f->theme==UI_THEME_RETRO ? s_colors[UI_PAPER] : LCD_BLACK);
    for (unsigned y = 0; y < f->rows; ++y)
        for (unsigned x = 0; x < f->cols; ++x)
            if ((f->cells[y][x].flags&UI_CELL_BITMAP) || f->cells[y][x].ch != ' ')
                draw_cell(x, y, f->cells[y][x],f->theme);
    lcd_flush();
    if (lcd_get_error() != LCD_ERROR_NONE) return -1;
    s_shown = *f; s_cursor = 0;
    return f->cols * f->rows;
}
