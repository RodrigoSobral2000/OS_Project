//RODRIGO SOBRAL  2018298209
//RAUL NOGUEIRA   2017267634

#include "header.h"

//======================================GLOBAL VARS======================================

int read_fifo;
int shmid_esta, shmid_todosArray, shmidMQ;
int arrIndex=0,depIndex=0, todosIndex=0;;
char horas[SIZE];
long int tempo_inicial;

sem_t acesso_log;
pid_t ct;
esta* estatisticas;

ListaVoos flights=NULL;

Threads all;
Chegadas *chegadasArray;
Partidas *partidasArray;
Todos *todosArray;

void imprime() {
    ListaVoos aux=flights;
    while (aux) {
        printf("%s: %s\tinit=%d\ttake_eta=%d\tfuel=%d\n", aux->status, aux->name, aux->init, aux->takeoff_eta, aux->fuel);
        aux=aux->next;
    }
}

//======================================MAIN FUNCTIONS======================================

int main() {
  FILE *log = fopen("log.txt","w");
  config configuration=read_config();
  pthread_t criadora_threads;


  // INICIALIZAÇÃO DA MEMORIA
  init(log, configuration);

  int read_fifo, nread_fifo;
  char buf_recv[DIM_LINHA];

  // TORRE DE CONTROLO
  ct = fork();
  if (ct ==0 ) {
    printf("%s CONTROL TOWER CREATED WITH PID %d\n", get_hours(horas), getpid());
    sem_wait(&acesso_log);
    fprintf(log, "%s CONTROL TOWER CREATED WITH PID %d\n", get_hours(horas), getpid());
    sem_post(&acesso_log);
    controlTower(configuration, log);
  }

  // GESTOR DE SIMULACAO
  else {
    // FAZER UMA PIPE DE LEITURA
    read_fifo= open(FIFO_NAME, O_RDWR);
    printf("%s SIMULATION MANAGER CREATED WITH PID %d\n", get_hours(horas), getpid());
    sem_wait(&acesso_log);
    fprintf(log, "%s SIMULATION MANAGER CREATED WITH PID %d\n", get_hours(horas), getpid());
    sem_post(&acesso_log);

    // REDIRECIONA O SINAL CTRL-C
    signal(SIGINT, over);
    signal(SIGUSR1, SIG_IGN);
    // --------------------------

    tempo_inicial = time(NULL);
    pthread_create(&criadora_threads, NULL, &createThreads, NULL);
    while (1) {

      //LEITURA DOS COMANDOS
      nread_fifo = read(read_fifo, buf_recv, DIM_LINHA);
      buf_recv[nread_fifo-1]='\0';
      if (checkCommands(buf_recv, tempo_inicial)==1) {
        printf("%s NEW COMMAND => %s\n", get_hours(horas), buf_recv);
        sem_wait(&acesso_log);
        fprintf(log, "%s NEW COMMAND => %s\n", get_hours(horas), buf_recv);
        sem_post(&acesso_log);

        flights=addToLinkedListFlights(buf_recv);
        flights=ordena_voos_init();
        pthread_cond_signal(&creator);
      } else {
        printf("%s WRONG COMMAND => %s\n", get_hours(horas), buf_recv);
        sem_wait(&acesso_log);
        fprintf(log, "%s WRONG COMMAND => %s\n", get_hours(horas), buf_recv);
        sem_post(&acesso_log);
      }
    }
  }
  return 0;
}

void controlTower(config configuration, FILE *log){
  signal(SIGINT, SIG_IGN);
  signal(SIGUSR1, estat);
  Msg m1;

  while(1) {
    msgrcv(shmidMQ, &m1, sizeof(Msg)-sizeof(long), GESTOR_TORRE, 0);
    if (m1.fuel==-1) {
      strcpy((partidasArray+depIndex)->name, m1.name);
      (partidasArray+depIndex)->init=m1.init;
      (partidasArray+depIndex)->takeOff= m1.takeoff_eta;
      depIndex++;
    } else{
      strcpy((chegadasArray+arrIndex)->name, m1.name);
      (chegadasArray+arrIndex)->init=m1.init;
      (chegadasArray+arrIndex)->eta= m1.takeoff_eta;
      (chegadasArray+arrIndex)->fuel= m1.fuel;
      (chegadasArray+arrIndex)->holds= 0;
      arrIndex++;
    }
    orderArr();
    orderArrEta(log, configuration);
    orderDep();
    orderAll(configuration);
    (*estatisticas).tot_aterragens_criadas = arrIndex;
    (*estatisticas).tot_descolagens_criadas = depIndex;
    acao(log, configuration);
  }
  exit(0);
}

//======================================SECUNDARY FUNCTIONS======================================

// INICIALIZAÇAO
config read_config() {
    FILE *fp_config= fopen("config.txt", "r");
    config config_aux;
    int cont_linha=0;
    char linha[DIM_LINHA];

    while (fgets(linha, DIM_LINHA, fp_config)) {
        if (cont_linha==0) config_aux.ut=atoi(strtok(linha, "\n"));
        else if (cont_linha==1) {
            config_aux.duracao_departures= atoi(strtok(linha, ", "));
            config_aux.intervalo_departures= atoi(strtok(NULL, "\n"));
        } else if (cont_linha==2) {
            config_aux.duracao_arrivals= atoi(strtok(linha, ", "));
            config_aux.intervalo_arrivals= atoi(strtok(NULL, "\n"));
        } else if (cont_linha==3) {
            config_aux.holding_min= atoi(strtok(linha, ", "));
            config_aux.holding_max= atoi(strtok(NULL, "\n"));
        } else if (cont_linha==4) config_aux.max_departures= atoi(strtok(linha, "\n"));
        else if (cont_linha==5) config_aux.max_arrivals= atoi(strtok(linha, "\n"));
        cont_linha++;
    }

    fclose(fp_config);
    return config_aux;
}
void init(FILE *log, config configuration) {
  fprintf(log, "%s BEGINNING SIMULATION\n", get_hours(horas));
  printf("%s BEGINNING SIMULATION\n", get_hours(horas));

  // CRIAR FIFO
  mkfifo(FIFO_NAME, O_CREAT | O_EXCL | 0666);

  // INICIAR SEMAFOROS
  sem_init(&acesso_log, 1, 1);
  pthread_cond_init(&creator,NULL);
  pthread_mutex_init(&muCondition,NULL);
  pthread_mutex_lock(&muCondition);

  // MEMORIA PARTILHADA DA INFORMAÇAO DOS DEPARTURES E ARRIVALS
  shmid_esta = shmget(IPC_PRIVATE, sizeof(esta), IPC_CREAT|0700);
	if (shmid_esta < 1) exit(0);
	estatisticas = (esta*) shmat(shmid_esta, NULL, 0);
	if (estatisticas < (esta*) 1) exit(0);
  memset(estatisticas, 0, sizeof(esta));

  shmid_todosArray= shmget(IPC_PRIVATE, sizeof(Todos), IPC_CREAT|0700);
  if (shmid_todosArray<1) exit(0);
  todosArray=(Todos*) shmat(shmid_todosArray, NULL, 0);
  if (todosArray<(Todos*)1) exit(0);

  all.allDep = (pthread_t*) malloc((configuration.max_departures)*sizeof(pthread_t));
  all.allArr = (pthread_t*) malloc((configuration.max_arrivals)*sizeof(pthread_t));

  // MESSAGE QUEUE
  shmidMQ = msgget(IPC_PRIVATE, IPC_CREAT | 0777);
  if (shmidMQ < 0) perror("Erro ao criar a fila de mensagens\n");
}

char* get_hours(char *texto_tempo){
  time_t horas;
  struct tm *mytime;
  time(&horas);
  mytime= localtime(&horas);
  strftime(texto_tempo, 100, "%X", mytime);
  return texto_tempo;
}

// VERIFICAÇAO
int verifica_numero(char *string) {
  for (int i=0; i< (int)strlen(string); i++) {
    if ((string[i]>='0' && string[i]<='9') || string[i]==' ') continue;
    else return 0;
  }
  return 1;
}
int checkCommands(char *string, long tempo_inicial) {
  char mini_buf[SIZE];
  char buf_recv[DIM_LINHA];

  strcpy(buf_recv, string);

  // TIPO DO VOO
  strcpy(mini_buf, strtok(buf_recv, " "));
  if (strcmp(DEPARTURE, mini_buf)==0) {

    // NOME DO VOO
    strcpy(mini_buf, strtok(NULL, " "));
    if (mini_buf[0]=='T' && mini_buf[1]=='P') {

      // init:
      strcpy(mini_buf, strtok(NULL, " "));
      if (strcmp("init:", mini_buf)==0) {

        // VALOR INIT
        strcpy(mini_buf, strtok(NULL, " "));
        if (verifica_numero(mini_buf)==1) {

          // verifica se o init é maior que o tempo real
          if (atoi(mini_buf)>=(time(NULL)-tempo_inicial)) {

            // takeoff:
            strcpy(mini_buf, strtok(NULL, " "));
            if (strcmp(mini_buf, "takeoff:")==0) {

              // VALOR TAKEOFF
              strcpy(mini_buf, strtok(NULL, "\0"));
              if (verifica_numero(mini_buf)==1) return 1;
              else return 0;

            } else return 0;
          } else return 0;
        } else return 0;
      } else return 0;
    } else return 0;

  } else if (strcmp(ARRIVAL, mini_buf)==0) {

    // NOME DO VOO
    strcpy(mini_buf, strtok(NULL, " "));
    if (mini_buf[0]=='T' && mini_buf[1]=='P') {

      // init:
      strcpy(mini_buf, strtok(NULL, " "));
      if (strcmp("init:", mini_buf)==0) {

        // VALOR INIT
        strcpy(mini_buf, strtok(NULL, " "));
        if (verifica_numero(mini_buf)==1) {

          // verifica se o init é maior que o tempo real
          if (atoi(mini_buf)>=(time(NULL)-tempo_inicial)) {

            // eta:
            strcpy(mini_buf, strtok(NULL, " "));
            if (strcmp(mini_buf, "eta:")==0) {

              // VALOR ETA
              strcpy(mini_buf, strtok(NULL, " "));
              if (verifica_numero(mini_buf)==1) {

                // fuel:
                strcpy(mini_buf, strtok(NULL, " "));
                if (strcmp(mini_buf, "fuel:")==0) {

                  // VALOR FUEL
                  strcpy(mini_buf, strtok(NULL, "\0"));
                  if (verifica_numero(mini_buf)==1) return 1;
                  else return 0;

                } else return 0;
              } else return 0;
            } else return 0;
          } else return 0;
        } else return 0;
      } else return 0;
    } else return 0;
  } else return 0;
}

// LISTA LIGADA VOOS
ListaVoos addToLinkedListFlights(char *string) {
  int i;
  char *token, buf_recv[DIM_LINHA];
  ListaVoos aux=NULL, new;
  new = (ListaVoos) malloc(sizeof(Flights));

  strcpy(buf_recv, string);
  token = strtok(buf_recv, " ");
  //ARRIVAL
  if (strcmp(ARRIVAL,token)==0) {
    strcpy(new->status,token);
    for (i = 0;i < 7;i++) {
      if (i!=6) token = strtok(NULL, " ");
      if (i==0) {
        strcpy(new->name,token);
      } else if(i==2){
        new->init = atoi(token);
      } else if(i==4){
        new->takeoff_eta = atoi(token);
      } else if(i==6){
        token=strtok(NULL, "\n");
        new->fuel = atoi(token);
      }
    }
  }
  //DEPARTURE
  else {
    strcpy(new->status,token);
    for (i = 0;i < 5;i++) {
      if (i!=4) token = strtok(NULL, " ");
      if (i==0) {
        strcpy(new->name,token);
      } else if(i==2){
        new->init = atoi(token);
      } else if(i==4){
        token=strtok(NULL, "\n");
        new->takeoff_eta = atoi(token);
        new->fuel = -1;
      }
    }
  }
  new->next=NULL;
  if(flights == NULL) flights = new;
  else{
    aux = (ListaVoos) malloc(sizeof(Flights));
    aux = flights;
    while(aux->next) aux = aux->next;
    aux->next=new;
  }
  return flights;
}
ListaVoos removeVoo(char *nome) {
  ListaVoos temp = flights, prev;

  if(temp!= NULL && strcmp(temp->name, nome)==0) {
    flights = temp->next;
    free(temp);
    return flights;
  }

  while(temp!=NULL && strcmp(temp->name, nome)!=0){
    prev = temp;
    temp = temp->next;
  }
  if(temp == NULL) return flights;

  prev->next = temp->next;
  free(temp);
  return flights;
}

// ORDENAMENTO POR INIT DA LISTA LIGADA
ListaVoos ordena_voos_init() {
  int troca;
  ListaVoos atual, anterior = NULL;
  if (flights != NULL && flights->next!=NULL) {
    do {
      troca = 0;
      atual = flights;
      while (atual->next != anterior) {
        if (atual->init > atual->next->init) {
          troca_voos(atual, atual->next);
          troca = 1;
        }
        atual= atual->next;
      }
      anterior=atual;
    } while (troca);
  }
  return flights;
}
void troca_voos(ListaVoos trocado1, ListaVoos trocado2) {
  int fuel_aux=trocado1->fuel, init_aux=trocado1->init, takeoff_eta=trocado1->takeoff_eta;
  char nome_aux[SIZE], status_aux[SIZE];
  strcpy(nome_aux, trocado1->name);
  strcpy(status_aux, trocado1->status);

  trocado1->fuel = trocado2->fuel;
  trocado1->init = trocado2->init;
  trocado1->takeoff_eta = trocado2->takeoff_eta;
  strcpy(trocado1->name, trocado2->name);
  strcpy(trocado1->status, trocado2->status);

  trocado2->fuel=fuel_aux;
  trocado2->init=init_aux;
  trocado2->takeoff_eta=takeoff_eta;
  strcpy(trocado2->name, nome_aux);
  strcpy(trocado2->status, status_aux);
}

// CRIAR THREADS PARA A MSQ
void* createThreads() {
  int now = 0;
  ListaVoos aux;
  time_t start=time(NULL);
  int faltaQuanto;

  if (flights==NULL) {
      sl.tv_sec=start+10000;
      pthread_cond_timedwait(&creator,&muCondition,&sl);
      aux=flights;
  }
  while (1) {
    while (aux->init>now) {
      pthread_cond_timedwait(&creator,&muCondition,&sl);
      now = time(NULL)-start;
      faltaQuanto=aux->init - now;
      sl.tv_sec=time(NULL)+faltaQuanto;
    }
    if (strcmp(aux->status, ARRIVAL)==0) {
      pthread_create(all.allArr+(*estatisticas).tot_aterragens_criadas, NULL, cria_voo, aux);
      pthread_join((*all.allArr)+(*estatisticas).tot_aterragens_criadas, NULL);
      (*estatisticas).tot_aterragens_criadas++;
    } else {
      pthread_create(all.allDep+(*estatisticas).tot_descolagens_criadas, NULL, cria_voo, aux);
      pthread_join((*all.allDep)+(*estatisticas).tot_descolagens_criadas, NULL);
      (*estatisticas).tot_descolagens_criadas++;
    }

    aux= removeVoo(aux->name);
    if (aux==NULL) {
      sl.tv_sec=tempo_inicial+10000;
      pthread_cond_timedwait(&creator,&muCondition,&sl);
    }
  }
  pthread_exit(0);
}
void* cria_voo(void* arg) {
  ListaVoos a = (ListaVoos)arg;
  Msg m;

  if (strcmp(a->status,ARRIVAL)==0) {
    m.mtype = 1;
    strcpy(m.name, a->name);
    m.takeoff_eta = a->takeoff_eta;
    m.fuel = a->fuel;
  } else{
    m.mtype = GESTOR_TORRE;
    strcpy(m.name, a->name);
    m.takeoff_eta = a->takeoff_eta;
    m.fuel = -1;
  }
  if(msgsnd(shmidMQ, &m, sizeof(Msg)-sizeof(long), 0) < 0) perror("Message not sent.\n");
  pthread_exit(0);
}

// ORDENAMENTO DA FILA DE ARRIVAL E DEPARTURE
void orderAll(config configuration){
  int i=0,j=0;
  while(i<depIndex && j<arrIndex){
    if (((chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff)||((chegadasArray+j)->eta>=(partidasArray+i)->takeOff && (chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures && (chegadasArray+j)->fuel1-configuration.holding_min<=0)||((chegadasArray+j)->eta>=(partidasArray+i)->takeOff  && (chegadasArray+j)->fuel1-configuration.holding_min<=0 && (chegadasArray+j)->eta<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) || ((chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals>=(partidasArray+i)->takeOff && (chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures && (chegadasArray+j)->fuel1-configuration.holding_min<=0)||(((chegadasArray+j)->eta>=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures))){
      if (((chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff)||((chegadasArray+j)->eta>=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures)) {

        strcpy((todosArray+todosIndex)->name,(chegadasArray+j)->name);
        if ((todosArray+todosIndex-1)->fuel==-1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) {
          (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures-(chegadasArray+j)->eta;
        } else if((todosArray+todosIndex-1)->fuel!= 0 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals){
          (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals-(chegadasArray+j)->eta;
        } else (todosArray+todosIndex)->hour = (chegadasArray+j)->eta;

        (todosArray+todosIndex)->fuel = (chegadasArray+j)->fuel;
        (todosArray+todosIndex)->init=(chegadasArray+j)->init;
        todosIndex++;
        j++;
      }
      else if((chegadasArray+j)->eta>=(partidasArray+i)->takeOff && (chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures && (chegadasArray+j)->fuel1-configuration.holding_min<=0){
        (*estatisticas).tempo_espera_descolar +=  configuration.duracao_arrivals+configuration.intervalo_arrivals;
        addTakeOff((chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals,i);
        strcpy((todosArray+todosIndex)->name,(chegadasArray+j)->name);
        if ((todosArray+todosIndex-1)->fuel==-1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) {
          (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures-(chegadasArray+j)->eta;
        } else if((todosArray+todosIndex-1)->fuel!= -1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals){
          (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals-(chegadasArray+j)->eta;
        } else (todosArray+todosIndex)->hour = (chegadasArray+j)->eta;

        (todosArray+todosIndex)->fuel = (chegadasArray+j)->fuel;
        (todosArray+todosIndex)->init=(chegadasArray+j)->init;
        todosIndex++;
        j++;
      } else if(((chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals>=(partidasArray+i)->takeOff && (chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures && (chegadasArray+j)->fuel1-configuration.holding_min<=0)){
        (*estatisticas).tempo_espera_descolar +=  configuration.duracao_arrivals+configuration.intervalo_arrivals;
        addTakeOff((chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals,i);

        strcpy((todosArray+todosIndex)->name,(chegadasArray+j)->name);
        if ((todosArray+todosIndex-1)->fuel==-1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) {
          (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures-(chegadasArray+j)->eta;
        } else if((todosArray+todosIndex-1)->fuel!= -1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals){
          (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals-(chegadasArray+j)->eta;
        } else (todosArray+todosIndex)->hour = (chegadasArray+j)->eta;

        (todosArray+todosIndex)->fuel = (chegadasArray+j)->fuel;
        (todosArray+todosIndex)->init=(chegadasArray+j)->init;
        todosIndex++;
        j++;
      } else if(((chegadasArray+j)->eta>=(partidasArray+i)->takeOff  && (chegadasArray+j)->fuel1-configuration.holding_min<0 && (chegadasArray+j)->eta<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures)){
        (*estatisticas).tempo_espera_descolar +=  configuration.duracao_arrivals+configuration.intervalo_arrivals;
        addTakeOff((chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals,i);

        strcpy((todosArray+todosIndex)->name,(chegadasArray+j)->name);
        if ((todosArray+todosIndex-1)->fuel==-1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) {
          (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures-(chegadasArray+j)->eta;
        } else if((todosArray+todosIndex-1)->fuel!= -1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals){
          (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals-(chegadasArray+j)->eta;
        } else (todosArray+todosIndex)->hour = (chegadasArray+j)->eta;
        (todosArray+todosIndex)->fuel = (chegadasArray+j)->fuel;
        (todosArray+todosIndex)->init=(chegadasArray+j)->init;
        todosIndex++;
        j++;
      }
    } else if((chegadasArray+j)->eta>=(partidasArray+i)->takeOff && (chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures && (chegadasArray+j)->fuel1-configuration.holding_min>0){
      (*estatisticas).holdings_aterragem++;
      (chegadasArray+j)->holds++;
      (chegadasArray+j)->eta += configuration.holding_min;
      (chegadasArray+j)->fuel1 -= configuration.holding_min;
      orderArr();
      strcpy((todosArray+todosIndex)->name,(partidasArray+i)->name);
      if ((todosArray+todosIndex-1)->fuel==-1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures))) {
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else if((todosArray+todosIndex-1)->fuel!= -1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) || ((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals) || ((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals))){
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel!=-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel==-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else (todosArray+todosIndex)->hour = (partidasArray+i)->takeOff;

      (todosArray+todosIndex)->fuel=(partidasArray+i)->fuel;
      (todosArray+todosIndex)->init=(partidasArray+i)->init;
      todosIndex++;
      i++;
    } else if((chegadasArray+j)->eta>=(partidasArray+i)->takeOff && (chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures && (chegadasArray+j)->fuel1-configuration.holding_min>0){
      (*estatisticas).holdings_aterragem++;
      (chegadasArray+j)->holds++;
      (chegadasArray+j)->eta += configuration.holding_min;
      (chegadasArray+j)->fuel1 -= configuration.holding_min;
      orderArr();
      strcpy((todosArray+todosIndex)->name,(partidasArray+i)->name);
      if ((todosArray+todosIndex-1)->fuel==-1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures))) {
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else if((todosArray+todosIndex-1)->fuel!= -1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) || ((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals) || ((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals))){
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel!=-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel==-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else (todosArray+todosIndex)->hour = (partidasArray+i)->takeOff;

      (todosArray+todosIndex)->fuel=(partidasArray+i)->fuel;
      (todosArray+todosIndex)->init=(partidasArray+i)->init;
      todosIndex++;
      i++;
    } else if((chegadasArray+j)->eta>=(partidasArray+i)->takeOff && (chegadasArray+j)->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures && (chegadasArray+j)->fuel1-configuration.holding_min>0){
      (*estatisticas).holdings_aterragem++;
      (chegadasArray+j)->holds++;
      (chegadasArray+j)->eta += configuration.holding_min;
      (chegadasArray+j)->fuel1 -= configuration.holding_min;
      orderArr();
      strcpy((todosArray+todosIndex)->name,(partidasArray+i)->name);
      if ((todosArray+todosIndex-1)->fuel==-1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures))) {
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else if((todosArray+todosIndex-1)->fuel!= -1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) || ((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals) || ((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals))){
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel!=-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel==-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else (todosArray+todosIndex)->hour = (partidasArray+i)->takeOff;

      (todosArray+todosIndex)->fuel=(partidasArray+i)->fuel;
      (todosArray+todosIndex)->init=(partidasArray+i)->init;
      todosIndex++;
      i++;
    } else if((chegadasArray+j)->eta>=(partidasArray+i)->takeOff  && (chegadasArray+j)->fuel1-configuration.holding_min>0 && (chegadasArray+j)->eta<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures){
      (*estatisticas).holdings_aterragem++;
      (chegadasArray+j)->holds++;
      (chegadasArray+j)->eta += configuration.holding_min;
      (chegadasArray+j)->fuel1 -= configuration.holding_min;
      orderArr();
      strcpy((todosArray+todosIndex)->name,(partidasArray+i)->name);
      if ((todosArray+todosIndex-1)->fuel==-1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures))) {
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else if((todosArray+todosIndex-1)->fuel!= -1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) || ((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals) || ((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals))){
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel!=-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel==-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else (todosArray+todosIndex)->hour = (partidasArray+i)->takeOff;

      (todosArray+todosIndex)->fuel=(partidasArray+i)->fuel;
      (todosArray+todosIndex)->init=(partidasArray+i)->init;
      todosIndex++;
      i++;
    }
  }
  if (i<depIndex && j>=arrIndex) {

    while(i<depIndex){
      strcpy((todosArray+todosIndex)->name,(partidasArray+i)->name);
      if ((todosArray+todosIndex-1)->fuel==-1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) ||((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures))) {
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else if((todosArray+todosIndex-1)->fuel!= -1 && (((todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals>=(partidasArray+i)->takeOff && (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals<=(partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures) || ((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals) || ((partidasArray+i)->takeOff>=(todosArray+todosIndex-1)->hour && (partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals))){
        (todosArray+todosIndex)->hour = (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel!=-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals;
      } else if(((partidasArray+i)->takeOff+configuration.duracao_departures+configuration.intervalo_departures<=(todosArray+todosIndex-1)->hour)&&((todosArray+todosIndex-1)->fuel==-1)){
        (todosArray+todosIndex)->hour=(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures;
      } else (todosArray+todosIndex)->hour = (partidasArray+i)->takeOff;

      (todosArray+todosIndex)->fuel=(partidasArray+i)->fuel;
      (todosArray+todosIndex)->init=(partidasArray+i)->init;
      todosIndex++;
      i++;
    }
  }
  else if(i>=depIndex && j<arrIndex){
    while(j<arrIndex){
      strcpy((todosArray+todosIndex)->name,(chegadasArray+j)->name);
      if ((todosArray+todosIndex-1)->fuel==-1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures) {
        (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_departures+configuration.intervalo_departures-(chegadasArray+j)->eta;
      } else if((todosArray+todosIndex-1)->fuel!=-1 && (chegadasArray+j)->eta>(todosArray+todosIndex-1)->hour && (chegadasArray+j)->eta<(todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals){
        (todosArray+todosIndex)->hour += (todosArray+todosIndex-1)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals-(chegadasArray+j)->eta;
      }else (todosArray+todosIndex)->hour = (chegadasArray+j)->eta;

      (todosArray+todosIndex)->fuel = (chegadasArray+j)->fuel;
      (todosArray+todosIndex)->init=(chegadasArray+j)->init;
      todosIndex++;
      j++;
    }
  }
}
void orderDep() {
  int i,j;
  Partidas aux;
  for (i = 0; i < depIndex; i++) {
    for (j = i+1; j < depIndex; j++) {
      if ((partidasArray+i)->takeOff > (partidasArray+j)->takeOff) {
        aux = *(partidasArray+i);
        strcpy((partidasArray+i)->name,(partidasArray+j)->name);
        (partidasArray+i)->init = (partidasArray+j)->init;
        (partidasArray+i)->takeOff = (partidasArray+j)->takeOff;
        strcpy((partidasArray+j)->name,aux.name);
        (partidasArray+j)->init = aux.init;
        (partidasArray+j)->takeOff = aux.takeOff;
      }
    }
  }
}
void orderArrEta(FILE *log, config configuration) {
  int j,i,counter;
  for(i=0;i<2;i++){
    if(i==0){
      for (j = 1; j<arrIndex; j++) {
        counter = countTime(j, configuration);
        if ((chegadasArray+j)->fuel < counter && (chegadasArray+j)->fuel<(chegadasArray+j)->eta+configuration.holding_min){
          (*estatisticas).voos_rejeitados++;
          printf("%s %s LEAVING TO OTHER AIRPORT => FUEL = 0\n", get_hours(horas), (chegadasArray+j)->name);
          (*estatisticas).voos_redirecionados++;
          sem_wait(&acesso_log);
          fprintf(log, "%s %s LEAVING TO OTHER AIRPORT => FUEL = 0\n", get_hours(horas), (chegadasArray+j)->name);
          sem_post(&acesso_log);
          removeFlight(j);
          j--;
          orderArr();
        }
      }
    } else{
      for (j = 1; j<arrIndex; j++) {
        counter = countTime(j, configuration);
        if ((chegadasArray+j)->eta < counter){
          (chegadasArray+j)->eta = (chegadasArray+j)->eta + configuration.holding_min;
          (chegadasArray+j)->holds++;
          (*estatisticas).holdings_aterragem++;
          (*estatisticas).tempo_espera_aterrar += ((chegadasArray+j)->holds) * (configuration.holding_min);
          sem_wait(&acesso_log);
          fprintf(log,"%s %s HOLDING %d\n",get_hours(horas),(chegadasArray+j)->name,(chegadasArray+j)->eta-((chegadasArray+j)->holds *  configuration.holding_min));
          sem_post(&acesso_log);
        }
        orderArr();
      }
    }
  }
}
void orderArr() {
  int i,j;
  Chegadas aux;
  for (i=0; i < arrIndex; i++) {
    for (j=1; j < arrIndex; j++) {
      if ((chegadasArray+i)->eta > (chegadasArray+j)->eta) {
        aux = *(chegadasArray+i);
        strcpy((chegadasArray+i)->name,(chegadasArray+j)->name);
        (chegadasArray+i)->init = (chegadasArray+j)->init;
        (chegadasArray+i)->eta = (chegadasArray+j)->eta;
        (chegadasArray+i)->fuel = (chegadasArray + j)->fuel;
        (chegadasArray+i)->holds = (chegadasArray + j)->holds;
        strcpy((chegadasArray+j)->name,aux.name);
        (chegadasArray+j)->init = aux.init;
        (chegadasArray+j)->eta = aux.eta;
        (chegadasArray+j)->fuel = aux.fuel;
        (chegadasArray+j)->holds = aux.holds;
      }
    }
  }
}
void acao(FILE *log, config configuration){
  int i,now=0;
  long int actual = time(NULL)/(configuration.ut);
  for(i=0;i<todosIndex;i++){
    while((todosArray+i)->hour/configuration.ut>now) now = (time(NULL)-actual)/(configuration.ut);
    if ((todosArray+i)->fuel==-1) {
      printf("%s %s DEPARTURE 1R started\n", get_hours(horas),(todosArray+i)->name);
      sem_wait(&acesso_log);
      fprintf(log,"%s %s DEPARTURE 1R started\n", get_hours(horas),(todosArray+i)->name);
      sem_post(&acesso_log);

      while(now<(todosArray+i)->hour+configuration.duracao_departures+configuration.intervalo_departures) now = (time(NULL)-actual)/configuration.ut;

      printf("%s %s DEPARTURE 1R concluded\n", get_hours(horas),(todosArray+i)->name);
      sem_wait(&acesso_log);
      fprintf(log,"%s %s DEPARTURE 1R concluded\n", get_hours(horas),(todosArray+i)->name);
      sem_post(&acesso_log);
    } else{
      printf("%s %s LANDING 28R started\n", get_hours(horas),(todosArray+i)->name);
      sem_wait(&acesso_log);
      fprintf(log,"%s %s LANDING 28R started\n", get_hours(horas),(todosArray+i)->name);
      sem_post(&acesso_log);

      while(now<(todosArray+i)->hour+configuration.duracao_arrivals+configuration.intervalo_arrivals) now = time(NULL)-actual;

      printf("%s %s LANDING 28R concluded\n", get_hours(horas),(todosArray+i)->name);
      sem_wait(&acesso_log);
      fprintf(log,"%s %s LANDING 28R concluded\n", get_hours(horas),(todosArray+i)->name);
      (*estatisticas).tot_voos_aterrados++;
      sem_post(&acesso_log);
    }
  }
}

// REMOVER VOOS NA CT
void removeDep() {
  int i;
  for (i=0; i < depIndex-1; i++) {
    strcpy((partidasArray+i)->name,(partidasArray+i+1)->name);
    (partidasArray+i)->init  = (partidasArray+i+1)->init;
    (partidasArray+i)->takeOff = (partidasArray+i+1)->takeOff;
  }
  depIndex--;
  partidasArray = realloc(partidasArray,sizeof(Partidas)*depIndex);
}
void removeFlight(int i) {
  for (; i < arrIndex-1; i++) {
    strcpy((chegadasArray+i)->name,(chegadasArray+i+1)->name);
    (chegadasArray+i)->init  = (chegadasArray+i+1)->init;
    (chegadasArray+i)->eta = (chegadasArray+i+1)->eta;
    (chegadasArray+i)->fuel = (chegadasArray+i+1)->fuel;
  }
  arrIndex--;
  chegadasArray = realloc(chegadasArray,sizeof(Chegadas)*arrIndex);
}

void addTakeOff(int diferenca, int i){
  (partidasArray+i)->takeOff = diferenca;
  orderDep();
}
int countTime(int i, config configuration) {
  int total=0;
  total += (chegadasArray+(i-1))->eta+configuration.duracao_arrivals+configuration.intervalo_arrivals;
  return total;
}

// SINAIS
void over() {
  char option[SIZE];
  FILE *log=fopen("log.txt", "a");

  do {
    printf("\n ^C pressed. Do you want to abort? [Y/N]: ");
    fgets(option, SIZE, stdin);
  } while(strcasecmp(option, "Y\n")!=0 && strcasecmp(option, "N\n")!=0);

  if (strcasecmp(option, "Y\n")==0) {
    sem_wait(&acesso_log);
    fprintf(log, "%s SIMULATION IS OVER.\n", get_hours(horas));
    sem_post(&acesso_log);
    printf("%s SIMULATION IS OVER.\n", get_hours(horas));
    shmctl(shmid_todosArray, IPC_RMID, NULL);
    shmctl(shmid_esta, IPC_RMID, NULL);
    shmctl(shmidMQ, IPC_RMID, NULL);
    sem_destroy(&acesso_log);
    sem_close(&acesso_log);
    fclose(log);
    kill(ct, SIGKILL);
    exit(0);
  }
  fclose(log);
}
void estat() {
  printf("Número total de voos criados: %d\n", ((*estatisticas).tot_aterragens_criadas+(*estatisticas).tot_descolagens_criadas));
  printf("Número total de voos que aterraram: %d\n", (*estatisticas).tot_voos_aterrados);
  printf("Tempo médio de espera (para além do ETA) para aterrar: %d\n", (*estatisticas).tempo_espera_aterrar/(*estatisticas).tot_voos_aterrados);
  printf("Número total de voos que descolaram: %d\n", (*estatisticas).tot_voos_descolados);
  printf("Tempo médio de espera para descolar: %d\n", (*estatisticas).tempo_espera_descolar/(*estatisticas).tot_voos_descolados);
  printf("Número médio de manobras de holding por voo de aterragem: %d\n", (*estatisticas).tot_voos_aterrados/(*estatisticas).holdings_aterragem);
  printf("Número médio de manobras de holding por voo em estado de urgência: %d\n", (*estatisticas).tot_voos_emergencia/(*estatisticas).holdings_emergencia);
  printf("Número de voos redirecionados para outro aeroporto: %d\n", (*estatisticas).voos_redirecionados);
  printf("Voos rejeitados pela Torre de Controlo: %d\n", (*estatisticas).voos_rejeitados);
}

