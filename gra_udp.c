#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>


#define MAX_MSG_LEN 1024

int sockfd;
int pid, shmid;
int sendLen, recLen;
struct sockaddr_in serverAddr, clientAddr;


struct myMsg{               //struktura sluzaca jako transmiter danych
    int number;
    int score;
    char nick[32];
    char message[512];
    char client[32];
    int action;
    int randStart;
}msg;

struct myStorage{           //przechowywalnia lokalna
    char serverNick[32];
    char cliName[32];
    int sum;
    int myScore;
    int cliScore;

}*storage;

void sgnHandler(int s){
    printf("\nOdebralem Ctrl+C (SIGINT)\n");
    printf("Koncze i sprzątam\n");

    msg.action = 4;
    shmdt(storage);
    shmctl(shmid, IPC_RMID, 0);

    sendLen = sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&serverAddr, (socklen_t)sizeof(serverAddr));
    kill(pid, SIGTERM);
    exit(0);
}

int main(int argc, char *argv[])
{
    
    u_short myPort;
    socklen_t cliLen;
    int key, randInt;
    struct hostent *host;

    srand(time(NULL));

    signal(SIGINT, sgnHandler);     //obsluga sygnalu


    if(argc < 3 || argc > 4){               //sprawdzamy czy zostaly podane wymagane argumenty
        printf("Uzycie: %s <host> <port> [nick]\n", argv[0]);
        exit(1);
    }

    if (argc < 4){                          //sprawdzamy czy zostala podana nazwa
        strcpy(msg.client, argv[1]);
        strcpy(msg.nick, "###");
    }
    else{
        strcpy(msg.nick, argv[3]);
        strcpy(msg.client, argv[1]);
    }

    msg.number = 0;
    msg.score = 0;
    // Tworzymy gniazdo
    myPort = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0){
        perror("blad socket");
        exit(1);
    }
    
    key = ftok(argv[0], 1);         //Tworzymy klucz i segment pamieci wspoldzielonej
    if(key == -1){
        perror("blad ftok");
        exit(1);
    }
    if((shmid = shmget(key, sizeof(struct myStorage), 0755 | IPC_CREAT | IPC_EXCL)) == -1){
        perror("blad shmget");
        exit(1);
    }
    if((storage = (struct myStorage *)shmat(shmid, (void *)0, 0)) == NULL){
        perror("blad shmat");
        shmctl(shmid, IPC_RMID, 0);
    }
    // Tworzymy strukture adresowa
    
    storage->sum = 0;
    storage->myScore = 0;
    storage->cliScore = 0;

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(myPort);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);


    clientAddr.sin_family = AF_INET;
    host = gethostbyname(argv[1]);          //adres domenowy
    if (host == NULL) {
        perror("blad gethostbyname");
        exit(1);
    }
    clientAddr.sin_addr = *((struct in_addr *) host->h_addr);


    clientAddr.sin_port = htons(myPort);

    if(bind(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0){
        perror("blad bind ");
        exit(1);
    }
    cliLen = sizeof(clientAddr);
    
    msg.randStart = 0;
    
    if((pid = fork()) == 0){            //potomek odbiera sygnaly
        while(1){
            recLen = recvfrom(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&clientAddr, &cliLen);
            
            if(recLen != -1){
                if(strcmp(msg.nick,"###") == 0){
                    strcpy(msg.nick, inet_ntoa(clientAddr.sin_addr));
                }
                
            }
            switch(msg.action){    
                case 1:                 //rozpoczecie poloczenia
                    strcpy(storage->cliName, msg.nick);               
                    storage->sum = msg.randStart;
                    printf("Rozpoczynam gre z %s. Napisz \"koniec\" by zakonczyc, lub \"wynik\" by wyswietlic aktualny wynik\n", inet_ntoa(clientAddr.sin_addr));
                    printf("Losowa wartosc poczatkowa: %d, podaj nastepna wartosc: \n", msg.randStart);                                      
                    break;
                
                
                case 2:                 //trwanie gry
                    if(storage->sum==0){
                        storage->sum = msg.randStart;
                    }
                    storage->sum = storage->sum + msg.number;
                    printf("%s podal liczbe: %d, podaj kolejna wartosc\n", msg.nick, storage->sum); 
                    break;
                
                case 3:                 //przegrana gracza
                    printf("Przegrana\n");
                    storage->sum = msg.randStart;
                    storage->cliScore+=1;                    
                    break;
                
                case 4:                 //wyjscie gracza
                    printf("\n %s wyszedl \n", msg.nick);
                    memset(&msg, 0, sizeof(msg));
                    
                    //storage->sum = randInt;
                    printf("Oczekiwanie na przeciwnika...\n");
                    
                    break;
            }
             
        }
    }
    else{                   //rodzic wysyla syganly
        msg.action = 1;
        // storage->sum = msg.randStart;
        randInt = (rand()%10) + 1;              //przypisujemy losowa wartosc poczatkowa
        msg.randStart = randInt;
        sendLen = sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&clientAddr, (socklen_t)sizeof(serverAddr));
        
        if(sendLen == -1){
            perror("blad sendto");
            exit(1);
        }
        storage->sum+=msg.randStart;            
        printf("Propozycja gry wyslana...\n");
        while(1){  
            if(storage->sum == 0){
                randInt = (rand()%10) + 1;
                msg.randStart = randInt;
            }
            fgets(msg.message, 256, stdin);
            msg.message[strlen(msg.message) - 1] = '\0';
            if(strcmp(msg.message, "koniec") == 0){                 //wyjscie gracza
                msg.action = 4;
                sendLen =  sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&clientAddr, (socklen_t)sizeof(serverAddr));
                kill(pid, SIGTERM);
                break;
            }
            if(strcmp(msg.message, "wynik") == 0){                  //wpisanie wyniku
                printf("Ty %d : %d %s\n", storage->myScore, storage->cliScore, storage->cliName);
                continue;
            }
            else{
                msg.action = 2;                                     //dodawanie liczb
                msg.number = atoi(msg.message);
                if(msg.number < 1 || msg.number > 10){
                    printf("Podaj liczbe od 1 do 10\n");
                        continue;
                }
                if(storage->sum==0){
                    msg.randStart = randInt;
                    storage->sum = msg.randStart;
                }
                msg.score = msg.score + msg.number;
                storage->sum = storage->sum + msg.number;
                if(storage->sum >= 50){
                    printf("Wygrana!\n");
                    printf("Zaczynamy kolejną rozgrywke...\n");
                    msg.action = 3;
                    storage->myScore++;
                    storage->sum = msg.randStart;
                    printf("Losowa wartosc pocatkowa %d, podaj kolejna wartosc:\n", storage->sum);
                    
                }
                sendLen = sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&clientAddr, (socklen_t)sizeof(serverAddr));
            }
        }
    }
    close(sockfd);
    shmctl(shmid, IPC_RMID, 0);
    return 0;
}
