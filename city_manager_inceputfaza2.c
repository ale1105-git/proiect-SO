#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "city_manager.h"
#define PID_FILE ".monitor_pid"

Role parse_role(const char *s) {
    if (strcmp(s, "manager") == 0) return ROLE_MANAGER;
    if (strcmp(s, "inspector") == 0) return ROLE_INSPECTOR;
    fprintf(stderr, "Rol necunoscut: %s\n", s);
    exit(1);
}

Operation parse_operation(const char *s) {
    if (strcmp(s, "--add")              == 0) return OP_ADD;
    if (strcmp(s, "--list")             == 0) return OP_LIST;
    if (strcmp(s, "--view")             == 0) return OP_VIEW;
    if (strcmp(s, "--remove_report")    == 0) return OP_REMOVE_REPORT;
    if (strcmp(s, "--update_threshold") == 0) return OP_UPDATE_THRESHOLD;
    if (strcmp(s, "--filter")           == 0) return OP_FILTER;
    if (strcmp(s, "--remove_district")  == 0) return OP_REMOVE_DISTRICT;
    return OP_UNKNOWN;
}

void mode_to_str(mode_t mode, char out[10]) {
    for (int i = 0; i < 9; i++) out[i] = '-';
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

int check_permission(const char *path, Role role, int need_write) {
    struct stat st;
    if (stat(path, &st) == -1) return 1;
    mode_t m = st.st_mode;
    if (role == ROLE_MANAGER) {
        if (need_write == 1) return ((m & S_IWUSR) != 0) ? 1 : 0;
        else                 return ((m & S_IRUSR) != 0) ? 1 : 0;
    } else {
        if (need_write == 1) return ((m & S_IWGRP) != 0) ? 1 : 0;
        else                 return ((m & S_IRGRP) != 0) ? 1 : 0;
    }
}

void district_dir(const char *d, char *out)  { snprintf(out, PATH_MAX_, "%s", d); }
void reports_path(const char *d, char *out)  { snprintf(out, PATH_MAX_, "%s/reports.dat", d); }
void cfg_path(const char *d, char *out)      { snprintf(out, PATH_MAX_, "%s/district.cfg", d); }
void log_path(const char *d, char *out)      { snprintf(out, PATH_MAX_, "%s/logged_district", d); }
void symlink_name(const char *d, char *out)  { snprintf(out, PATH_MAX_, "active_reports-%s", d); }

int init_district(const char *district, Role role, const char *user) {
    char dir[PATH_MAX_], rep[PATH_MAX_], cfg[PATH_MAX_],
         log[PATH_MAX_], sym[PATH_MAX_], abs_rep[PATH_MAX_];
    district_dir(district, dir);
    reports_path(district, rep);
    cfg_path(district, cfg);
    log_path(district, log);
    symlink_name(district, sym);

    if (mkdir(dir, 0750) == -1 && errno != EEXIST) { perror("mkdir"); return -1; }
    chmod(dir, 0750);

    int fd1 = open(rep, O_RDWR | O_CREAT, 0664);
    if (fd1 == -1) { perror("open reports"); return -1; }
    close(fd1); chmod(rep, 0664);

    int fd2 = open(cfg, O_RDWR | O_CREAT, 0640);
    if (fd2 == -1) { perror("open cfg"); return -1; }
    struct stat st; fstat(fd2, &st);
    if (st.st_size == 0) { const char *def = "threshold=2\n"; write(fd2, def, strlen(def)); }
    close(fd2); chmod(cfg, 0640);

    int fd3 = open(log, O_RDWR | O_CREAT, 0644);
    if (fd3 == -1) { perror("open log"); return -1; }
    close(fd3); chmod(log, 0644);

    snprintf(abs_rep, PATH_MAX_, "%s", rep);
    struct stat lst;
    if (lstat(sym, &lst) == -1) {
        if (symlink(abs_rep, sym) == -1) { perror("symlink"); return -1; }
    } else if (S_ISLNK(lst.st_mode)) {
        unlink(sym);
        if (symlink(abs_rep, sym) == -1) { perror("symlink recreere"); return -1; }
    } else {
        fprintf(stderr, "%s exista dar nu e symlink\n", sym);
    }
    (void)role; (void)user;
    return 0;
}

int add_log(const char *district, Role role, const char *user, const char *action) {
    char logp[PATH_MAX_];
    log_path(district, logp);
    int fd = open(logp, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd == -1) { perror("open log"); return -1; }
    char buf[512];
    const char *rs = (role == ROLE_MANAGER) ? "manager" : "inspector";
    snprintf(buf, sizeof(buf), "%ld\t%s\t%s\t%s\n", (long)time(NULL), user, rs, action);
    write(fd, buf, strlen(buf));
    close(fd);
    return 0;
}

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

static void clear_stdin(void) {
    int c; while ((c = getchar()) != '\n' && c != EOF);
}

static Report read_report_interactive(const char *inspector_name) {
    Report r; memset(&r, 0, sizeof(r)); char buf[256];
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    r.id = (int)(time(NULL) & 0x7FFF) * 1000 + rand() % 1000;
    printf("ID generat: %d\n", r.id);
    strncpy(r.inspector, inspector_name, NAME_LEN - 1);
    while (1) {
        printf("Latitudine: ");
        if (fgets(buf, sizeof(buf), stdin) && sscanf(buf, "%f", &r.latitude) == 1) break;
        printf("Valoare gresita.\n");
    }
    while (1) {
        printf("Longitudine: ");
        if (fgets(buf, sizeof(buf), stdin) && sscanf(buf, "%f", &r.longitude) == 1) break;
        printf("Valoare gresita.\n");
    }
    printf("Categorie (road/lighting/flooding/other): ");
    if (fgets(r.category, CAT_LEN, stdin)) r.category[strcspn(r.category, "\n")] = '\0';
    while (1) {
        printf("Severitate (1, 2 sau 3): ");
        if (fgets(buf, sizeof(buf), stdin) && sscanf(buf, "%d", &r.severity) == 1
            && r.severity >= 1 && r.severity <= 3) break;
        printf("Nu e ok. Scrie 1, 2 sau 3.\n");
    }
    r.timestamp = time(NULL);
    printf("Descriere: ");
    if (fgets(r.description, DESC_LEN, stdin))
        r.description[strcspn(r.description, "\n")] = '\0';
    else clear_stdin();
    return r;
}


int op_add(const char *district, Role role, const char *user) {
    if (init_district(district, role, user) != 0) return -1;
    char rep[PATH_MAX_]; reports_path(district, rep);
    if (check_permission(rep, role, 1) == 0) {
        printf("Eroare permisiune: nu ai voie sa scrii in reports.dat\n");
        return -1;
    }
    Report r = read_report_interactive(user);
    int fd = open(rep, O_WRONLY | O_APPEND);
    if (fd == -1) { printf("Eroare la deschidere fisier add\n"); return -1; }
    ssize_t bw = write(fd, &r, sizeof(Report));
    if (bw != (ssize_t)sizeof(Report)) {
        printf("Eroare la scrierea structurii\n"); close(fd); return -1;
    }
    close(fd); chmod(rep, 0664);
    printf("Raport salvat cu ID-ul %d.\n", r.id);
    notify_monitor(district, role, user);
    return 0;
}

int op_list(const char *district, Role role, const char *user) {
    char rep[PATH_MAX_]; reports_path(district, rep);
    if (check_permission(rep, role, 0) == 0) {
        printf("Nu ai permisiuni de citire pe reports.dat\n"); return -1;
    }
    struct stat st;
    if (stat(rep, &st) == -1) { printf("Districtul %s nu are rapoarte.\n", district); return -1; }
    char perm[10]; mode_to_str(st.st_mode & 0777, perm);
    char ts[32]; struct tm *ti = localtime(&st.st_mtime);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", ti);
    printf("Fisier: %s\n", rep);
    printf("Permisiuni: %s | Dimensiune: %ld bytes | Ultima modificare: %s\n\n",
           perm, (long)st.st_size, ts);
    int fd = open(rep, O_RDONLY);
    if (fd == -1) { printf("Nu pot deschide pt citire\n"); return -1; }
    Report r; int count = 0;
    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) { print_report(&r); count++; }
    close(fd);
    if (count == 0) printf("Nu exista rapoarte in districtul '%s'\n", district);
    else printf("\n");
    printf("Total: %d rapoarte.\n", count);
    add_log(district, role, user, "list");
    return 0;
}

int op_view(const char *district, int report_id, Role role, const char *user) {
    char rep[PATH_MAX_]; reports_path(district, rep);
    if (check_permission(rep, role, 0) == 0) { printf("Acces refuzat.\n"); return -1; }
    int fd = open(rep, O_RDONLY);
    if (fd == -1) return -1;
    Report r; int found = 0;
    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        if (r.id == report_id) { print_report(&r); found = 1; break; }
    }
    close(fd);
    if (!found) { printf("Nu am gasit raportul %d\n", report_id); return -1; }
    add_log(district, role, user, "view");
    return 0;
}

int op_remove_report(const char *district, int report_id, Role role, const char *user) {
    if (role != ROLE_MANAGER) { printf("Eroare: Doar managerul poate sterge rapoarte.\n"); return -1; }
    char rep[PATH_MAX_]; reports_path(district, rep);
    struct stat st;
    if (stat(rep, &st) == -1) { perror("stat"); return -1; }
    int fd = open(rep, O_RDWR);
    if (fd == -1) { perror("open remove"); return -1; }
    long total = (long)(st.st_size / sizeof(Report)), pos = -1;
    Report r;
    for (long i = 0; i < total; i++) {
        if (lseek(fd, i * sizeof(Report), SEEK_SET) == -1) { perror("lseek"); close(fd); return -1; }
        if (read(fd, &r, sizeof(Report)) != sizeof(Report)) { perror("read"); close(fd); return -1; }
        if (r.id == report_id) { pos = i; break; }
    }
    if (pos == -1) { printf("Raportul cu ID=%d nu a fost gasit\n", report_id); close(fd); return -1; }
    for (long i = pos + 1; i < total; i++) {
        if (lseek(fd, i * sizeof(Report), SEEK_SET) == -1) { perror("lseek"); close(fd); return -1; }
        if (read(fd, &r, sizeof(Report)) != sizeof(Report)) { perror("read"); close(fd); return -1; }
        if (lseek(fd, (i-1) * sizeof(Report), SEEK_SET) == -1) { perror("lseek"); close(fd); return -1; }
        if (write(fd, &r, sizeof(Report)) != sizeof(Report)) { perror("write"); close(fd); return -1; }
    }
    if (ftruncate(fd, (total-1) * sizeof(Report)) == -1) {
        perror("ftruncate"); close(fd); return -1;
    }
    close(fd);
    printf("Raport %d sters.\n", report_id);
    add_log(district, role, user, "remove_report");
    return 0;
}

int op_update_threshold(const char *district, int value, Role role, const char *user) {
    if (role != ROLE_MANAGER) { printf("Doar managerii pot schimba configuratia.\n"); return -1; }
    char cfgp[PATH_MAX_]; cfg_path(district, cfgp);
    struct stat st;
    if (stat(cfgp, &st) == -1) return -1;
    if ((st.st_mode & 0777) != 0640) {
        printf("Fisierul district.cfg nu are permisiunile 640. Eroare.\n"); return -1;
    }
    if (check_permission(cfgp, ROLE_MANAGER, 1) == 0) return -1;
    int fd = open(cfgp, O_WRONLY | O_TRUNC);
    if (fd == -1) return -1;
    char buf[64]; snprintf(buf, sizeof(buf), "threshold=%d\n", value);
    write(fd, buf, strlen(buf)); close(fd);
    printf("Am actualizat pragul la %d pt districtul %s.\n", value, district);
    add_log(district, role, user, "update_threshold");
    return 0;
}

/* FUNCTII CU ASISTENTA AI */

int parse_condition(const char *input, char *field, char *op, char *value) {
    const char *p1 = strchr(input, ':');
    if (!p1) return -1;
    const char *p2 = strchr(p1 + 1, ':');
    if (!p2) return -1;
    size_t flen = (size_t)(p1 - input);
    if (flen == 0 || flen >= 32) return -1;
    strncpy(field, input, flen); field[flen] = '\0';
    size_t olen = (size_t)(p2 - p1 - 1);
    if (olen == 0 || olen >= 4) return -1;
    strncpy(op, p1 + 1, olen); op[olen] = '\0';
    const char *vstart = p2 + 1;
    if (*vstart == '\0') return -1;
    strncpy(value, vstart, 63); value[63] = '\0';
    return 0;
}

int match_condition(Report *r, const char *field, const char *op, const char *value) {
    if (strcmp(field, "severity") == 0) {
        int v = atoi(value), s = r->severity;
        if (strcmp(op, "==") == 0) return s == v;
        if (strcmp(op, "!=") == 0) return s != v;
        if (strcmp(op, "<")  == 0) return s <  v;
        if (strcmp(op, "<=") == 0) return s <= v;
        if (strcmp(op, ">")  == 0) return s >  v;
        if (strcmp(op, ">=") == 0) return s >= v;
    } else if (strcmp(field, "category") == 0) {
        int c = strcmp(r->category, value);
        if (strcmp(op, "==") == 0) return c == 0;
        if (strcmp(op, "!=") == 0) return c != 0;
    } else if (strcmp(field, "inspector") == 0) {
        int c = strcmp(r->inspector, value);
        if (strcmp(op, "==") == 0) return c == 0;
        if (strcmp(op, "!=") == 0) return c != 0;
    } else if (strcmp(field, "timestamp") == 0) {
        long v = strtol(value, NULL, 10), t = (long)r->timestamp;
        if (strcmp(op, "==") == 0) return t == v;
        if (strcmp(op, "!=") == 0) return t != v;
        if (strcmp(op, "<")  == 0) return t <  v;
        if (strcmp(op, "<=") == 0) return t <= v;
        if (strcmp(op, ">")  == 0) return t >  v;
        if (strcmp(op, ">=") == 0) return t >= v;
    } else {
        fprintf(stderr, "Camp necunoscut: %s\n", field);
    }
    return 0;
}

int op_filter(const char *district, Role role, const char *user,
              char **conditions, int ncond) {
    char rep[PATH_MAX_]; reports_path(district, rep);
    if (check_permission(rep, role, 0) == 0) {
        fprintf(stderr, "EROARE permisiune: nu poti citi reports.dat\n"); return -1;
    }
    char fields[16][32], ops[16][4], values[16][64];
    for (int i = 0; i < ncond; i++) {
        if (parse_condition(conditions[i], fields[i], ops[i], values[i]) != 0) {
            fprintf(stderr, "Conditie invalida: %s\n", conditions[i]); return -1;
        }
    }
    int fd = open(rep, O_RDONLY);
    if (fd == -1) { perror("open filter"); return -1; }
    Report r; int count = 0;
    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        int ok = 1;
        for (int i = 0; i < ncond && ok; i++)
            if (match_condition(&r, fields[i], ops[i], values[i]) == 0) ok = 0;
        if (ok) { print_report(&r); count++; }
    }
    close(fd);
    if (count == 0) printf("Niciun raport nu satisface conditiile.\n");
    else printf("\n");
    printf("Rapoarte gasite: %d\n", count);
    add_log(district, role, user, "filter");
    return 0;
}

//////iehfaihfahif

//functie noua adaugata pentru faza 2
//remove_district <district_id>  care sterge intregul district si tot ceea ce contine acesta plus link urile

int op_remove_district(const char *district, Role role, const char *user) {
    
    // 1. Verificam drepturile de acces, deoarece doar managerul poate folosi comanda, in caz contrar afisam o eroare
    if (role != ROLE_MANAGER)
    {
        printf("Eroare: Doar managerul poate sterge un district.\n"); 
        return -1;
    }

    // 2. Ne asiguram ca districtul chiar exista pe disc inainte sa incercam sa-l stergem, evitam comportamentul nedefinit, in caz contrar adisam un mesaj de eroare  
    struct stat st;
    if (stat(district, &st) == -1)
    {
        printf("Districtul '%s' nu exista.\n", district); 
        return -1;
    }

    // 3. Verificam ca numele primit corespunde unui director, nu unui fisier obisnuit (verificare ca este director)
    //in caz contrar, afisam yb mesaj de eroare si nu continuam operatia 
    if (!S_ISDIR(st.st_mode))
    {
        printf("'%s' nu este un director valid.\n", district); 
        return -1;
    }

    // 4. securitate - avem grija la numele districtului care urmeaza a fi sters si continu,a operatia doaar daca acesta este  ok
    // Ne asiguram ca numele nu contine "/" sau referinte la foldere parinte ("..").
    // Asta previne comenzi dezastruoase de tipul "rm -rf ../../" sau "rm -rf /".
    // aceasta verificare a fost explicit mentionata in conteztul cerintei, fara ea am putea sterge ceea ce nu ne dorim din calculator 
    if (strchr(district, '/') || strcmp(district,"..") == 0 || strcmp(district,".") == 0)
    {
        printf("Eroare: nume district invalid '%s'.\n", district); 
        return -1;
    }

    printf("Sterg districtul '%s'...\n", district);
    
    // 5. Cream un proces COPIL. Aici programul "se cloneaza".
    // Variabila 'pid' va avea valoarea 0 in interiorul copilului, 
    // si va contine ID-ul copilului in interiorul parintelui.
    
    pid_t pid = fork();
    //FORK() creaza o copie identica a procesului parinte iar vfork nu copiaza tot din procesul parinte si se folosteste in asociate cu exec (), deci noi folosim fork().
    
    if (pid == -1) //avem eroare: pid < 0 eroare, pid == 0 copil, pid  > 0 parinte
    { 
        perror("fork"); 
        return -1; 
    }

    // 6. COD EXECUTAT DOAR DE COPIL
    if (pid == 0)
    {
        // execl inlocuieste complet codul copilului cu programul /bin/rm.
        // Ultimul argument trebuie mereu sa fie NULL pentru a marca finalul listei de argumente.
        execl("/bin/rm", "rm", "-rf", district, NULL);
        
        // Daca execl reuseste, copilul se transforma in comanda 'rm', deci liniile 
        // de mai jos NU se mai executa. Daca ajungem la ele, inseamna ca execl a dat eroare.
        perror("execl"); 
        exit(1); 
    }

    // 7. COD EXECUTAT DOAR DE PARINTE
    int status;
    
    // Parintele asteapta sa se termine procesul copil inainte sa mearga mai departe.
    if (waitpid(pid, &status, 0) == -1)
    { 
        perror("waitpid"); 
        return -1; 
    }

    // 8. Verificam daca programul 'rm' s-a terminat normal (WIFEXITED) 
    // si daca codul sau de iesire a fost 0 (WEXITSTATUS == 0, adica succes).
    //altfel dam eroare 
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        printf("Eroare la stergerea directorului '%s'.\n", district); 
        return -1;
    }
    
    printf("Director '%s' sters cu succes.\n", district); //mesaj care sa ne spuna ca directorul nostru a fost sters cu succes, practic functia a functionat

    // 9. Curatam symlink-ul activ
    char sym[PATH_MAX_]; 
    symlink_name(district, sym);
    struct stat lst;
    
    // Folosim lstat pentru a citi symlink-ul fara a incerca sa il urmarim 
    // (pentru ca folderul destinatie tocmai a fost sters mai sus, deci e un "broken link").
    if (lstat(sym, &lst) == 0) 
    {
        // Daca exista, il stergem folosind unlink().
        if (unlink(sym) == 0) 
            printf("Symlink '%s' sters.\n", sym);
        else 
            perror("unlink symlink");
    }

    // Aceasta linie ii "spune" compilatorului sa nu dea warning de tipul 
    // "unused variable" in cazul in care parametrul 'user' nu este folosit deloc in functie.
    //(void)user;
    
    return 0;
}

static void check_symlink(const char *district) {
    char sym[PATH_MAX_]; symlink_name(district, sym);
    struct stat lst;
    if (lstat(sym, &lst) == -1) return;
    if (!S_ISLNK(lst.st_mode)) {
        fprintf(stderr, "%s exista dar nu este un symlink!\n", sym); return;
    }
    struct stat st;
    if (stat(sym, &st) == -1)
        fprintf(stderr, "AVERTISMENT: symlink '%s' e dangling!\n", sym);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Utilizare:\n"
        "  %s --role <manager|inspector> --user <nume> --add <district>\n"
        "  %s --role <manager|inspector> --user <nume> --list <district>\n"
        "  %s --role <manager|inspector> --user <nume> --view <district> <id>\n"
        "  %s --role manager --user <nume> --remove_report <district> <id>\n"
        "  %s --role manager --user <nume> --update_threshold <district> <val>\n"
        "  %s --role <manager|inspector> --user <nume> --filter <district> <cond...>\n"
        "  %s --role manager --user <nume> --remove_district <district>\n",
        prog,prog,prog,prog,prog,prog,prog);
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        printf("Nr incorect de argumente.\n");
        printf("Exemplu: %s --role manager --user nume --list district\n", argv[0]);
        return 1;
    }
    const char *role_str = NULL, *user = NULL;
    Operation op = OP_UNKNOWN; int op_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--role") == 0 && i+1 < argc) { role_str = argv[++i]; }
        else if (strcmp(argv[i], "--user") == 0 && i+1 < argc) { user = argv[++i]; }
        else { op = parse_operation(argv[i]); op_idx = i; break; }
    }
    if (!role_str || !user || op == OP_UNKNOWN || op_idx == -1) { usage(argv[0]); return 1; }
    Role role = parse_role(role_str);
    int argumente_ramase = argc - op_idx - 1;
    char **rest_args = argv + op_idx + 1;
    if (argumente_ramase < 1) { fprintf(stderr, "Lipsa district_id\n"); return 1; }
    const char *district = rest_args[0];
    check_symlink(district);
    switch (op) {
        case OP_ADD:    return op_add(district, role, user);
        case OP_LIST:   return op_list(district, role, user);
        case OP_VIEW: {
            if (argumente_ramase < 2) { fprintf(stderr, "Lipsa report_id\n"); return 1; }
            return op_view(district, atoi(rest_args[1]), role, user);
        }
        case OP_REMOVE_REPORT: {
            if (argumente_ramase < 2) { fprintf(stderr, "Lipsa report_id\n"); return 1; }
            return op_remove_report(district, atoi(rest_args[1]), role, user);
        }
        case OP_UPDATE_THRESHOLD: {
            if (argumente_ramase < 2) { fprintf(stderr, "Lipsa valoare\n"); return 1; }
            return op_update_threshold(district, atoi(rest_args[1]), role, user);
        }
        case OP_FILTER: {
            if (argumente_ramase < 2) { fprintf(stderr, "Lipsa conditie\n"); return 1; }
            return op_filter(district, role, user, rest_args+1, argumente_ramase-1);
        }
        case OP_REMOVE_DISTRICT:
            return op_remove_district(district, role, user);
        default:
            fprintf(stderr, "Operatie necunoscuta.\n"); return 1;
    }
}
