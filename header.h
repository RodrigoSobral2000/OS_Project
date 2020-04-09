//RODRIGO SOBRAL  2018298209
//RAUL NOGUEIRA   2017267634

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

#define SIZE 15
#define DIM_LINHA 100
#define SIZEFLIGHT 6
#define DEPARTURE "DEPARTURE"
#define ARRIVAL "ARRIVAL"
#define FIFO_NAME "input_pipe"
#define GESTOR_TORRE 1
#define TORRE_GESTOR 2

//CONFIGURATION
typedef struct {
    int ut;     //milissegundos
    int duracao_departures, intervalo_departures;   // duracao de uma descolagem e intervalo entre descolagens, em uts
    int duracao_arrivals, intervalo_arrivals;       // duracao de uma aterragem e intervalo entre aterragens, em uts
    int holding_min, holding_max;       // holding minimo de maximo de um voo, em uts
    int max_departures, max_arrivals;   // quantidade maxima de departures e arrivals, em uts
}config;

//FLIGHTS
typedef struct voos *ListaVoos;
typedef struct voos{
  char status[SIZE], name[SIZE];
  int init, takeoff_eta, fuel;
  ListaVoos next;
} Flights;

typedef struct th {
  pthread_t *allArr;
  pthread_t *allDep;
} Threads;

struct timespec sl;
pthread_mutex_t mutex1=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t creator = PTHREAD_COND_INITIALIZER;
pthread_mutex_t muCondition = PTHREAD_MUTEX_INITIALIZER;

typedef struct{
  char name[SIZE];
  int init, eta, holds, fuel, fuel1;
}Chegadas;

typedef struct{
  char name[SIZE];
  long int init;
  int takeOff;
  int fuel;
}Partidas;

typedef struct{
  char name[SIZE];
  int init, fuel, hour;
}Todos;

typedef struct {
  long int mtype;
  char name[SIZE];
  int init, takeoff_eta, fuel;
}Msg;

typedef struct {
  int tot_aterragens_criadas, tot_descolagens_criadas, tot_voos_aterrados, tot_voos_descolados, tot_voos_emergencia;
  int tempo_espera_aterrar, tempo_espera_descolar;
  int holdings_aterragem, holdings_emergencia; 
  int voos_rejeitados, voos_redirecionados;
} esta;


//FUNCTIONS
void controlTower(config configuration, FILE *log);

config read_config();
char* get_hours(char *texto_tempo);
void init(FILE *log, config configuration);

int checkCommands(char *buf_recv, long tempo_inicial);
int verifica_numero(char *string);

ListaVoos addToLinkedListFlights(char *string);
ListaVoos ordena_voos_init();
void troca_voos(ListaVoos trocado1, ListaVoos trocado2);

void addToArrayArr(char *string);
ListaVoos removeVoo(char *nome_voo);

void* cria_voo(void* arg);
void* createThreads();

void orderAll(config configuration);
void orderDep();
void orderArrEta(FILE *log, config configuration);
void orderArr();
void acao(FILE *log, config configuration);

void removeDep();
void removeFlight(int i);

void addTakeOff(int diferenca, int i);
int countTime(int i, config configuration);

void over();
void estat();