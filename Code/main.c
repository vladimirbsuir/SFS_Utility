#include "sfs.h"
#include "ui.h"
#include "network.h"
#include "aes.h"

int main() {
    signal(SIGWINCH, handle_sigwinch);
    // Инициализация файловой системы
    //echo();
    //set_terminal_size(31, 99);
    printf("Enter file system name (without spaces): ");
    scanf("%s", sfs_name);
    //getstr(sfs_name);
    printf("Enter server port: ");
    scanf("%d", &server_port);
    //noecho();

    sfs_init(sfs_name);
    read_sb(&sb);
    pthread_create(&server_tid, NULL, server_thread, NULL);

    // Инициализация интерфейса
    init_ui();
    create_tabs();
    
    // Основной цикл
    while(1) {
        draw_tabs();
        tabs[current_tab].draw_content();
        update_panels();
        doupdate();
        handle_input();
    }

    endwin();
    return 0;
}
