
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "city_manager.h"

//Parsare
Role parse_role(const char *s) {
    if (strcmp(s, "manager") == 0)  
    {
        return ROLE_MANAGER;
    }
    if (strcmp(s, "inspector") == 0) 
    {
        return ROLE_INSPECTOR;
    }

    printf("rol necunoscut: %s\n", s);
    return -1;
}

Operation parse_operation(const char *s) {
    if (strcmp(s, "--add")== 0) return OP_ADD;
    if (strcmp(s, "--list")== 0) return OP_LIST;
    if (strcmp(s, "--view")== 0) return OP_VIEW;
    if (strcmp(s, "--remove_report")== 0) return OP_REMOVE_REPORT;
    if (strcmp(s, "--update_threshold")== 0) return OP_UPDATE_THRESHOLD;
    if (strcmp(s, "--filter")== 0) return OP_FILTER;
    return OP_UNKNOWN;
}

//PERMISIUNi
 
//la inceput, punem '-' peste tot, iar unde trebuie, adica avem permisiune suprascriem

void mode_to_str(mode_t mode, char out[10]) {
    for(int i=0; i<9; i++) out[i] = '-';
    out[9] = '\0';

    if (mode & S_IRUSR) out[0] = 'r';
    if (mode & S_IWUSR) out[1] = 'w';
    if (mode & S_IXUSR) out[2] = 'x';
    
    if (mode & S_IRGRP) out[3] = 'r';
    if (mode & S_IWGRP) out[4] = 'w';
    if (mode & S_IXGRP) out[5] = 'x';
    
    if (mode & S_IROTH) out[6] = 'r';
    if (mode & S_IWOTH) out[7] = 'w';
    if (mode & S_IXOTH) out[8] = 'x';
}

//Verifica daca rolul, adica manager sau inspector are dreptul r/w 

int check_permission(const char *path, Role role, int need_write) {
    struct stat st;
    if (stat(path, &st) == -1) {
        return 1;
    }
    mode_t m = st.st_mode;
    
    if (role == ROLE_MANAGER) {
        if (need_write == 1) {
            if ((m & S_IWUSR) != 0) return 1;
            else return 0;
        } else {
            if ((m & S_IRUSR) != 0) return 1;
            else return 0;
        }
    } else {
        //aici e rol inspector
        if (need_write == 1) {
            if ((m & S_IWGRP) != 0) return 1;
            else return 0;
        } else {
            if ((m & S_IRGRP) != 0) return 1;
            else return 0;
        }
    }
}

//Paths 
//le folosim ca sa nu sxcriem mereu de mana 

void district_dir(const char *district, char *out) {
    snprintf(out, PATH_MAX_, "%s", district);
}

void reports_path(const char *district, char *out) {
    snprintf(out, PATH_MAX_, "%s/reports.dat", district);
}

void cfg_path(const char *district, char *out) {
    snprintf(out, PATH_MAX_, "%s/district.cfg", district);
}

void log_path(const char *district, char *out) {
    snprintf(out, PATH_MAX_, "%s/logged_district", district);
}

void symlink_name(const char *district, char *out) {
    snprintf(out, PATH_MAX_, "active_reports-%s", district);
}


//  INIT

int init_district(const char *district, Role role, const char *user) {
    char dir[PATH_MAX_], rep[PATH_MAX_], cfg[PATH_MAX_],
         log[PATH_MAX_], sym[PATH_MAX_], abs_rep[PATH_MAX_];

    district_dir(district, dir);
    reports_path(district, rep);
    cfg_path(district, cfg);
    log_path(district, log);
    symlink_name(district, sym);

    // 750 pt director 

    int dir_status = mkdir(dir, 0750);
    if (dir_status == -1 && errno != EEXIST) {
        perror("Eroare la creare director");
        return -1;
    }
    chmod(dir, 0750); // ne asiguram ca e 750

    // reports.dat: 664
    int fd1 = open(rep, O_RDWR | O_CREAT, 0664);
    if (fd1 == -1) { perror("open reports"); return -1; }
    close(fd1);
    chmod(rep, 0664);

    // district.cfg: 640
    int fd2 = open(cfg, O_RDWR | O_CREAT, 0640);
    if (fd2 == -1) { perror("open cfg"); return -1; }
    
    struct stat st;
    fstat(fd2, &st);
    if (st.st_size == 0) {
        const char *def = "threshold=2\n";
        write(fd2, def, strlen(def));
    }
    close(fd2);
    chmod(cfg, 0640);

    //logged_district: 644
    int fd3 = open(log, O_RDWR | O_CREAT, 0644);
    if (fd3 == -1) { perror("open logged_district"); return -1; }
    close(fd3);
    chmod(log, 0644);

    /* symlink active_reports-<district> -> reports.dat
       Folosim lstat ca sa nu urmam link-uri existente */
    struct stat lst;
    if (lstat(sym, &lst) == -1) {
        // nu exista, deci se va crea 
        snprintf(abs_rep, PATH_MAX_, "%s", rep);
        if (symlink(abs_rep, sym) == -1) {
            perror("Eroare la symlink");
            return -1;
        }
    } else if (!S_ISLNK(lst.st_mode)) {
        fprintf(stderr, "%s exista dar nu e symlink\n", sym);
    }

    (void)role; (void)user;
    return 0;
}

// functie pt fisiere log 

int add_log(const char *district, Role role, const char *user, const char *action) {
    char logp[PATH_MAX_];
    log_path(district, logp);

    int fd = open(logp, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd == -1)
    { 
        perror("open log"); return -1; 
    }

    char buf[512];
    time_t now = time(NULL);
    struct tm *ti = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", ti);

    char role_str[20];
    if (role == ROLE_MANAGER) {
        strcpy(role_str, "manager");
    } else {
        strcpy(role_str, "inspector");
    }

    snprintf(buf, sizeof(buf), "%ld\t%s\t%s\t%s\n", (long)now, user, role_str, action);
    write(fd, buf, strlen(buf));
    
    close(fd);
    return 0;
}

//citire si afisare - raport (asta va aparea in terminal)

void print_report(const Report *r) {
    char ts[32];
    struct tm *ti = localtime(&r->timestamp);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", ti);

    printf("--------------------------------------------------\n");
    printf("ID          : %d\n",   r->id);
    printf("Inspector   : %s\n",   r->inspector);
    printf("GPS         : %.6f, %.6f\n", r->latitude, r->longitude);
    printf("Categorie   : %s\n",   r->category);
    printf("Severitate  : %d\n",   r->severity);
    printf("Timestamp   : %s\n",   ts);
    printf("Descriere   : %s\n",   r->description);
}

//   CITIRE INTERACTIVA 
   

static void clear_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

static Report read_report_interactive(const char *inspector_name) {
    Report r;
    memset(&r, 0, sizeof(r));
    char buf[256];

    // generez un id random
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    r.id = (int)(time(NULL) & 0x7FFF) * 1000 + rand() % 1000;
    printf("ID generat: %d\n", r.id);

    strncpy(r.inspector, inspector_name, NAME_LEN - 1);

    while (1) {
        printf("Latitudine: ");
        if (fgets(buf, sizeof(buf), stdin)) {
            if (sscanf(buf, "%f", &r.latitude) == 1) break;
        }
        printf("Valoare gresita.\n");
    }

    while (1) {
        printf("Longitudine: ");
        if (fgets(buf, sizeof(buf), stdin)) {
            if (sscanf(buf, "%f", &r.longitude) == 1) break;
        }
        printf("Valoare gresita.\n");
    }

    printf("Categorie (road/lighting/flooding/other): ");
    if (fgets(r.category, CAT_LEN, stdin)) {
        r.category[strcspn(r.category, "\n")] = '\0';
    }

    while (1) {
        printf("Severitate (1, 2 sau 3): ");
        if (fgets(buf, sizeof(buf), stdin)) {
            if (sscanf(buf, "%d", &r.severity) == 1) {
                if (r.severity >= 1 && r.severity <= 3) {
                    break;
                }
            }
        }
        printf("Nu e ok valoarea. Scrie o valoare intre 1 si 3.\n");
    }

    r.timestamp = time(NULL);

    printf("Descriere: ");
    if (fgets(r.description, DESC_LEN, stdin)) {
        r.description[strcspn(r.description, "\n")] = '\0';
    } else {
        clear_stdin();
    }

    return r;
}

//OPERATIUNI

//1. OP_ADD

int op_add(const char *district, Role role, const char *user) {
    if (init_district(district, role, user) != 0) 
        return -1;

    char rep[PATH_MAX_];
    reports_path(district, rep);

    // verifica permisiunea de scriere 
    int perm = check_permission(rep, role, 1);
    if (perm == 0) {
        printf("Eroare permisiune: nu ai voie sa scrii in reports.dat\n");
        return -1;
    }

    Report r = read_report_interactive(user);

    int fd = open(rep, O_WRONLY | O_APPEND);
    if (fd == -1) 
    { 
        printf ("Eroare la deschidere fisier add"); 
        return -1; 
    }

    int bytes_written = write(fd, &r, sizeof(Report));
    if (bytes_written != sizeof(Report)) {
        perror("Eroare la scrierea structurii");
        close(fd);
        return -1;
    }
    close(fd);

    chmod(rep, 0664); 

    printf("Raport salvat cu ID-ul %d.\n", r.id);
    add_log(district, role, user, "add");
    return 0;
}

// OP_LIST

int op_list(const char *district, Role role, const char *user) {
    char rep[PATH_MAX_];
    reports_path(district, rep);

    if (check_permission(rep, role, 0) == 0) {
        printf("Nu ai permisiuni de citire pe reports.dat\n");
        return -1;
    }

    struct stat st;
    if (stat(rep, &st) == -1) {
        printf("Districtul %s nu are rapoarte.\n", district);
        return -1;
    }

    char perm[10];
    mode_to_str(st.st_mode & 0777, perm);

    char ts[32];
    struct tm *ti = localtime(&st.st_mtime);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", ti);

    printf("Fisier: %s\n", rep);
    printf("Permisiuni: %s | Dimensiune: %ld bytes | Ultima modificare: %s\n\n",
           perm, (long)st.st_size, ts);

    int fd = open(rep, O_RDONLY);
    if (fd == -1) 
    { 
        printf("nu pot deschide pt citire");
        return -1; 
    }

    Report r;
    ssize_t bytes;
    int count = 0;

    while ((bytes = read(fd, &r, sizeof(Report))) == sizeof(Report)) {
        print_report(&r);
        count++;
    }
    close(fd);

    if (count == 0) printf("Nu exista rapoarte in districtul '%s'.\n", district);
    else printf("\n");
    printf("Total: %d rapoart.\n", count);

    add_log(district, role, user, "list");
    return 0;
}
