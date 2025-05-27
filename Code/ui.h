#pragma once

#include <ncurses.h>
#include <menu.h>
#include <panel.h>

#include <pthread.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <unistd.h>
#include <malloc.h>
#include <ctype.h>
#include <stdlib.h>

#include "sfs.h"
#include "network.h"

#define TAB_COUNT 4
#define TAB_BAR_HEIGHT 3

enum ColorPairs {
    CP_DEFAULT = 1,
    CP_HIGHLIGHT,
    CP_TAB_ACTIVE,
    CP_TAB_INACTIVE,
    CP_STATUS,
    CP_BUTTON
};

typedef struct {
    char** lines;
    int line_count;
    int top_line;   
} content_buffer;

typedef struct {
    WINDOW *win;
    PANEL *panel;
    const char *title;
    void (*draw_content)(void);
    void (*handle_mouse)(MEVENT*);
} Tab;

typedef struct {
    int x, y, width, height;
    const char* label;
    void (*action)(void);
} Button;

extern Tab tabs[TAB_COUNT];
extern Button buttons[10];
extern int button_count;
extern int current_tab;
extern char sfs_name[MAX_NAME_LEN];
extern struct superblock sb;
extern int server_port;
extern pthread_t server_tid;

extern int selected_file;
extern char current_path[256];

void handle_sigwinch(int sig);

void init_ui(void);
void create_tabs(void);
void draw_tabs(void);
void switch_tab(int new_tab);
void handle_input(void);
void register_button(int x, int y, int w, int h, const char* label, void (*action)(void));
int check_button_click(MEVENT* mevent);
void draw_visible_lines(WINDOW* win, content_buffer* content, int height, int width);

void draw_files_tab(void);
void handle_files_mouse(MEVENT* mevent);
void draw_network_tab(void);
void handle_network_mouse(MEVENT* mevent);
void draw_tools_tab(void);
void handle_tools_mouse(MEVENT* mevent);
void draw_help_tab(void);
void handle_help_mouse(MEVENT* mevent);