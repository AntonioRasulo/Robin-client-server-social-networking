#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

#define CIP_LENGTH 280
#define BUF_SIZE 1024
#define NUM_CHAR_FOLLOW 6

struct sockaddr_in s_addr;
int fd, ret, s_port,dim;
char msg[BUF_SIZE];

int receive_print_send()
{
	//Si riceve la dimensione del messaggio 
	//recv() viene usata per ricevere dati da un socket
	//restituisce il numero di byte ricevuti; -1 in caso di errore
	//si riceve la dimensione del messaggio che arriva dal server
	ret = recv(fd, (void*) &dim, sizeof(dim),MSG_WAITALL);
	if(ret < 0 || ret < sizeof(dim))
	{
		perror("recv(dim)");
		return 0;
	}
			
	//Si converte in un formato di rete
	dim = ntohl(dim);
				
	//msg viene "pulito"
	memset(msg,0,sizeof(msg));
		
	//Si riceve il messaggio dal server
	ret = recv(fd,(void*) msg, dim, MSG_WAITALL);
	if(ret <0 || ret < dim)
	{
		perror("recv(msg)");
		return 0;
	}
				
	printf("%s\n",msg);
	fgets(msg, sizeof(msg), stdin);
				
	//dim contiene il numero di caratteri del msg nel formato di rete
	dim = htonl(strlen(msg)-1);
				
	//si invia la dimensione del messaggio 
	ret = send(fd, (void*) &dim, sizeof(dim), 0);
	if(ret < 0 || ret < sizeof(dim))
	{
		perror("send(dim)");
		return 0;
	}
			
	//si invia il messaggio contenente la email
	ret = send(fd, (void*) &msg, strlen(msg)-1, 0);
	if(ret < 0 || ret < strlen(msg)-1)
	{
		perror("send(msg)");
		return 0;
	}
	
	return 1;
	
}

int send_cmd()
{
	//dim contiene il numero di caratteri del comando nel formato di rete
	dim = htonl(strlen(msg)-1);
			
	//send() viene usata per inviare dati attraverso il socket
	// restituisce il numero di caratteri spediti; -1 in caso di errore
	//si invia la dimensione del comando
	ret = send(fd, (void*) &dim, sizeof(dim), 0);
	if(ret < 0 || ret < sizeof(dim))
	{
		perror("send(dim)");
		return 0;
	}
			
	//si invia il comando
	ret = send(fd, (void*) msg, strlen(msg)-1, 0);
	if(ret < 0 || ret < strlen(msg)-1)
	{
		perror("send(msg)");
		return 0;
	}
	
	return 1;
	
}

int main(int argc, char* argv[])
{
	
	int login = 0;
	
	if(argc < 3)
	{
		printf("usage: ./client <server_IP> <port>");
		exit(-1);
	}
	
	//atoi traduce una stringa in un intero
	s_port = atoi (argv[2]);	
	
	//La primitiva socket() crea un socket. Restituisce un file descriptor.
	//Famiglia di protocolli usata: AF_INET -->  IPv4
	//Tipo di comunicazione che si vuole usare: SOCK_STREAM --> TCP
	fd = socket(AF_INET, SOCK_STREAM, 0);
	
	//La struttura s_addr viene azzerata
	memset(&s_addr, 0, sizeof(s_addr));
	
	//Famiglia della struttura dati per il socket
	s_addr.sin_family = AF_INET;
	
	//Porta della struttura dati per il socket
	//htons converte s_port in un formato di rete (big endian) così che non sia
	//dipendente dal formato usato dal calcolatore
	s_addr.sin_port = htons(s_port);
	
	//inet_pton consente di passare da un formato presentation ad un formato numeric
	//di un indirizzo IP
	inet_pton(AF_INET, argv[1], &s_addr.sin_addr);
	
	//connect() viene utilizzata dal client per creare una connessione con il server
	//deve essere specificato il socket con cui avviene la trasmissione
	//s_addr contiene il nome del socket remoto a cui connettersi
	// in caso di errore restituisce -1
	if(connect(fd, (struct sockaddr*) &s_addr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("Connect()");
		exit(-1);
	}
	
	while(strcmp(msg,"quit\n") != 0)
	{				
		//msg viene pulito
		memset (msg, 0, sizeof(msg));
			
		/*Si leggono caratteri da stdin e si memorizzano
		 in msg finchè non si incontra newline
		oppure finchè non vengono letti sizeof(msg) -1 B*/
		fgets(msg, sizeof(msg), stdin);
						
		//se è stato eseguito il comando "help"
		if(strcmp(msg, "help\n") == 0)
		{
			//Invio del comando
			if(send_cmd() == 0 )
			{
				perror("send_cmd()");
				exit(-1);
			};
			
			//Ricezione della dimensione del messaggio
			//si riceve la dimensione del msg contenente i comandi disponibili
			ret = recv(fd, (void*) &dim, sizeof(dim),MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(dim)");
				exit(-1);
			}
			
			//Si converte in un formato di rete
			dim = ntohl(dim);
			
			//Ricezione del messaggio: Adesso segui tizio/Operazione fallita					
			//Si riceve il risultato dal server
			ret = recv(fd,(void*) &msg, dim, MSG_WAITALL);
			if(ret <0 || ret < dim)
			{
				perror("recv(result)");
				exit(-1);
			}
				
			printf("%s",msg);
		}
		else if(strcmp(msg, "cip\n") == 0 && login)
		{
			//Invio del comando
			if(send_cmd() == 0 )
			{
				perror("send_cmd()");
				exit(-1);
			};
			
			//Inserimento e invio cip
			if(receive_print_send() == 0)
			{
				printf("Errore\n");
				exit(-1);
			};
			
			//Ricezione della dimensione del messaggio
			//si riceve la dimensione del msg contenente i comandi disponibili
			ret = recv(fd, (void*) &dim, sizeof(dim),MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(dim)");
				exit(-1);
			}
			
			//Si converte in un formato di rete
			dim = ntohl(dim);
			
			//msg viene pulito
			memset (msg, 0, sizeof(msg));
			
			//Ricezione del messaggio: Adesso segui tizio/Operazione fallita					
			//Si riceve il risultato dal server
			ret = recv(fd,(void*) &msg, dim, MSG_WAITALL);
			if(ret <0 || ret < dim)
			{
				perror("recv(result)");
				exit(-1);
			}
				
			printf("%s",msg);
			
		}
		else if((strcmp(msg, "register\n") == 0 || strcmp(msg, "login\n") == 0) && login == 0)	//è stato eseguito il comando register oppure il comando di login
		{
			
			if(strcmp(msg, "login\n") == 0)
				login = 1;
			
			//Invio del comando
			if(send_cmd() == 0 )
			{
				perror("send_cmd()");
				exit(-1);
			};
				
			//Inserimento e invio nome utente
			if(receive_print_send() == 0)
			{
				printf("Errore\n");
				exit(-1);
			};
				
			//Inserimento e invio password
			if(receive_print_send() == 0)
			{
				printf("Errore\n");
				exit(-1);
			};
				
			//si riceve la dimensione del msg contenente il risultato della registrazione/login
			ret = recv(fd, (void*) &dim, sizeof(dim),MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(dim)");
				exit(-1);
			}
			
			//Si converte in un formato di rete
			dim = ntohl(dim);
						
			//Si riceve il risultato dal server
			ret = recv(fd,(void*) &msg, dim, MSG_WAITALL);
			if(ret <0 || ret < dim)
			{
				perror("recv(result)");
				exit(-1);
			}
				
			printf("%s\n",msg);
				
		}
		else if(strcmp(msg, "quit\n") == 0)	
		{
			//Invio del comando
			if(send_cmd() == 0 )
			{
				perror("send_cmd()");
				exit(-1);
			};
		}
		else if((strncmp(msg, "follow ", NUM_CHAR_FOLLOW + 1 ) == 0) && login == 1)
		{
			//Invio del comando
			if(send_cmd() == 0 )
			{
				perror("send_cmd()");
				exit(-1);
			};
			
			//Ricezione della dimensione del messaggio
			//si riceve la dimensione del msg contenente il risultato del follow
			ret = recv(fd, (void*) &dim, sizeof(dim),MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(dim)");
				exit(-1);
			}
			
			//Si converte in un formato di rete
			dim = ntohl(dim);
			
			//Ricezione del messaggio: Adesso segui tizio/Operazione fallita					
			//Si riceve il risultato dal server
			ret = recv(fd,(void*) &msg, dim, MSG_WAITALL);
			if(ret <0 || ret < dim)
			{
				perror("recv(result)");
				exit(-1);
			}
				
			printf("%s\n",msg);
					
		}
		else if((strcmp(msg, "home\n") == 0) && login==1)
		{
			//Invio del comando
			if(send_cmd() == 0 )
			{
				perror("send_cmd()");
				exit(-1);
			};
			
			//Ricezione della dimensione del messaggio
			//si riceve la dimensione del msg contenente il risultato del follow
			ret = recv(fd, (void*) &dim, sizeof(dim),MSG_WAITALL);
			if(ret < 0 || ret < sizeof(dim))
			{
				perror("recv(dim)");
				exit(-1);
			}
			
			//Si converte in un formato di rete
			dim = ntohl(dim);
			
			//Ricezione del messaggio: Adesso segui tizio/Operazione fallita					
			//Si riceve il risultato dal server
			ret = recv(fd,(void*) &msg, dim, MSG_WAITALL);
			if(ret <0 || ret < dim)
			{
				perror("recv(result)");
				exit(-1);
			}
				
			printf("%s",msg);
			
		}
		
	}
	
	//Ricezione della dimensione del messaggio
	//si riceve la dimensione del msg contenente il risultato del follow
	ret = recv(fd, (void*) &dim, sizeof(dim),MSG_WAITALL);
	if(ret < 0 || ret < sizeof(dim))
	{
		perror("recv(dim)");
		exit(-1);
	}
			
	//Si converte in un formato di rete
	dim = ntohl(dim);
	
	//msg viene "pulito"
	memset(msg,0,sizeof(msg));
	
	//Ricezione del messaggio di uscita dal sistema
	ret = recv(fd,(void*) &msg, dim, MSG_WAITALL);
	if(ret <0 || ret < dim)
	{
		perror("recv(result)");
		exit(-1);
	}
				
	printf("%s",msg);
	
	close(fd);
	return 0;
	
}