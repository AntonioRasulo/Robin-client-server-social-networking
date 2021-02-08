#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>
#include <crypt.h>
#include <time.h>

#define CIP_SIZE 280
#define BUF_SIZE 1024
#define NUM_CHAR_FOLLOW 6
#define SECONDS_PER_DAY 86400

#define NUM_THREADS 4 //num di thread nel pool

//identificatore per i thread
static pthread_t tid [NUM_THREADS];

//semafori degli eventi per i thread
static sem_t evsem[NUM_THREADS];

//array per i thread occupati
static int busy[NUM_THREADS];

//mutex lock globale per le strutture di dati condivise tra i thread
static pthread_mutex_t glock;

//socket di connessione corrente per thread
static int fds [NUM_THREADS];

//semaforo di attesa per thread liberi
static sem_t free_threads;

//Struttura che rappresenta un elemento della lista contenente i nodi padre
struct Nodo 
{
	char name_user[BUF_SIZE];	/*Campo dato*/
	struct Nodo *next_follow;			/*Puntatore all'elemento successivo della lista dei seguiti*/
	struct Nodo *next_father;			/*Puntatore all'elemento successivo della lista dei nodi padri*/
	int num_follower ;					//Numero di seguaci
};

//Struttura che rappresenta un elemento della lista contenente i nodi cip
struct Nodo_cip
{
	char name_user[BUF_SIZE];				//Nome dell'utente che ha effettuato il cip
	char cip[CIP_SIZE];							//Cip
	struct Nodo_cip *next_cip;					//Puntatore all'elemento successivo della lista
	char timestamp[BUF_SIZE];				//Stringa che contiene la data e l'ora in un formato leggibile
	time_t time_cip;									//Contiene la data e l'ora nel formato time_t
};

//Struttura che rappresenta un elemento della lista contenente i nodi contenente le parole calde
struct Nodo_hot_word
{
	char word[CIP_SIZE-1];						//Parola calda
	int occorrenze;									//Occorrenze della parola calda nelle ultime 24h
	struct Nodo_hot_word *next_word;		//Puntatore all'elemento successivo della lista delle parole calde
	time_t time_word;								//Contiene la data e l'ora della parola calda quando è stata incrementata
};															//l'occorrenza l'ultima volta

struct Nodo* listPtr = 0; //All'inizio non ci sono nodi
struct Nodo_cip* cipPtr = 0;//All'inizio non ci sono cip
struct Nodo_hot_word* wordPtr = 0;//All'inizio non ci sono parole calde

//Array che conterrà le parole calde con più occorrenze nelle ultime 24h, ordinate per numero di occorrenze
struct Nodo_hot_word ordinated_array[BUF_SIZE];	 

//Questa funzione pone in tempo la data attuale nel formato time_t ed in ora_convertita in un formato stringa di char
void return_date(time_t* tempo, char* ora_convertita)
{
	time_t current_time;
	char* c_time_string;
	
	current_time = time(NULL);					//time(NULL) fornisce la data nel formato time_t
	*tempo = current_time;
	if (current_time == ((time_t)-1))
    {
		(void) fprintf(stderr, "Failure to obtain the current time.\n");
		exit(EXIT_FAILURE);
    }
	
	/* Convert to local time format. */
    c_time_string = ctime(&current_time);

	if(c_time_string == NULL)
    {
		(void) fprintf(stderr, "Failure to convert the current time.\n");
		exit(EXIT_FAILURE);
    }
	
	strcpy(ora_convertita,c_time_string);
	
}

char char_for_salt()
{
	int r, t1, t2, t3, tot;
	
	//Genero i caratteri del salt in modo tale che appartengano ad uno degli
	//intervalli previsti: ./0..9 a..z A..Z
	t1 = '9' - '.' + 1;
	t2 = 'z' - 'a' +1;
	t3 = 'Z' - 'A' +1;
	tot = t1 + t2 + t3;
	
	r = rand();
	r = r % tot;
	if(r<t1) return r + '.';
	if(r < t1 + t2) return r - t1 + 'A';
	return r - t1 -t2 +'a';
}

//Funzione che genera due sali
//Un sale è una sequenza casuale di bit utilizzata assieme ad una passwd come input 
//ad una funzione unidirezionale (difficile da invertire) il cui output è conservato al posto 
//della sola passwd
void create_salt(char salt[])
{
	salt[0] = char_for_salt();
	salt[1] = char_for_salt();
	
	printf("Salt: %c%c\n",salt[0],salt[1]);
}


//Questa funzione cerca all'interno della lista dei nodi padre il nodo con nome "name" e lo restituisce.
//i varrà 0 se il nodo non è presente mentre varrà 1 se il nodo sarà presente
struct Nodo* find_node_father(char* name, int * i)
{

	struct Nodo* tmpPtr = listPtr;
	
	while(tmpPtr  != 0)
	{	
		if(strcmp(tmpPtr->name_user, name) == 0)
		{
			*i=1;
			return tmpPtr;
		}
		else
		{
			tmpPtr = tmpPtr -> next_father;
		}
	}
	*i=0;
	return NULL;
	
}

//Questa funzione cerca all'interno della lista dei nodi follow il nodo con nome "name_follow".
//Questa funzione restituisce 0 se il nodo sarà presente, altrimenti restituisce 0
int find_node_follow(struct Nodo* followPtr, char* name_follow)
{
	while(followPtr != 0)
		if(strcmp(followPtr -> name_user, name_follow) == 0)
			return 0;
		else
			followPtr = followPtr->next_follow;
	return 1;
}

//Questa funzione cerca all' interno della lista delle parole calde il nodo con la parola "word"
int find_hot_word(char *word)
{
	struct Nodo_hot_word* tmpPtr = wordPtr;
	time_t tmpTimestamp;
	char tmpOrario [BUF_SIZE];
	while(tmpPtr != 0)
	{
		if(strcmp(tmpPtr -> word, word) == 0)
		{
			return_date(&tmpTimestamp, tmpOrario);	//tmpOrario non verrà utilizzato
			
			//viene aggiornato il tempo dell'ultima occorrenza della parola calda
			tmpPtr -> time_word=tmpTimestamp;			
			tmpPtr -> occorrenze += 1;			//Incrementa l'occorrenza della parola calda		
			return 1;
		}
		
		tmpPtr = tmpPtr -> next_word;	
		
	}
	
	return 0;
	
}

//Questa funzione aggiunge un nodo alla lista delle parole calde
void add_hot_word(char* word)
{
	struct Nodo_hot_word* elem;
	time_t tmpTimestamp;
	char tmpOrario [BUF_SIZE];
	elem = (struct Nodo_hot_word*) malloc(sizeof(*elem));
	return_date(&tmpTimestamp, tmpOrario);					//tmpOrario non verrà usato
	strcpy(elem -> word, word);
	elem -> next_word = wordPtr;
	elem -> occorrenze = 1;
	elem -> time_word=tmpTimestamp;
	wordPtr = elem;
}

//Questa funzione mette nell'array odinated_array i nodi con le parole calde in ordine decrescente
//di occorrenza considerando le parole calde delle ultime 24h.
//Inoltre scrive in coda alla stringa di char "msg" le parole nel formato "Posizione Parola_calda (occorrenze)"
void ordina_array(char* msg)
{
	struct Nodo_hot_word* tmpPtr = wordPtr;
	int i, j, cnt = 0, bigger, gap;
	char tmpStr[BUF_SIZE];
	time_t tmpTimestamp;
	char tmpOrario[BUF_SIZE];
	return_date(&tmpTimestamp,tmpOrario);
	if(tmpPtr != 0 )
	{	//metto nell'array gli elementi della lista delle parole
		//in maniera disordinata
		while(tmpPtr != 0)
		{
			ordinated_array[cnt] = *tmpPtr;
			tmpPtr = tmpPtr -> next_word;
			cnt++;
		}
		//Algoritmo per ordinare l'array
		for(i=0; i<cnt - 1; i++)
		{
			bigger = i;
			for(j = i+1; j< cnt; j++)
			{
				if(ordinated_array[j].occorrenze >= ordinated_array[bigger].occorrenze)
				{
					bigger = j;
				}
			}
			
			struct Nodo_hot_word tmpNode = ordinated_array[bigger];
			ordinated_array[bigger] = ordinated_array[i];
			ordinated_array[i] = tmpNode;
		}
		//vengono concatenate le parole calde delle ultime 24h in coda a msg		
		if(cnt<10)
			for(i=0; i<cnt; i++)
			{
				gap = difftime(tmpTimestamp,ordinated_array[i].time_word);
				if(gap < SECONDS_PER_DAY)
				{	
					sprintf(tmpStr,"%d° %s (%d)\n", i+1, ordinated_array[i].word, ordinated_array[i].occorrenze);
					strcat(msg,tmpStr);
				}
			}
		else
			for(i=0; i<10; i++)
			{
				gap = difftime(tmpTimestamp,ordinated_array[i].time_word);
				if(gap < SECONDS_PER_DAY)
				{
					sprintf(tmpStr,"%d° %s (%d)\n", i+1, ordinated_array[i].word, ordinated_array[i].occorrenze);
					strcat(msg,tmpStr);
				}
			}
	}
	else 
	{
		
		strcat(msg,"Non ci sono ancora argomenti caldi\n");
		
	}
}

//Aggiunge un nodo all lista dei nodi padre
struct Nodo* add_node_father(char* name)
{
	//Nuovo nodo
	struct Nodo* elem;
	//Si alloca spazio per il nuovo nodo
	elem = (struct Nodo*) malloc(sizeof(*elem));
	//Il dato del nodo sarà il nome di chi ha effettuato il login
	strcpy(elem->name_user,name);
	//Il nuovo elemento punterà a listPtr, che è il puntatore globale perchè così quando si effettua questa operazione 0 <- nodo <-nodo <- ...<- nodo <-listPtr
	elem -> next_father = listPtr;
	//inizialmente un nodo padre non punta a nessun nodo follower
	elem -> next_follow = 0;
	//un nodo padre appena creato non ha seguaci
	elem -> num_follower = 0;
		
	listPtr = elem;
	
	return elem;
}

//Si aggiunge alla lista dei nodi cip un nuovo elemento
void add_to_cip_list(char* cip, char* name_user)
{
	struct Nodo_cip* elem;
	char tmpTimestamp[BUF_SIZE];
	time_t tmpTime_cip;
		
	elem = (struct Nodo_cip*) malloc(sizeof(*elem));
	strcpy(elem -> name_user, name_user);
	strcpy(elem -> cip, cip);
	return_date(&tmpTime_cip, tmpTimestamp);
	strcpy(elem -> timestamp, tmpTimestamp);
	elem -> time_cip = tmpTime_cip;
	elem -> next_cip = cipPtr;
	
	cipPtr = elem;
}

//Questa funzione restituisce 1 se il cip è stato scritto nell'ultima ora,
//altrimenti restituisce 0
int last_hour_cip(struct Nodo_cip* cipNode)
{
	time_t tmpTime;
	char actual_date[BUF_SIZE];
	return_date(&tmpTime, actual_date);
	int seconds_gap;

	seconds_gap = difftime(tmpTime, cipNode -> time_cip);
	
	if(seconds_gap < SECONDS_PER_DAY/24)
		return 1;
	else
		return 0;
}


//Questa funzione va a concatenare in fondo a "msg" la lista dei cip scritti nelle ultime ore
//dall'utente seguito
void show_last_hour_cip(struct Nodo* followPtr, char* msg)
{
	struct Nodo_cip* tmpcipPtr = cipPtr;
	struct Nodo* tmpfollowPtr = followPtr -> next_follow;
	char name_user_tmp [BUF_SIZE];
	char final_msg [BUF_SIZE];
	char tmp_msg [2*BUF_SIZE];
	memset(name_user_tmp, '\0', sizeof(name_user_tmp));	//Pulisco le stringhe di char
	memset(final_msg, '\0', sizeof(final_msg));
	memset(tmp_msg, '\0', sizeof(tmp_msg));
	
	if(tmpfollowPtr == 0)
	{
		strcpy(final_msg,"Non segui nessun utente\n");
	}
	else if(tmpcipPtr == 0)
	{
		strcpy(final_msg,"Nessun utente che segui ha postato cip\n");
	}
	else
	{
		while(tmpfollowPtr != 0)
		{
			memset(name_user_tmp, '\0', sizeof(name_user_tmp));
			strcpy(name_user_tmp,tmpfollowPtr->name_user);//copio il nome del follow
			while(tmpcipPtr != 0)	//quando non si è esplorata tutta la lista dei cip
			{
				if(strcmp(name_user_tmp,tmpcipPtr -> name_user) == 0)	//se il cip è stato effettuato da un utente seguito
					if(last_hour_cip(tmpcipPtr))		//Se il cip è stato effettuato nell'ultima ora
					{
						sprintf(tmp_msg,"%s cip dell'utente %s: %s\n", tmpcipPtr -> timestamp, name_user_tmp, tmpcipPtr -> cip);
						strcat(final_msg,tmp_msg);
					}
				tmpcipPtr = tmpcipPtr -> next_cip;
			}
			tmpcipPtr = cipPtr;	//riporto il puntatore in cima alla lista dei cip
			tmpfollowPtr = tmpfollowPtr -> next_follow;
		}
	}

		strcat(msg,final_msg);
	
}

//Questa funzione aumenta il numero di seguaci del nodo con nome "name_father"
// che si trova nella lista dei nodi padre
int increment_follower(char* name_father)
{
	struct Nodo* tmpPtr = listPtr;
	
	while(tmpPtr != 0)
		if(strcmp(tmpPtr -> name_user, name_father) == 0)
		{
			tmpPtr -> num_follower += 1;
			return 1;
		}
		else
		{
			tmpPtr = tmpPtr -> next_father;
		}
	return 0;
}

//Si aggiunge alla lista dei nodi dei follow un nuovo elemento
//Il nodo del seguace (nodo padre) viene puntato dal puntatore followPtr
struct Nodo* add_to_follow_list(struct Nodo* followPtr,char* name_follow)
{
	struct Nodo *elem;
	
	elem = (struct Nodo*) malloc(sizeof(*elem));
	strcpy(elem->name_user, name_follow);
	elem -> next_father = 0;
	elem -> next_follow = 0;
	
	struct Nodo *currentPtr = followPtr;
	struct Nodo *previousPtr = NULL;
	
	while(currentPtr != 0)
	{
		previousPtr = currentPtr;
		currentPtr = currentPtr -> next_follow;
	}
	
	previousPtr->next_follow = elem;
	
	return elem;
		
}

//Create and initialize a listening socket
static int init_server(int myport)
{
	struct sockaddr_in my_addr;
	int fd;
	
	//La struttura my_addr viene pulita
	memset(&my_addr, 0, sizeof(my_addr));
	
	//Famiglia del socket: IPv4
	my_addr.sin_family = AF_INET;
	
	//Porta della struttura dati per il socket
	//htons converte myport in un formato di rete (big endian) così che non sia
	//dipendente dal formato usato dal calcolatore
	my_addr.sin_port = htons(myport);
	
	//Accetta i messaggi da ogni interfaccia
	my_addr.sin_addr.s_addr = INADDR_ANY;
	
	//La primitiva socket() crea un socket. Restituisce un file descriptor.
	//Famiglia di protocolli usata: AF_INET -->  IPv4
	//Tipo di comunicazione che si vuole usare: SOCK_STREAM --> TCP
	fd = socket(AF_INET, SOCK_STREAM, 0);
	
	//La primitiva bind() assegna un nome locale al socket creato con socket()
	//Usata dal server per specificare l'indirizzo su cui accetta le richieste
	//In caso di errore restituisce -1
	if(bind(fd, (struct sockaddr*) &my_addr, sizeof(my_addr)) < 0)
	{
		perror("bind()");
		exit(-1);
	}
	
	//La primitiva listen() prepara il socket per attendere eventuali
	//connessioni in ingresso
	//Viene usata dal server per segnalare al kernel la sua disponibilità
	//ad accettare richieste di connessione destinate al nome locale
	//specificato dalla bind() precedente
	if(listen(fd,10)<0)
	{
		perror("listen()");
		exit(-1);
	}
	
	printf("Server listening on port %d \n", myport);
	return fd;
}


//Questa funzione prende in ingresso nome e password e ci dice se ad essi è associato
// un utente presente nel sistema (nel file di testo "users")
int login(char* email, char* passwd)
{
	//File users
	FILE* fd;
	
	//Riga del file users
	char line[BUF_SIZE];
	
	//Campo username di una riga del file
	char usr[BUF_SIZE];
	
	//password cifrata di una riga del file
	char epwd[BUF_SIZE];
	
	//Salt estratto da una riga del file
	char salt[2];
	
	//Password inserita dall'utente e cifrata
	char res[BUF_SIZE];
	
	//Apro il file users in modalità di sola lettura
	fd = fopen("users","r");
	if(!fd)
		return 0;
	
	//Leggo tutte le righe del file
	while(fgets(line, sizeof(line), fd))
	{
		//Estraggo username e password dalla riga appena letta
		sscanf(line, "%s %s", usr, epwd);
		
		//Estraggo il salt
		salt[0] = epwd[0];
		salt[1] = epwd[1];
		
		//Cifro la password inserita dall'utente, uso il salt appena estratto
		strncpy(res, crypt(passwd,salt),sizeof(res));
		
		//username e password cifrata coincidono con quelli del file, l'utente
		//ha eseguito login con successo
		if(strncmp(email,usr, sizeof(usr)) == 0)			//nome utente trovato
		{	
			if (strncmp (res, epwd, sizeof(res)) == 0 )	
				return 1;									//password giusta
			else
				return -1;									//password errata
		}
	}
	
	//login fallito
	return 0;

}

//Questa funzione prende in ingresso un nome e ci dice se ad essi è associato
// un utente presente nel sistema (nel file di testo "users")
int find_name(char *email)
{
	//File users
	FILE* fd;
	
	//Riga del file users
	char line[BUF_SIZE];
	
	//Campo username di una riga del file
	char usr[BUF_SIZE];
	
	//Apro il file users in lettura
	fd = fopen("users","r");
	if(!fd)
		return 0;
	
	//Leggo tutte le righe del file
	while(fgets(line, sizeof(line), fd))
	{
		//Estraggo username dalla riga appena letta
		sscanf(line, "%s", usr);
		
		//Se è stato trovato un utente con quel nome
		if(strncmp(email,usr, sizeof(usr)) == 0)			//nome utente trovato
			return 1;
		
	}
	
	return 0;
	
}

//Questa funzione va a scrivere in fondo al file di testo "users"
//la registrazione di un nuovo utente
int registration(char* email, char* passwd)
{
	//Nome del nuovo utente
	char username[BUF_SIZE];
	
	//Password in chiaro del nuovo utente
	char password[BUF_SIZE];
	
	//Riga del file users
	char line[BUF_SIZE];

	//Password cifrata del nuovo utente
	char res[BUF_SIZE];
	
	// File delle password
	FILE* fd;
	
	//Salt
	char salt[2];
	
	//Contiene la nuova linea del file degli utenti
	char buf[BUF_SIZE];
	
	//Byte scritti
	ssize_t w;
	
	//Inizializzo il generatore di numeri casuali
	srand(time (NULL));
		
	// Apro il file degli utenti in modalità di lettura
	fd = fopen("users","r");
	if(fd)
		while(fgets(line, sizeof(line), fd))
		{
			//Estraggo username dalla riga appena letta
			sscanf(line, "%s", username);
						
			//username e password cifrata coincidono con quelli del file, l'utente
			//ha eseguito login con successo
			if(strncmp(email,username, sizeof(username)) == 0 )
				return 0;
		}
		
	//Imposto username e password
	strcpy(username,email);
	strcpy(password,passwd);
	
	
	// Genero il salt
	create_salt(salt);
	
	// Cifro la password
	strncpy(res, crypt(password, salt), sizeof(res));
	
	// Apro il file degli utenti in scrittura dal fondo
	fd = fopen("users", "a");
	if(fd == NULL)
	{
		perror("opening file");
		exit(-1);
	}
	
	// Genero la nuova riga: username password_cifrata
	// I primi due caratteri della password cifrata sono il salt
	snprintf(buf, sizeof(buf), "%s %s\n", username, res);
	
	// Scrivo la nuova riga nel file
	w = fwrite(buf, 1, strlen(buf), fd);
	if(w != strlen(buf)) 
	{
		perror("fwrite");
		exit(-1);
	}
	
	fclose(fd);
	printf("L'utente è stato aggiunto\n");
	return 1;
}

/*I/O for a single client request*/
static void serve_request(int cfd, int index)
{
	char msg_email[]="Inserisci l' indirizzo di posta elettronica:";
	char msg_passwd[]="Inserisci la password:";
	char add_success[]="Registrazione eseguita.";
	char add_failure[]="Registrazione fallita, utente già presente.";
	char login_success[]="Login effettuato con successo.";
	char login_failure_passwd[]="Errore: password errata.";
	char login_failure_np[]="Errore, utente non presente: ";
	char follow_success[] ="Adesso segui:";
	char follow_fail[]="Operazione di follow fallita, utente non presente: ";
	char follow_fail_already[] = "Operazione di follow fallita, segui già l'utente: ";
	char help_logout[] = "Comandi disponibili:\n"
			"help	-->	mostra l'elenco dei comandi disponibili\n"
			"register	-->	registrazione al social network\n"
			"login	-->	effettua il login nel social network\n"
			"quit	-->	esci dal social network\n";
	char help_login[] = "Comandi disponibili:\n"
			"help	-->	mostra l'elenco dei comandi disponibili\n"
			"follow nome_utente	-->	segui un altro utente\n"
			"cip	-->	nuovo cip\n"
			"home	-->	visualizza la mia home page\n"
			"quit	-->	esci dal social network\n";
	char quit_msg[] = "Sei uscito dal sistema.\n";
	char cip_msg[] = "Scrivere il cip:";
	char cip_send[]="Cip inviato.\n";
	char msg[BUF_SIZE], name_user[BUF_SIZE], passwd[BUF_SIZE];
	char cip[CIP_SIZE];
	int dim, ret, result, log = 0;
	struct Nodo* followPtr = 0;
	int find_father;
		
	memset(msg, '\0', sizeof(msg));	
	
	while(strcmp(msg,"quit") != 0)
	{
		//Pulisco msg (sporco dei messaggi precedenti)
		memset(msg, '\0', sizeof(msg));
		
		printf("THREAD %d : in attesa di un comando\n", index);
		//Viene ricevuta la dimensione del comando
		ret = recv(cfd, (void*)&dim, sizeof(dim), MSG_WAITALL);
		if(ret < 0 || ret < sizeof(dim))
		{
			perror("recv(len)");
			exit(-1);
		}
	 
		dim = ntohl(dim);
		
		//Viene ricevuto il comando
		ret = recv(cfd, (void*) msg, dim, MSG_WAITALL);
		if(ret < 0 || ret<dim)
		{
			perror("recv(msg)");
			exit(-1);
		}
		printf("THREAD %d : ricevuto il comando %s\n", index, msg);
		if(strcmp(msg,"register") == 0 && log == 0)							//La registrazione può essere effettuata solo quando non è stato fatto il login (scelta personale)
		{
					
			//dim contiene il numero di caratteri del msg nel formato di rete
			dim = htonl(strlen(msg_email));
				
			//si invia la dimensione del messaggio 
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
			
			//si invia il messaggio msg_email
			ret = send(cfd, (void*) &msg_email, strlen(msg_email), 0);
			if(ret < 0 || ret < strlen(msg_email))
			{
				perror("send(msg)");
				exit(-1);
			}
		
			//Viene ricevuta la dimensione della email
			ret = recv(cfd, (void*)&dim, sizeof(dim), MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(len)");
				exit(-1);
			}
	 
			dim = ntohl(dim);
			
			char name_user_reg[dim];
		
			memset(msg, '\0', sizeof(msg));
				
			ret = recv(cfd, (void*) &msg, dim, MSG_WAITALL);
		
			if(ret < 0 || ret < dim)
			{
				perror("recv(msg_passwd)");
				exit(-1);
			}
		
			//Metto il nome utente nella stringa name_user_reg
			strcpy(name_user_reg,msg);
			
			//dim contiene il numero di caratteri del msg nel formato di rete
			dim = htonl(strlen(msg_passwd));
				
			//si invia la dimensione del messaggio 
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
			
			//si invia il messaggio msg_passwd
			ret = send(cfd, (void*) &msg_passwd, strlen(msg_passwd), 0);
			if(ret < 0 || ret < strlen(msg_passwd))
			{
				perror("send(msg_passwd)");
				exit(-1);
			}
		
			ret = recv(cfd, (void*)&dim, sizeof(dim), MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(len)");
				exit(-1);
			}
	 
			dim = ntohl(dim);
			
			char passwd_reg[dim];
						
			memset(msg, '\0', sizeof(msg));
					
			ret = recv(cfd, (void*) &msg, dim, MSG_WAITALL);
			
			if(ret < 0 || ret < dim)
			{
				perror("recv(msg)");
				exit(-1);
			}
		
			//Metto la password nella stringa passwd_reg
			strcpy(passwd_reg,msg);
		
			//Registro il nuovo utente con il nome name_user_reg e la password passwd
			result = registration(name_user_reg, passwd_reg);
		
			if(result)
			{
				//dim contiene il numero di caratteri del msg nel formato di rete
				dim = htonl(strlen(add_success));
						
				//si invia la dimensione del messaggio 
				ret = send(cfd, (void*) &dim, sizeof(dim), 0);
				if(ret < 0 || ret < sizeof(dim))
				{
					perror("send(dim)");
					exit(-1);
				}
					
				//si invia il messaggio add_success
				ret = send(cfd, (void*) &add_success, strlen(add_success), 0);
				if(ret < 0 || ret < strlen(add_success))
				{
					perror("send(add_success)");
					exit(-1);
				}
			}
			else
			{
				//dim contiene il numero di caratteri del msg nel formato di rete
				dim = htonl(strlen(add_failure));
						
				//si invia la dimensione del messaggio 
				ret = send(cfd, (void*) &dim, sizeof(dim), 0);
				if(ret < 0 || ret < sizeof(dim))
				{
					perror("send(dim)");
					exit(-1);
				}
					
				//si invia il messaggio add_success
				ret = send(cfd, (void*) &add_failure, strlen(add_failure), 0);
				if(ret < 0 || ret < strlen(add_failure))
				{
					perror("send(add_failure)");
					exit(-1);
				}
			}
		}
		else if(strcmp(msg,"cip") == 0 && log == 1)
		{
			//dim contiene il numero di caratteri del msg nel formato di rete
			dim = htonl(strlen(cip_msg));
				
			//si invia la dimensione del messaggio 
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
			
			//si invia il messaggio cip_msg
			ret = send(cfd, (void*) &cip_msg, strlen(cip_msg), 0);
			if(ret < 0 || ret < strlen(cip_msg))
			{
				perror("send(msg)");
				exit(-1);
			}
			
			//Viene ricevuta la dimensione del cip
			ret = recv(cfd, (void*)&dim, sizeof(dim), MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(len)");
				exit(-1);
			}
	 
			dim = ntohl(dim);
					
			memset(cip, '\0', sizeof(cip));
			//Viene ricevuto il cip
			ret = recv(cfd, (void*) &cip, dim, MSG_WAITALL);
		
			if(ret < 0 || ret < dim)
			{
				perror("recv(msg_passwd)");
				exit(-1);
			}
			
			int i,cnt;
			char hot_word[dim-1];
			memset(hot_word, '\0', sizeof(hot_word));
			pthread_mutex_lock(&glock);
			
			add_to_cip_list(cip, name_user);	//si aggiunge il cip emesso da name_user nella lista dei cip
			for(i=0; i<dim; i++)
			{	
				if('#' == cip[i])				//si trova l'hashtag
				{
					cnt = 1;
					while(' ' != cip[i+cnt] && '\0' != cip[i+cnt])		//quando non si trova uno spazio ed un carattere di terminazione stringa
					{	
						char ch = cip[i+cnt];
						hot_word[cnt-1] = ch;
						cnt++;
					}//se la parola è presente nella lista delle hot word viene aumentato il suo valore di occorrenza e viene tornato 1, se non è presente viene tornato 0
					
					if(find_hot_word(hot_word) == 0)
						add_hot_word(hot_word);					//si aggiunge la parola calda alla lista delle parole calde
					memset(hot_word, '\0', sizeof(hot_word));	//si pulisce hot_word 
				}
			}
			pthread_mutex_unlock(&glock);
			
			//dim contiene il numero di caratteri del msg nel formato di rete
			dim = htonl(strlen(cip_send));
				
			//si invia la dimensione del messaggio 
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
			
			//si invia il messaggio cip_send
			ret = send(cfd, (void*) &cip_send, strlen(cip_send), 0);
			if(ret < 0 || ret < strlen(cip_send))
			{
				perror("send(msg)");
				exit(-1);
			}
			
		}
		else if(strcmp(msg,"login") == 0 && log == 0)										//Se è già stato fatto il login non è possibile effettuarlo nuovamente		
		{
					
			//dim contiene il numero di caratteri del msg nel formato di rete
			dim = htonl(strlen(msg_email));
				
			//si invia la dimensione del messaggio 
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
			
			//si invia il messaggio msg_email
			ret = send(cfd, (void*) &msg_email, strlen(msg_email), 0);
			if(ret < 0 || ret < strlen(msg_email))
			{
				perror("send(msg)");
				exit(-1);
			}
		
			//Viene ricevuta la dimensione della email
			ret = recv(cfd, (void*)&dim, sizeof(dim), MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(len)");
				exit(-1);
			}
	 
			dim = ntohl(dim);
					
			memset(msg, '\0', sizeof(msg));
				
			ret = recv(cfd, (void*) &msg, dim, MSG_WAITALL);
		
			if(ret < 0 || ret < dim)
			{
				perror("recv(msg_passwd)");
				exit(-1);
			}
	
			strcpy(name_user,msg);
		
			//dim contiene il numero di caratteri del msg nel formato di rete
			dim = htonl(strlen(msg_passwd));
				
			//si invia la dimensione del messaggio 
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
			
			//si invia il messaggio msg_passwd
			ret = send(cfd, (void*) &msg_passwd, strlen(msg_passwd), 0);
			if(ret < 0 || ret < strlen(msg_passwd)-1)
			{
				perror("send(msg_passwd)");
				exit(-1);
			}
		
			ret = recv(cfd, (void*)&dim, sizeof(dim), MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(len)");
				exit(-1);
			}
	 
			dim = ntohl(dim);
						
			memset(msg, '\0', sizeof(msg));
					
			ret = recv(cfd, (void*) &msg, dim, MSG_WAITALL);
			
			if(ret < 0 || ret < dim)
			{
				perror("recv(msg)");
				exit(-1);
			}
		
			strcpy(passwd,msg);
			
			result = login(name_user, passwd);		//viene controllato se l'utente ha eseguito il login correttamente
			
			if(result == 1)
			{
				//dim contiene il numero di caratteri del msg nel formato di rete
				dim = htonl(strlen(login_success));
						
				//si invia la dimensione del messaggio 
				ret = send(cfd, (void*) &dim, sizeof(dim), 0);
				if(ret < 0 || ret < sizeof(dim))
				{
					perror("send(dim)");
					exit(-1);
				}
					
				//si invia il messaggio login_success
				ret = send(cfd, (void*) &login_success, strlen(login_success), 0);
				if(ret < 0 || ret < strlen(login_success))
				{
					perror("send(login_success)");
					exit(-1);
				}
				
				log = 1; //login effettuato
				
				/*Ricerca/creazione nodo padre*/
				
				pthread_mutex_lock(&glock);
				
				followPtr = find_node_father(name_user,&find_father);			//Si cerca il nodo padre nell'array dei nodi padre, followPtr punterà al nodo padre nel caso in cui sia presente
				
				if(find_father == 0)								//Se il nodo non è presente nell'array dei nodi padre lo si crea
				{
					followPtr = add_node_father(name_user);			//viene aggiunto il nodo padre. followPtr punterà al nodo padre
				}
								
				pthread_mutex_unlock(&glock);
			
			}
			else if (result == 0)
			{
				//dim contiene il numero di caratteri del msg nel formato di rete
				dim = htonl(strlen(login_failure_np));
						
				//si invia la dimensione del messaggio 
				ret = send(cfd, (void*) &dim, sizeof(dim), 0);
				if(ret < 0 || ret < sizeof(dim))
				{
					perror("send(dim)");
					exit(-1);
				}
					
				//si invia il messaggio add_success
				ret = send(cfd, (void*) &login_failure_np, strlen(login_failure_np), 0);
				if(ret < 0 || ret < strlen(login_failure_np))
				{
					perror("send(login_failure_np)");
					exit(-1);
				}
			}
			else
			{
				//dim contiene il numero di caratteri del msg nel formato di rete
				dim = htonl(strlen(login_failure_passwd));		
				//si invia la dimensione del messaggio 
				ret = send(cfd, (void*) &dim, sizeof(dim), 0);
				if(ret < 0 || ret < sizeof(dim))
				{
					perror("send(dim)");
					exit(-1);
				}
					
				//si invia il messaggio add_success
				ret = send(cfd, (void*) &login_failure_passwd, strlen(login_failure_passwd), 0);
				if(ret < 0 || ret < strlen(login_failure_passwd))
				{
					perror("send(login_failure_passwd)");
					exit(-1);
				}
			}
		}
		else if(strncmp(msg, "follow ", NUM_CHAR_FOLLOW+1)==0 && log==1 )
		{	
			int find, k;
			
			char name_follow[dim-(NUM_CHAR_FOLLOW+1)];
			for(k=NUM_CHAR_FOLLOW+2; k<=dim; k++)
				strcpy(&name_follow[k-(NUM_CHAR_FOLLOW+2)],&msg[k-1]);	//sscanf mi da problemi non so perchè
			
			find=find_name(name_follow);			//viene cercato il nome nel file di testo per vedere se è presente nel sistema
			if(find)											//Il nome corrisponde ad un utente registrato
			{
				int find_follow = 0;//find_follow ci dirà se si segue di già l'utente oppure no
				int increment;
				pthread_mutex_lock(&glock);
				
				followPtr = find_node_father(name_user, &increment);		//followPtr punterà al nodo padre
							
				find_follow = find_node_follow(followPtr, name_follow);	//se ritorna 1 non viene già seguito, se ritorna 0 viene già seguito
								
				if(find_follow)
				{
				
					followPtr = add_to_follow_list(followPtr, name_follow);	//si aggiunge il nodo follow alla lista dei nodi follow del nodo padre
																			//(cioè chi ha fatto il login segue il follower)
																			
					find_node_father(name_follow, &increment);			//Si cerca il nodo padre con nome name_follow
					
					if(increment == 0)						//Se il nodo non è presente nella lista dei nodi padre lo si crea 
					{
						add_node_father(name_follow);
					}
					
					increment_follower(name_follow);	//Si incrementa il numero di seguaci del nodo padre con nome "name_follow"
				}	
								
				pthread_mutex_unlock(&glock);
				
				if(find_follow)
				{
					strcpy(msg, follow_success);
					
					strcat(msg,name_follow);
					
					dim = htonl(strlen(msg));
					ret = send(cfd, (void*) &dim, sizeof(dim), 0);
					if(ret < 0 || ret < sizeof(dim))
					{
						perror("send(dim)");
						exit(-1);
					}
					
					//Si invia il messaggio follow_success
					ret = send(cfd, (void*) &msg, strlen(msg), 0);
					if(ret < 0 || ret < strlen(msg))
					{
						perror("send(follow_success)");
						exit(-1);
					}
				}
				else
				{
					strcpy(msg, follow_fail_already);
					
					strcat(msg,name_follow);
					
					dim = htonl(strlen(msg));
					ret = send(cfd, (void*) &dim, sizeof(dim), 0);
					if(ret < 0 || ret < sizeof(dim))
					{
						perror("send(dim)");
						exit(-1);
					}
					
					//Si invia il messaggio follow_fail_already
					ret = send(cfd, (void*) &msg, strlen(msg), 0);
					if(ret < 0 || ret < strlen(msg))
					{
						perror("send(follow_fail_already)");
						exit(-1);
					}
				}
			}
			else												//Il nome non corrisponde ad un utente registrato
			{
				
				strcpy(msg, follow_fail);
				
				strcat(msg,name_follow);
				
				dim = htonl(strlen(msg));
				ret = send(cfd, (void*) &dim, sizeof(dim), 0);
				if(ret < 0 || ret < sizeof(dim))
				{
					perror("send(dim)");
					exit(-1);
				}
				
				//Si invia il messaggio follow_fail
				ret = send(cfd, (void*) &msg, strlen(msg), 0);
				if(ret < 0 || ret < strlen(msg))
				{
					perror("send(follow_fail)");
					exit(-1);
				}
			}
		}
		else if(strcmp(msg, "help")==0 && log==1)
		{
			dim = htonl(strlen(help_login));
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
				
			//Si invia il messaggio help_login
			ret = send(cfd, (void*) &help_login, strlen(help_login), 0);
			if(ret < 0 || ret < strlen(help_login))
			{
				perror("send(help_login)");
				exit(-1);
			}
		}
		else if(strcmp(msg, "help")==0 && log==0)
		{
			dim = htonl(strlen(help_logout));
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
				
			//Si invia il messaggio help_logout
			ret = send(cfd, (void*) &help_logout, strlen(help_logout), 0);
			if(ret < 0 || ret < strlen(help_logout))
			{
				perror("send(help_logout)");
				exit(-1);
			}
		}
		else if(strcmp(msg,"home") == 0 && log == 1)
		{
			int num_seguaci;
			pthread_mutex_lock(&glock);
			followPtr = find_node_father(name_user,&num_seguaci);			//Viene trovato nella lista dei nodi padre il nodo dell'utente che ha effettuato il login
			num_seguaci = followPtr -> num_follower;
			memset(msg, '\0', sizeof(msg));	//Pulisco msg 
			sprintf(msg,"Hai %d seguaci\n"									//Si scrive in msg il numero di seguaci dell'utente
						"-----------------------\n",num_seguaci);
			show_last_hour_cip(followPtr,msg);								//Si concatenano in msg i cip dell'ultima ora
			pthread_mutex_unlock(&glock);
			strcat(msg,"\n------------------------\n");
			strcat(msg,"Argomenti caldi:\n");
			pthread_mutex_lock(&glock);
			ordina_array(msg);												//Viene ordinato e concatenato in coda a msg l'array degli elementi caldi
			pthread_mutex_unlock(&glock);
			dim = htonl(strlen(msg));
			ret = send(cfd, (void*) &dim, sizeof(dim), 0);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("send(dim)");
				exit(-1);
			}
				
			//Si invia il messaggio di home
			ret = send(cfd, (void*) &msg, strlen(msg), 0);
			if(ret < 0 || ret < strlen(msg))
			{
				perror("send(msg)");
				exit(-1);
			}
			
		}
		
	}
	
	dim = htonl(strlen(quit_msg));
	ret = send(cfd, (void*) &dim, sizeof(dim), 0);
	if(ret < 0 || ret < sizeof(dim))
	{
		perror("send(dim)");
		exit(-1);
	}
				
	//Si invia il messaggio quit_msg
	ret = send(cfd, (void*) &quit_msg, strlen(quit_msg), 0);
	if(ret < 0 || ret < strlen(quit_msg))
	{
		perror("send(quit_msg)");
		exit(-1);
	}
	
	close(cfd);

}


//Thread body for the request handlers. Wait for a signal from the main
//thread, collect the client file descriptor and serve the request.
//Signal the main thread when the request has been finished
static void* handler_body(void *idx)
{
	int index = *((int*)idx);
	int cfd;
	
	//viene liberata la memoria occupata da idx
	free(idx);
	
	for(;;)
	{
		
		//Se il valore del semaforo è zero la wait si blocca
		//forzando un cambio di contesto a favore di un altro
		//thread pronto
		
		//Aspetta per una richiesta assegnata dal thread principale
		sem_wait(&evsem[index]);
		
		//Un thread pronto supera la wait e stampa un messaggio
		printf("Thread %d take charge of a request\n", index);

		//Ogni thread prima di accedere ai dati condivisi deve effettuare la lock
		//sulla variabile mutex glock
		//Blocca l'accesso da parte di altri thread
		//Se più thread eseguono la lock, solo uno la termina e prosegue l'esecuzione
		pthread_mutex_lock(&glock);
		
		//Poniamo in cfd il file descriptor del socket del thread
		cfd = fds[index];
		
		//Con la unlock si va a liberare la variabile mutex glock
		pthread_mutex_unlock(&glock);
		
		//Serve the request
		serve_request(cfd,index);
		printf("Thread %d served a request\n",index);
		
		
		//lock sulla variabile mutex
		pthread_mutex_lock(&glock);
		
		//Il thread non è più occupato ma diventa libero
		busy[index] = 0;
		
		//unlock sulla variabile mutex
		pthread_mutex_unlock(&glock);
		
		//Si sveglia uno dei thread che è in attesa
		//al semaforo free_threads
		sem_post(&free_threads);
	}
	
	//Terminazione del thread, valore di ritorno NULL
	pthread_exit(NULL);
	
}

int main(int argc, char* argv[])
{
	
	struct sockaddr_in c_add;
	socklen_t addrlen;
	int lfd, cfd;
	int myport;
	int i;
	
	if(argc < 3)
	{
		printf("usage: server <host> <porta> \n");
		exit(-1);
	}
	
	//atoi traduce una stringa in un intero
	myport = atoi(argv[2]);
	
	printf("Indirizzo: %s (Porta: %d)\n", argv[1], myport);
	
	//viene creato ed inizializzato un socket
	lfd = init_server(myport);
	
	//Inizializzazione dinamica della variabile mutex glock a 0
	pthread_mutex_init (&glock, 0);
	
	
	//Si assegna a free threads il valore NUM_THREADS dal momento
	//che all'inizio tutti i thread sono liberi
	sem_init(&free_threads, 0, NUM_THREADS);
	for(i = 0; i< NUM_THREADS; i++)
	{
		int *idx = malloc(sizeof(*idx));
		
		if(!idx)
		{
			printf("Error: out of memory\n");
			exit(-1);
		}
		
		*idx = i;
		busy[i] = 0;				//inizialmente il thread #i è libero
		sem_init(&evsem[i], 0, 0);	//il semaforo evsem viene inizializzato a 0
		
		//Creazione dei thread
		//I thread diventano eseguibili
		pthread_create(&tid[i], 0, handler_body, (void*)idx);
		printf("THREAD %d: pronto\n", i);
	}
	
	for(;;)
	{
		//Aspetta per un thread libero
		sem_wait(&free_threads);
		
		
		//La primitiva accept() viene usata dal server per
		//accettare richieste di connessione
		//Estrae la prima richiesta di connessione dalla coda delle
		//connessioni pendenti relativa al listening socket (lfd).
		//Crea un nuovo socket "connected socket" (cfd) per 
		//gestire la nuova connessione.
		//cfd verrà usato per la comunicazione vera e propria col client
		addrlen = sizeof(c_add);
		cfd = accept(lfd, (struct sockaddr*) &c_add, &addrlen);	//Accetta la connessione
		
		//In caso di errore accept restituisce -1
		if(cfd < 0)
		{
			perror("accept()");
			exit(-1);
		}
		
		
		//Lock sulla variabile mutex
		pthread_mutex_lock(&glock);
		for(i=0; i<NUM_THREADS; i++)	//Guarda se c'è un thread libero scansionando l'array busy
		{
			if(!busy[i])//Se viene trovato un thread libero
				break;
		}
		
		//Se i > NUM_THREADS viene stampato un messaggio di errore
		//e viene chiamata la funzione abort per terminare
		//l'esecuzione del programma
		assert(i<NUM_THREADS);
		fds[i] = cfd;	//Viene passato il file descriptor per il socket di connessione
		busy[i] =1;	//Seleziona il thread selezionato come busy
		//unlock sulla variabile mutex
		pthread_mutex_unlock(&glock);
		sem_post(&evsem[i]);//Si sveglia il thread selezionato
		
	}
	
	free(listPtr);
	free(wordPtr);
	free(cipPtr);
	return 0;
	
}

