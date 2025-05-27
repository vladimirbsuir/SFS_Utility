#include "ui.h"

Tab tabs[TAB_COUNT];
Button buttons[10];
int button_count = 0;
int current_tab = 0;
char sfs_name[MAX_NAME_LEN];
pthread_t server_tid;

int selected_file = -1;
char current_path[256] = "/";

void handle_sigwinch(int sig) {}

void init_ui(void) {
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);

    init_pair(CP_DEFAULT, COLOR_WHITE, COLOR_BLACK);
    init_pair(CP_HIGHLIGHT, COLOR_BLACK, COLOR_WHITE);
    init_pair(CP_TAB_ACTIVE, COLOR_WHITE, COLOR_BLUE);
    init_pair(CP_TAB_INACTIVE, COLOR_BLUE, COLOR_BLACK);
    init_pair(CP_STATUS, COLOR_BLACK, COLOR_WHITE);
    init_pair(CP_BUTTON, COLOR_BLACK, COLOR_CYAN);
}


void create_tabs(void) {
    const char *titles[TAB_COUNT] = {
        "Files", "Network", "Tools", "Help"
    };

    struct {
        void (*draw)(void);
        void (*handle_mouse)(MEVENT*);
    } tab_functions[TAB_COUNT] = {
        {draw_files_tab, handle_files_mouse},
        {draw_network_tab, handle_network_mouse},
        {draw_tools_tab, handle_tools_mouse},
        {draw_help_tab, handle_help_mouse}
    };
    
    int tab_height = LINES - TAB_BAR_HEIGHT - 1;
    
    for (int i = 0; i < TAB_COUNT; i++) {
        tabs[i].win = newwin(tab_height, COLS, TAB_BAR_HEIGHT, 0);
        tabs[i].panel = new_panel(tabs[i].win);
        tabs[i].title = titles[i];
        tabs[i].draw_content = tab_functions[i].draw;
        tabs[i].handle_mouse = tab_functions[i].handle_mouse;
        
        if (i != 0) hide_panel(tabs[i].panel);
    }
}

void draw_tabs(void) {
    int tab_width = COLS / TAB_COUNT;
    
    for (int i = 0; i < TAB_COUNT; i++) {
        if (i == current_tab) {
            attron(COLOR_PAIR(CP_TAB_ACTIVE));
        } else {
            attron(COLOR_PAIR(CP_TAB_INACTIVE));
        }
        
        mvprintw(0, i * tab_width, " %s ", tabs[i].title);
        attroff(COLOR_PAIR(CP_TAB_INACTIVE));
    }

    attron(COLOR_PAIR(CP_STATUS));
    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, " SFS: %s | Port: %d | Tab: %s ", 
             sfs_name, server_port, tabs[current_tab].title);
    attroff(COLOR_PAIR(CP_STATUS));
    
    refresh();
}

void switch_tab(int new_tab) {
    if (new_tab < 0 || new_tab >= TAB_COUNT) return;
    
    hide_panel(tabs[current_tab].panel);
    show_panel(tabs[new_tab].panel);
    current_tab = new_tab;
    
    // Обновляем порядок панелей
    top_panel(tabs[current_tab].panel);
}

void register_button(int x, int y, int w, int h, const char *label, void (*action)(void)) {
    if (button_count >= 10) return;
    
    buttons[button_count].x = x;
    buttons[button_count].y = y;
    buttons[button_count].width = w;
    buttons[button_count].height = h;
    buttons[button_count].label = label;
    buttons[button_count].action = action;
    
    // Отрисовка кнопки
    wattron(tabs[current_tab].win, COLOR_PAIR(CP_BUTTON));
    mvwprintw(tabs[current_tab].win, y, x, "[ %s ]", label);
    wattroff(tabs[current_tab].win, COLOR_PAIR(CP_BUTTON));
    
    button_count++;
}

int check_button_click(MEVENT *mevent) {
    if (mevent->y < TAB_BAR_HEIGHT) {
        int tab_width = COLS / TAB_COUNT;
        int clicked_tab = mevent->x / tab_width;
        if (clicked_tab < TAB_COUNT) {
            switch_tab(clicked_tab);
            return 1;
        }
        return 0;
    }
    
    // Проверка кликов по кнопкам
    /*for (int i = 0; i < button_count; i++) {
        if (mevent->x >= buttons[i].x && 
            mevent->x <= buttons[i].x + buttons[i].width &&
            mevent->y - TAB_BAR_HEIGHT >= buttons[i].y && 
            mevent->y - TAB_BAR_HEIGHT <= buttons[i].y + buttons[i].height) {
            
            buttons[i].action();
            return 1;
        }
    }*/

    if (tabs[current_tab].handle_mouse != NULL) tabs[current_tab].handle_mouse(mevent);
    return 1;
}

void handle_input(void) {
    int ch = getch();
    
    switch(ch) {
        case KEY_LEFT:
            switch_tab((current_tab - 1 + TAB_COUNT) % TAB_COUNT);
            break;
        case KEY_RIGHT:
            switch_tab((current_tab + 1) % TAB_COUNT);
            break;
        case '1'...'4':
            switch_tab(ch - '1');
            break;
        case KEY_MOUSE: {
            MEVENT event;
            if (getmouse(&event) == OK) {
                if (event.bstate & BUTTON1_PRESSED) {
                   check_button_click(&event);
                }
            }
            break;
        }
        case 'q':
            endwin();
            printf("\033[?1003l\n");
            exit(0);
        default:
            // Обработка специфичных для вкладки команд
            break;
    }
}

void draw_help_tab(void) {
    WINDOW *win = tabs[3].win;
    werase(win);
    
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "Help & Information");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, 3, 2, "Keyboard shortcuts:");
    mvwprintw(win, 4, 2, "Left/Right Arrows - Switch tabs");
    mvwprintw(win, 5, 2, "1-4 - Jump to tab");
    mvwprintw(win, 6, 2, "Mouse click to action");
    mvwprintw(win, 7, 2, "Q - Quit");
    
    wrefresh(win);
}

void create_file_dialog(WINDOW *win) {
    int row = 1;
    char path[MAX_PATH_LEN] = {0};
    int timeout_seconds = 10;
    time_t start_time = time(NULL);
    
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, row++, 2, "Enter file path, including file name:");
    wmove(win, row++, 2);
    wrefresh(win);

    mmask_t old_mask;
    mousemask(0, &old_mask);
    
    echo();
    curs_set(1);
    wtimeout(win, -1);
    wgetnstr(win, path, MAX_PATH_LEN);
    noecho();
    curs_set(0);

    row++;
    mvwprintw(win, row, 2, "Creating file: %s", path);
    wrefresh(win);

    row++;
    int32_t code = create_file(path);
    if (code > 0) {
        mvwprintw(win, row++, 2, "File was created successfully");
    } else if (code == -1) {
        mvwprintw(win, row++, 2, "Error: empty path");
    } else if (code == -2) {
        mvwprintw(win, row++, 2, "Error: invalid file name, need extension");
    } else if (code == -3) {
        mvwprintw(win, row++, 2, "There is no such directory");
    } else if (code == -4) {
        mvwprintw(win, row, 2, "File with this name already exists");
    }

    wtimeout(win, 100);
    time_t current_time;
    int ch;
    
    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);
        
        wattron(win, A_BLINK);
        mvwprintw(win, row, 2, "Auto-continue in: %2d sec ", remaining);
        wattroff(win, A_BLINK);
        wrefresh(win);

        ch = wgetch(win);
        if(ch == 27) {
            break;
        }
        
    } while(current_time - start_time < timeout_seconds);

    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
}

void delete_file_dialog(WINDOW* win) {
    int row = 1;
    char path[MAX_PATH_LEN] = {0};
    int timeout_seconds = 10;
    time_t start_time = time(NULL);
    
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, row++, 2, "Enter file path, including file name:");
    wmove(win, row++, 2);
    wrefresh(win);

    mmask_t old_mask;
    mousemask(0, &old_mask);
    
    echo();
    curs_set(1);
    wtimeout(win, -1);
    wgetnstr(win, path, MAX_PATH_LEN);
    noecho();
    curs_set(0);

    row++;
    mvwprintw(win, row++, 2, "Deleting file: %s", path);
    wrefresh(win);

    int8_t code = delete_file(path);
    if (code == 1) {
        mvwprintw(win, row++, 2, "File was deleted successfully");
    } else if (code == -1) {
        mvwprintw(win, row++, 2, "Error: empty path\n");
    } else if (code == -2) {
        mvwprintw(win, row++, 2, "Error: there is no such directory");
    } else if (code == -3) {
        mvwprintw(win, row++, 2, "Error: there is no such file in this directory");
    }

    wtimeout(win, 100);
    time_t current_time;
    int ch;
    
    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);
        
        wattron(win, A_BLINK);
        mvwprintw(win, row, 2, "Auto-continue in: %2d sec ", remaining);
        wattroff(win, A_BLINK);
        wrefresh(win);

        ch = wgetch(win);
        if(ch == 27) break;
        
    } while(current_time - start_time < timeout_seconds);

    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
}

void create_dir_dialog(WINDOW* win) {
    int row = 1;
    char path[MAX_PATH_LEN] = {0};
    int timeout_seconds = 10;
    time_t start_time = time(NULL);
    
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, row++, 2, "Enter directory path, including directory name:");
    wmove(win, row++, 2);
    wrefresh(win);

    mmask_t old_mask;
    mousemask(0, &old_mask);
    
    echo();
    curs_set(1);
    wtimeout(win, -1); 
    wgetnstr(win, path, MAX_PATH_LEN);
    noecho();
    curs_set(0);

    row++;
    mvwprintw(win, row, 2, "Creating directory: %s", path);
    wrefresh(win);

    row++;
    int8_t code = create_dir(path);
    if (code == 1) {
        mvwprintw(win, row++, 2, "Directory was created successfully");
    } else if (code == -1) {
        mvwprintw(win, row++, 2, "Error: empty path");
    } else if (code == -2) {
        mvwprintw(win, row++, 2, "Directory not found");
    }

    wtimeout(win, 100);
    time_t current_time;
    int ch;
    
    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);
        
        wattron(win, A_BLINK);
        mvwprintw(win, row, 2, "Auto-continue in: %2d sec ", remaining);
        wattroff(win, A_BLINK);
        wrefresh(win);

        ch = wgetch(win);
        if(ch == 27) break;
        
    } while(current_time - start_time < timeout_seconds);

    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
}

void delete_dir_dialog(WINDOW* win) {
    int8_t row = 1;
    char path[MAX_PATH_LEN] = {0};
    int timeout_seconds = 10; // Таймаут 10 секунд
    time_t start_time = time(NULL);
    
    // Настройка окна
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, row++, 2, "Enter directory path, including directory name:");
    wmove(win, row++, 2);
    wrefresh(win);

    // Временное отключение мыши
    mmask_t old_mask;
    mousemask(0, &old_mask);
    
    // Ввод имени файла
    echo();
    curs_set(1);
    wtimeout(win, -1); // Блокирующий ввод
    wgetnstr(win, path, MAX_PATH_LEN);
    noecho();
    curs_set(0);

    row++;

    // Отображение статуса
    mvwprintw(win, row++, 2, "Deleting directory: %s", path);
    wrefresh(win);

    int8_t code = delete_dir(path);

    if (code == 1) {
        mvwprintw(win, row++, 2, "Directory was deleted successfully");
    } else if (code == -1) {
        mvwprintw(win, row++, 2, "Error: empty path");
    } else if (code == -2) {
        mvwprintw(win, row++, 2, "Error: there is no such directory in path");
    } else if (code == -3) {
        mvwprintw(win, row++, 2, "Error: there is no such directory to delete");
    }
    wrefresh(win);

    // Настройка таймаута
    wtimeout(win, 100); // Обновление каждые 100 мс
    time_t current_time;
    int ch;
    
    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);
        
        // Визуализация таймера
        wattron(win, A_BLINK);
        mvwprintw(win, row, 2, "Auto-continue in: %2d sec ", remaining);
        wattroff(win, A_BLINK);
        wrefresh(win);

        // Проверка ввода
        ch = wgetch(win);
        if(ch == 27) break;
        
    } while(current_time - start_time < timeout_seconds);

    // Восстановление настроек
    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
}

void print_dir_dialog(WINDOW* win) {
    uint8_t row = 0;
    int col = 0;
    char path[MAX_PATH_LEN] = {0};
    int timeout_seconds = 10; // Таймаут 10 секунд
    time_t start_time = time(NULL);
    
    const int HEIGHT = 10;
    const int WIDTH = 50;
    // Настройка окна
    wclear(win);
    box(win, 0, 0);
    WINDOW* inner_win = derwin(win, HEIGHT, WIDTH, 1, 1);

    mvwprintw(inner_win, row++, col, "Enter directory path, including directory name:");
    wmove(inner_win, row++, col);
    wrefresh(win);

    // Временное отключение мыши
    mmask_t old_mask;
    mousemask(0, &old_mask);
    
    // Ввод имени файла
    echo();
    curs_set(1);
    wtimeout(inner_win, -1); // Блокирующий ввод
    wgetnstr(inner_win, path, MAX_PATH_LEN);
    noecho();
    curs_set(0);

    char** dirents = print_dir(path);
    wrefresh(inner_win);
    

    //mvwprintw(inner_win, HEIGHT - 2, 0, "%d", dirents_count);
    //wgetch(inner_win);
    if (isdigit(dirents[0][0]) && atoi(dirents[0]) != 0)
    {
        int dirents_count = atoi(dirents[0]);
        //int dirents_count = atoi(dirents[0]);
        content_buffer content = {0};
        //content.lines = malloc(sizeof(char*));
        //content.lines[0] = malloc(WIDTH + 1);
        content.line_count = 0;
        content.top_line = 0;
    
        char data[BLOCK_SIZE] = {0};
        int block_index = 0;
        int current_line_pos = 0;

        for (int i = 1; i < dirents_count; i++) {
            content.lines = realloc(content.lines, (content.line_count + 1) * sizeof(char*));
            content.lines[content.line_count] = malloc(WIDTH + 1);
            strncpy(content.lines[content.line_count], dirents[i], WIDTH);
            content.line_count++;
        }
        
        draw_visible_lines(inner_win, &content, HEIGHT, WIDTH);

        keypad(inner_win, TRUE);
        // Обработка навигации
        int ch;
        while((ch = wgetch(inner_win)) != 27) { // ESC для выхода
            switch(ch) {
                case KEY_UP:
                    if(content.top_line > 0) content.top_line--;
                    break;
                case KEY_DOWN:
                    if(content.top_line + HEIGHT - 2 < content.line_count) content.top_line++;
                    break;
                case KEY_PPAGE: // Page Up
                    content.top_line = (content.top_line - (HEIGHT - 2)) > 0 ? 
                    (content.top_line - (HEIGHT - 2)) : 0;
                    break;
                case KEY_NPAGE: // Page Down
                    content.top_line = (content.top_line + (HEIGHT - 2)) < (content.line_count - (HEIGHT - 2)) ?
                    (content.top_line + (HEIGHT - 2)) : (content.line_count - (HEIGHT - 2));
                    break;
                case KEY_HOME:
                    content.top_line = 0;
                    break;
                case KEY_END:
                    content.top_line = (content.line_count - (HEIGHT - 2)) > 0 ?
                    (content.line_count - (HEIGHT - 2)) : 0;
                    break;
            }
            draw_visible_lines(inner_win, &content, HEIGHT, WIDTH);
        }
        keypad(inner_win, FALSE);

        // Освобождение ресурсов
        for(int i = 0; i < content.line_count; i++) {
            free(content.lines[i]);
        }
        free(content.lines);
    } else if (!isdigit(dirents[0][0])) {
        wclear(inner_win);
        mvwprintw(inner_win, 0, col, dirents[0]);
        mvwprintw(inner_win, 1, col, "Press any key to continue...");
        wrefresh(inner_win);
        wgetch(inner_win);
    } else {
        wclear(inner_win);
        mvwprintw(inner_win, 0, col, "Directory is empty");
        mvwprintw(inner_win, 1, col, "Press any key to continue...");
        wrefresh(inner_win);
        wgetch(inner_win);
    }

    // Восстановление настроек
    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
    delwin(inner_win);
}


void write_to_file_dialog(WINDOW* parent_win) {
    int row = 0;
    int col = 0;
    char path[MAX_PATH_LEN] = {0};
    int timeout_seconds = 10;
    
    const int HEIGHT = 15;
    const int WIDTH = 60;
    WINDOW* win = newwin(HEIGHT + 2, WIDTH + 2, (LINES-15)/2 - 1, (COLS-60)/2 - 1);
    box(win, 0, 0);
    WINDOW* inner_win = derwin(win, HEIGHT, WIDTH, 1, 1);
    scrollok(inner_win, TRUE);
    wrefresh(win);
    
    // Временное отключение мыши
    mmask_t old_mask;
    mousemask(0, &old_mask);

    // Ввод пути к файлу
    mvwprintw(inner_win, row++, col, "Enter file path:");
    echo();
    curs_set(1);
    mvwgetnstr(inner_win, row++, col, path, MAX_PATH_LEN);
    
    // Проверка существования файла
    struct path_components path_c = parse_path(path);
    uint32_t parent_inode_num = find_parent_dir(path_c);
    int8_t status = -1;

    if (parent_inode_num == -1) {
        mvwprintw(inner_win, 2, 0, "Directory not found");
        mvwprintw(inner_win, 3, 0, "Press any key to continue");
        wgetch(inner_win);
        noecho();
        curs_set(0);
        mousemask(old_mask, NULL);
        delwin(win);
        delwin(inner_win);
        return;
    }

    noecho();

    struct inode dir_inode;
    read_inode(parent_inode_num, &dir_inode);
    
    char buffer[BLOCK_SIZE];
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) {
            //printf("Error: there is no such file in this directory\n");
            mvwprintw(inner_win, 2, 0, "Error: there is no such file in this directory");
            mvwprintw(inner_win, 3, 0, "Press any key to continue");
            wgetch(inner_win);
            noecho();
            curs_set(0);
            mousemask(old_mask, NULL);
            delwin(win);
            delwin(inner_win);
            free_path_component_struct(&path_c);
            return;
        }

        if (strcmp(objects[i].name, path_c.components[path_c.count - 1]) == 0) {
            //write_data_to_file(objects[i], path_c.components[path_c.count - 1]);       

            struct inode file_inode;
            read_inode(objects[i].inode_num, &file_inode);

            if (file_inode.type != FIL) {
                //printf("Error: '%s' is not a file\n", objects[i].name);
                mvwprintw(inner_win, 2, 0, "Error: '%s' is not a file", objects[i].name);
                mvwprintw(inner_win, 3, 0, "Press any key to continue");
                wgetch(inner_win);
                curs_set(0);
                mousemask(old_mask, NULL);
                delwin(win);
                delwin(inner_win);
                free_path_component_struct(&path_c);
                return;
            }

            mvwprintw(inner_win, row++, col, "Enter data (press ESC to finish):");
            wmove(inner_win, row++, col);
            wrefresh(inner_win);
            
            // Буфер для данных
            char content[BLOCK_SIZE] = {0};
            int ch;
            int pos = 0;
            size_t total_size = 0;
            int block_index = 0;

            uint32_t new_block_num = find_free_block();
            if (new_block_num == -1) {
                //printf("Error writing data to file\n");
                mvwprintw(inner_win, 2, 0, "Error writing data to file");
                mvwprintw(inner_win, 3, 0, "Press any key to continue");
                wgetch(inner_win);
                curs_set(0);
                mousemask(old_mask, NULL);
                delwin(win);
                delwin(inner_win);
                free_path_component_struct(&path_c);
                return;
            }
            file_inode.blocks[0] = new_block_num;
            set_block(new_block_num, 1);
            
            while(1) {
                ch = wgetch(inner_win);
                
                if (ch == 27) {
                    write_block(file_inode.blocks[block_index], content);
                    break;
                }

                content[total_size % BLOCK_SIZE] = ch;
                total_size++;

                if (total_size % BLOCK_SIZE == 0) {
                    write_block(file_inode.blocks[block_index], content);
                    block_index++;
                    if (block_index == 12) {
                        break;
                    }
                    new_block_num = find_free_block();
                    file_inode.blocks[block_index] = new_block_num;
                    set_block(new_block_num, 1);
                    for (int i = 0; i < BLOCK_SIZE; i++) {
                        content[i] = '\0';
                    }
                }

                mvwprintw(inner_win, row-1, col, "%s", content);
                wrefresh(inner_win);
            }

            file_inode.size = total_size;
            write_inode(objects[i].inode_num, &file_inode);
            status = 1;
            noecho();
            curs_set(0);

            break;
        }
    }

    wclear(inner_win);
    row = 0;
    // Отображение статуса
    mvwprintw(inner_win, row++, col, status == 1 ? "Success!" : "Error writing file!");
    
    time_t start_time = time(NULL);
    // Таймер автоматического закрытия
    time_t current_time;
    wtimeout(inner_win, 100);
    int ch;
    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);
        mvwprintw(inner_win, row, 2, "Auto-closing in %2d sec...", remaining);
        wrefresh(inner_win);
    } while((ch = wgetch(inner_win)) != 27 && (current_time - start_time < timeout_seconds));
    
    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
    delwin(win);
    delwin(inner_win);
}


void draw_visible_lines(WINDOW* win, content_buffer* content, int height, int width) {
    werase(win);
    int lines_to_show = (content->line_count - content->top_line) < (height - 2) ? 
                        (content->line_count - content->top_line) : (height - 2);
    
    for(int i = 0; i < lines_to_show; i++) {
        mvwprintw(win, i, 0, "%.*s", width, content->lines[content->top_line + i]);
    }

    // Статусная строка
    mvwprintw(win, height - 1, 0, "Line %d-%d of %d (Arrows - scroll, Esc - exit)", 
             content->top_line + 1, 
             (content->top_line + height - 1) < content->line_count ? 
                (content->top_line + height - 1) : content->line_count,
             content->line_count);
    
    wrefresh(win);
}

void read_file_dialog(WINDOW* parent_win) {
    int row = 0;
    int col = 0;
    char path[MAX_PATH_LEN] = {0};
    
    const int HEIGHT = LINES - 10;
    const int WIDTH = COLS - 20;
    WINDOW* win = newwin(HEIGHT + 2, WIDTH + 2, 5, 10);
    box(win, 0, 0);
    WINDOW* inner_win = derwin(win, HEIGHT, WIDTH, 1, 1);
    scrollok(inner_win, TRUE);
    wrefresh(win);

    // Временное отключение мыши
    mmask_t old_mask;
    mousemask(0, &old_mask);

    // Ввод пути к файлу
    mvwprintw(inner_win, row++, col, "Enter file path to read:");
    echo();
    curs_set(1);
    mvwgetnstr(inner_win, row++, col, path, MAX_PATH_LEN);
    noecho();
    curs_set(0);
    
    // Получаем информацию о файле
    struct path_components pc = parse_path(path);
    uint32_t parent_inode = find_parent_dir(pc);
    
    if(parent_inode == -1) {
        mvwprintw(inner_win, row++, 2, "Error: Invalid path");
        mvwprintw(inner_win, row, col, "Press any key to continue...");
        wrefresh(inner_win);
        wgetch(inner_win);
        mousemask(old_mask, NULL);
        delwin(inner_win);
        delwin(win);
        return;
    }

    // Читаем данные файла
    struct inode file_inode;
    struct dirent file_entry;
    int found = 0;
    
    // Ищем файл в родительском каталоге
    char buffer[BLOCK_SIZE];
    read_inode(parent_inode, &file_inode);
    read_block(file_inode.blocks[0], buffer);
    struct dirent* entries = (struct dirent*)buffer;
    
    for(int i = 0; i < BLOCK_SIZE/sizeof(struct dirent); i++) {
        if(entries[i].inode_num == 0) break;
        
        if(strcmp(entries[i].name, pc.components[pc.count-1]) == 0) {
            file_entry = entries[i];
            found = 1;
            break;
        }
    }
    
    if(!found) {
        mvwprintw(inner_win, row++, col, "Error: File not found");
        mvwprintw(inner_win, row, col, "Press any key to continue...");
        wrefresh(inner_win);
        wgetch(inner_win);
        mousemask(old_mask, NULL);
        delwin(win);
        delwin(inner_win);
        return;
    }
    
    read_inode(file_entry.inode_num, &file_inode);
    
    if(file_inode.type != FIL) {
        mvwprintw(inner_win, row++, col, "Error: Not a file");
        mvwprintw(inner_win, row, col, "Press any key to continue...");
        wrefresh(inner_win);
        wgetch(inner_win);
        mousemask(old_mask, NULL);
        delwin(win);
        delwin(inner_win);
        return;
    }
    
    mvwprintw(inner_win, row++, col, "Data in '%s':", file_entry.name);
    wmove(inner_win, row, col);
    wrefresh(inner_win);
    
    content_buffer content = {0};
    content.lines = malloc(sizeof(char*));
    content.lines[0] = malloc(WIDTH + 1);
    content.line_count = 1;
    content.top_line = 0;

    char data[BLOCK_SIZE] = {0};
    int block_index = 0;
    int current_line_pos = 0;
    
    while(file_inode.blocks[block_index] != 0 && block_index < MAX_BLOCK_COUNT) {
        read_block(file_inode.blocks[block_index], data);
        
        for(int i = 0; i < BLOCK_SIZE && data[i] != '\0'; i++) {
            if (data[i] == '\n' || current_line_pos >= WIDTH - 1) {
                content.lines = realloc(content.lines, (content.line_count + 1) * sizeof(char*));
                content.lines[content.line_count] = malloc(WIDTH + 1);
                content.line_count++;
                current_line_pos = 0;
            }

            if (data[i] != '\n') {
                content.lines[content.line_count - 1][current_line_pos++] = data[i];
                content.lines[content.line_count - 1][current_line_pos] = '\0';
            }
            
            //waddch(inner_win, data[i]);
        }
        
        block_index++;
    }


    // Первоначальная отрисовка
    draw_visible_lines(inner_win, &content, HEIGHT, WIDTH);

    keypad(inner_win, TRUE);
    // Обработка навигации
    int ch;
    while((ch = wgetch(inner_win)) != 27) { // ESC для выхода
        switch(ch) {
            case KEY_UP:
                if(content.top_line > 0) content.top_line--;
                break;
            case KEY_DOWN:
                if(content.top_line + HEIGHT - 2 < content.line_count) content.top_line++;
                break;
            case KEY_PPAGE: // Page Up
                content.top_line = (content.top_line - (HEIGHT - 2)) > 0 ? 
                (content.top_line - (HEIGHT - 2)) : 0;
                break;
            case KEY_NPAGE: // Page Down
                content.top_line = (content.top_line + (HEIGHT - 2)) < (content.line_count - (HEIGHT - 2)) ?
                (content.top_line + (HEIGHT - 2)) : (content.line_count - (HEIGHT - 2));
                break;
            case KEY_HOME:
                content.top_line = 0;
                break;
            case KEY_END:
                content.top_line = (content.line_count - (HEIGHT - 2)) > 0 ?
                (content.line_count - (HEIGHT - 2)) : 0;
                break;
        }
        draw_visible_lines(inner_win, &content, HEIGHT, WIDTH);
    }
    keypad(inner_win, FALSE);

    // Освобождение ресурсов
    for(int i = 0; i < content.line_count; i++) {
        free(content.lines[i]);
    }
    free(content.lines);
    
    mousemask(old_mask, NULL);
    delwin(inner_win);
    delwin(win);
}

/* READ FILE*/

void handle_files_mouse(MEVENT *mevent) {
    int win_y = mevent->y - TAB_BAR_HEIGHT;
    int win_x = mevent->x;
    WINDOW *win = tabs[current_tab].win;

    if (win_y == 3 && win_x >= 2 && win_x <= 13) { // Create file
        WINDOW* dialog_win = newwin(10, 50, (LINES - 10) / 2, (COLS - 50) / 2);
        create_file_dialog(dialog_win);
        delwin(dialog_win);
    } else if (win_y == 3 && win_x >= 16 && win_x <= 29) { // Delete file
        WINDOW* dialog_win = newwin(10, 50, (LINES - 10) / 2, (COLS - 50) / 2);
        delete_file_dialog(dialog_win);
        delwin(dialog_win);
    } else if (win_y == 5 && win_x >= 2 && win_x <= 21) { // Create directory
        WINDOW* dialog_win = newwin(10, 50, (LINES - 10) / 2, (COLS - 50) / 2);
        create_dir_dialog(dialog_win);
        delwin(dialog_win);
    } else if (win_y == 5 && win_x >= 24 && win_x <= 42) { // Delete directory
        WINDOW* dialog_win = newwin(10, 50, (LINES - 10) / 2, (COLS - 50) / 2);
        delete_dir_dialog(dialog_win);
        delwin(dialog_win);
    } else if (win_y == 7 && win_x >= 2 && win_x <= 20) { // Print directory
        WINDOW* dialog_win = newwin(12, 52, (LINES - 10) / 2, (COLS - 50) / 2);
        print_dir_dialog(dialog_win);
        delwin(dialog_win);
    } else if (win_y == 9 && win_x >= 2 && win_x <= 22) {
        //WINDOW* dialog_win = newwin(15, 60, (LINES-15)/2, (COLS-60)/2);
        write_to_file_dialog(NULL);
        //delwin(dialog_win);
    } else if (win_y == 9 && win_x >= 26 && win_x <= 49) {
        read_file_dialog(NULL);
    }
}

void send_file_dialog(WINDOW* win) {
    int row = 1;
    char filepath[MAX_PATH_LEN] = {0};
    char ip[16];
    int port;
    int timeout_seconds = 10; // Таймаут 10 секунд
    
    // Настройка окна
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, row++, 2, "Enter file path, including file name:");
    wmove(win, row++, 2);
    wrefresh(win);

    // Временное отключение мыши
    mmask_t old_mask;
    mousemask(0, &old_mask);
    
    // Ввод имени файла
    echo();
    curs_set(1);

    wtimeout(win, -1); // Блокирующий ввод
    wgetnstr(win, filepath, MAX_PATH_LEN);

    mvwprintw(win, row++, 2, "Enter ip:");
    wmove(win, row++, 2);
    wrefresh(win);
    wgetnstr(win, ip, 16);

    char buffer[5];
    mvwprintw(win, row++, 2, "Enter port:");
    wmove(win, row++, 2);
    wrefresh(win);
    wgetnstr(win, buffer, 5);
    port = atoi(buffer);

    noecho();
    curs_set(0);

    int8_t code = send_file(filepath, ip, port);
    if (code == 1) {
        mvwprintw(win, row++, 2, "File was sended successfully");
    } else if (code == -1) {
        mvwprintw(win, row++, 2, "Connection failed");
    } else if (code == -2) {
        mvwprintw(win, row++, 2, "There is no such directory");
    } else if (code == -3) {
        mvwprintw(win, row++, 2, "There is no such file in this directory");
    } else if (code == -4) {
        mvwprintw(win, row++, 2, "Timeout waiting for response");
    }
    wrefresh(win);

    // Настройка таймаута
    wtimeout(win, 100); // Обновление каждые 100 мс
    time_t current_time;
    int ch;
    
    time_t start_time = time(NULL);
    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);

        // Визуализация таймера
        wattron(win, A_BLINK);
        mvwprintw(win, row, 2, "Auto-continue in: %2d sec ", remaining);
        wattroff(win, A_BLINK);
        wrefresh(win);

        // Проверка ввода
        ch = wgetch(win);
        if(ch == 27) break;
        
    } while(current_time - start_time < timeout_seconds);

    // Восстановление настроек
    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
}

void check_incoming_requests_dialog(WINDOW* win) {
    int row = 1;
    char filepath[MAX_PATH_LEN] = {0};
    char ip[16];
    int port;
    int timeout_seconds = 10; // Таймаут 10 секунд
    
    // Настройка окна
    wclear(win);
    box(win, 0, 0);
    wrefresh(win);

    // Временное отключение мыши
    mmask_t old_mask;
    mousemask(0, &old_mask);
    
    check_incoming_requests(win, &row);
    wrefresh(win);
    row++;

    time_t current_time;
    wtimeout(win, 100);
    int ch;
    
    time_t start_time = time(NULL);
    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);

        // Визуализация таймера
        wattron(win, A_BLINK);
        mvwprintw(win, row, 2, "Auto-continue in: %2d sec ", remaining);
        wattroff(win, A_BLINK);
        wrefresh(win);

        // Проверка ввода
        ch = wgetch(win);
        if(ch == 27) break;
        
    } while(current_time - start_time < timeout_seconds);

    // Восстановление настроек
    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
}

// Реализация для вкладки Network
void handle_network_mouse(MEVENT *mevent) {
    int win_y = mevent->y - TAB_BAR_HEIGHT;
    int win_x = mevent->x;

    if (win_x >= 2 && win_x <= 14 && win_y == 3) {
        WINDOW* dialog_win = newwin(10, 50, (LINES - 10) / 2, (COLS - 50) / 2);
        send_file_dialog(dialog_win);
        delwin(dialog_win);
    }
    
    if (win_y >= 4 && win_y <= 6 && win_x >= 2 && win_x <= 28) {
        WINDOW* dialog_win = newwin(10, 50, (LINES - 10) / 2, (COLS - 50) / 2);
        check_incoming_requests_dialog(dialog_win);
        delwin(dialog_win);
    }
}

void check_filesystem_dialog(WINDOW* win) {
    int row = 1;
    int timeout_seconds = 10; // Таймаут 10 секунд
    
    // Настройка окна
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, row++, 2, "Checking filesystem...");
    wmove(win, row++, 2);
    wrefresh(win);

    // Временное отключение мыши
    mmask_t old_mask;
    mousemask(0, &old_mask);

    check_blocks(win, &row);
    row++;
    check_metadata(win, &row);
    row++;
    check_duplicates(win, &row);
    row++;
    //check_dirs(0, 0);

    // Настройка таймаута
    wtimeout(win, 100); // Обновление каждые 100 мс
    time_t current_time;
    int ch;
    
    time_t start_time = time(NULL);
    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);

        // Визуализация таймера
        wattron(win, A_BLINK);
        mvwprintw(win, row, 2, "Auto-continue in: %2d sec ", remaining);
        wattroff(win, A_BLINK);
        wrefresh(win);

        // Проверка ввода
        ch = wgetch(win);
        if(ch == 27) break;
        
    } while(current_time - start_time < timeout_seconds);

    // Восстановление настроек
    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
}

void format_filesystem_dialog(WINDOW* win) {
    int row = 1;
    int timeout_seconds = 10; // Таймаут 10 секунд
    
    // Настройка окна
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, row++, 2, "Formatting filesystem...");
    wmove(win, row++, 2);
    wrefresh(win);

    // Временное отключение мыши
    mmask_t old_mask;
    mousemask(0, &old_mask);

    defragment(win, &row);
    row++;

    time_t start_time = time(NULL);
    // Настройка таймаута
    wtimeout(win, 100); // Обновление каждые 100 мс
    time_t current_time;
    int ch;

    do {
        current_time = time(NULL);
        int remaining = timeout_seconds - (current_time - start_time);

        // Визуализация таймера
        wattron(win, A_BLINK);
        mvwprintw(win, row, 2, "Auto-continue in: %2d sec ", remaining);
        wattroff(win, A_BLINK);
        wrefresh(win);

        // Проверка ввода
        ch = wgetch(win);
        if(ch == 27) break;
        
    } while(current_time - start_time < timeout_seconds);

    // Восстановление настроек
    mousemask(old_mask, NULL);
    wtimeout(win, -1);
    wclear(win);
    wrefresh(win);
}

// Реализация для вкладки Tools
void handle_tools_mouse(MEVENT *mevent) {
    int win_y = mevent->y - TAB_BAR_HEIGHT;
    int win_x = mevent->x;
    
    if (win_y == 3 && win_x >= 2 && win_x <= 31) {
        WINDOW* dialog_win = newwin(10, 50, (LINES - 10) / 2, (COLS - 50) / 2);
        check_filesystem_dialog(dialog_win);
        delwin(dialog_win);
    } 
    
    if (win_y == 5 && win_x >= 2 && win_x <= 15) {
        WINDOW* dialog_win = newwin(10, 50, (LINES - 10) / 2, (COLS - 50) / 2);
        format_filesystem_dialog(dialog_win);
        delwin(dialog_win);
    }
}

// Реализация для вкладки Help
void handle_help_mouse(MEVENT *mevent) {
    int win_y = mevent->y - TAB_BAR_HEIGHT;
    
    // Прокрутка содержимого
    if (win_y >= LINES - TAB_BAR_HEIGHT - 2) {
        // Обработка скролла
        // Можно добавить логику прокрутки текста
    }
}

void draw_files_tab(void) {
    WINDOW *win = tabs[0].win;
    werase(win);
    button_count = 0;
    
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "File Manager:");
    wattroff(win, A_BOLD);
    
    register_button(2, 3, 8, 1, "New file", NULL);
    register_button(16, 3, 11, 1, "Delete file", NULL);
    register_button(2, 5, 16, 1, "Create directory", NULL);
    register_button(24, 5, 16, 1, "Delete directory", NULL);
    register_button(2, 7, 15, 1, "Print directory", NULL);
    register_button(2, 9, 18, 1, "Write data to file", NULL);
    register_button(26, 9, 19, 1, "Read data from file", NULL);
    //register_button(30, 3, 12, 1, "Rename", rename_file);
    
    wrefresh(win);
}

void draw_network_tab(void) {
    WINDOW *win = tabs[1].win;
    werase(win);
    button_count = 0;
    
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "Network Managere");
    wattroff(win, A_BOLD);
    
    register_button(2, 3, 13, 1, "Send file", NULL);
    register_button(2, 5, 27, 1, "Check incoming requests", NULL);
    
    wrefresh(win);
}

void draw_tools_tab(void) {
    WINDOW *win = tabs[2].win;
    werase(win);
    button_count = 0;
    
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "System Tools");
    wattroff(win, A_BOLD);
    
    register_button(2, 3, 28, 1, "Check filesystem integrity", NULL);
    register_button(2, 5, 18, 1, "Defragment", NULL);
    register_button(2, 7, 18, 1, "Clear all files", NULL);
    
    wrefresh(win);
}