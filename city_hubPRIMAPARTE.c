
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define PID_FILE ".monitor_pid"


static void cmd_start_monitor(void) 
{
    printf("Pornesc monitorul...\n");
    fflush(stdout);
    //Cream procesul hub_mon si acesta va gestiona comunicarea cu monitor_reports prin pipe
    pid_t hub_mon_pid = fork();
    if (hub_mon_pid == -1) 
    {
        perror("Eroare fork hub_mon"); //dam eroare ca nu a putut sa creeze procesul
        return;
    }

    if (hub_mon_pid == 0) 
    {
        //proces copil 
         //pfd[0] = capatul de citire  (hub_mon va citi de aici)
         //pfd[1] = capatul de scriere (monitor_reports va scrie aici)
        int pfd[2];
        if (pipe(pfd) == -1) 
        {
            perror("[HUB_MON] Eroare la creare pipe");
            exit(1);
        }

        //Fork la procesul monitor_reports 
        pid_t mon_pid = fork();
        if (mon_pid == -1) 
        {
            perror("Eroare fork monitor");
            close(pfd[0]);
            close(pfd[1]);
            exit(1);
        }

        if (mon_pid == 0) 
        {
            //proces nepot: monitor_reports 

            /*
             Inchidem capatul de citire - monitorul nu citeste din pipe, el doar scrie (trimite mesaje)
            */
            close(pfd[0]);

            /*
             dup2(pfd[1], STDOUT_FILENO):
             Redirectam stdout (fd=1) spre capatul de scriere al pipe-ului
             Dupa aceasta operatie, orice printf() din monitor_reports va ajunge in pipe, nu pe ecran
            */
            if (dup2(pfd[1], STDOUT_FILENO) == -1) 
            {
                perror("Eroare dup2");
                exit(1);
            }

            close(pfd[1]);

            // prin execl, inlocuim procesul cu monitor_reports 
            execl("./monitor_reports", "monitor_reports", NULL);

            // Daca ajungem aici, execl a esuat si dam un mesaj de eroare
            perror("Eroare execl monitor_reports");
            exit(1);
        }

        //revenim in  hub_mon (parintele)

    
        //Inchidem capatul de scriere al pipe-ului in hub_mon, ADICA PFD[1]
        //Astfel, cand monitorul se inchide si inchide pfd[1], read() va returna 0 (EOF) si stim ca monitorul s-a oprit
        
        close(pfd[1]);

        printf("Monitor pornit. Citesc mesajele...\n");
        fflush(stdout);

        
        //Citim din pipe in bucla pana la EOF
        //Citim caracter cu caracter pentru a aduna linii complete
        // stim ca mesajele sunt de lungime variabila
         
        char buf[MAX_LINE];
        int  pos = 0;
        char c;
        ssize_t n;

        while ((n = read(pfd[0], &c, 1)) > 0) {
            if (c == '\n' || pos >= MAX_LINE - 1) {
                buf[pos] = '\0';
                pos = 0;

                
                //Parsam prefixul mesajului pentru a-l afisa corespunzator, acestea sunt:
                //MON_MSG: – mesaj informativ
                //MON_ERR: – eroare
                //MON_END: – monitorul se opreste
                 
                if (strncmp(buf, "MON_MSG:", 8) == 0) 
                {
                    printf("[MONITOR] %s\n", buf + 8);
                } 
                else if (strncmp(buf, "MON_ERR:", 8) == 0) 
                {
                    printf("[MONITOR EROARE] %s\n", buf + 8);
                    printf("[HUB_MON] Monitorul nu a putut porni.\n");
                } 
                else if (strncmp(buf, "MON_END:", 8) == 0) 
                {
                    printf("[MONITOR OPRIT] %s\n", buf + 8);
                } 
                else 
                {
                    // avem mesaj fara prefix cunoscut
                    printf("[MONITOR] %s\n", buf);
                }
                fflush(stdout);

            } 
            else 
            {
                buf[pos++] = c; //mergem mai departe
            }
        }

        //EOF pe pipe – monitorul s-a inchis 
        close(pfd[0]);

        //asteptam procesul monitor sa se termine complet 

        int status; //ne zice starea monitorului

        waitpid(mon_pid, &status, 0);

        printf("Monitorul s-a oprit. hub_mon se inchide.\n");
        fflush(stdout);
        exit(0);
    }

    //in  city_hub 
    
    //hub_mon ruleaza in background 
    //continuam sa citim comenzi de la utilizator
     
    printf("hub_mon pornit (PID=%d). Monitorul ruleaza in background.\n", (int)hub_mon_pid);
    printf("poti continua sa dai comenzi.\n");
    fflush(stdout);
}