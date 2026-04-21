#ifndef CITY_MANAGER_H
#define CITY_MANAGER_H

#include <sys/types.h>
#include <time.h>

#define NAME_LEN   64
#define CAT_LEN    32
#define DESC_LEN   128
#define PATH_MAX_  512


typedef struct {
    int    id;
    char   inspector[NAME_LEN];
    float  latitude;
    float  longitude;
    char   category[CAT_LEN];   // "road", "lighting", "flooding", etc. 
    int    severity;             //1=minor, 2=moderat, 3=critic 
    time_t timestamp;
    char   description[DESC_LEN];
} Report;

//roluri 
typedef enum { ROLE_INSPECTOR, ROLE_MANAGER } Role;

// operatii
typedef enum {
    OP_ADD,
    OP_LIST,
    OP_VIEW,
    OP_REMOVE_REPORT,
    OP_UPDATE_THRESHOLD,
    OP_FILTER,
    OP_UNKNOWN
} Operation;


// parsare
Role      parse_role(const char *s);
Operation parse_operation(const char *s);

// permisiuni
void mode_to_str(mode_t mode, char out[10]);
int  check_permission(const char *path, Role role, int need_write);

//cai
void district_dir(const char *district, char *out);
void reports_path(const char *district, char *out);
void cfg_path(const char *district, char *out);
void log_path(const char *district, char *out);
void symlink_name(const char *district, char *out);

// initializare
int  init_district(const char *district, Role role, const char *user);

// functie fisiere log
int  add_log(const char *district, Role role, const char *user, const char *action);

// operatii principale 
int  op_add(const char *district, Role role, const char *user);
int  op_list(const char *district, Role role, const char *user);
int  op_view(const char *district, int report_id, Role role, const char *user);
int  op_remove_report(const char *district, int report_id, Role role, const char *user);
int  op_update_threshold(const char *district, int value, Role role, const char *user);
int  op_filter(const char *district, Role role, const char *user,
               char **conditions, int ncond);

// functii cu ai
int  parse_condition(const char *input, char *field, char *op, char *value);
int  match_condition(Report *r, const char *field, const char *op, const char *value);

// afis raport
void print_report(const Report *r);

#endif 
