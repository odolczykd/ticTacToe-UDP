/*
    SIECI KOMPUTEROWE 2021/22 -- grupa LF
    Projekt III: Gra UDP w "Kolko i krzyzyk"
    Autor: Dawid Odolczyk

    UWAGI:
    * ulubiona liczba n = 6101
      (sluzy ona jako numer portu, na ktorym komunikaja sie uruchomione programy)
*/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<signal.h>
#include<sys/ipc.h>
#include<sys/shm.h>

#define NICK_SIZE 32
#define MODE_SIZE 16

/* Struktura z podstawowymi informacjami o rozgrywce */
struct gameSet{
    char nickname[NICK_SIZE];       /* nick uzytkownika */
    char mode[MODE_SIZE];           /* tryb/polecenie (<poczatek>, <wynik>, <koniec>, a-i) */
    char field[10];                 /* plansza do gry w "Kolko i krzyzyk" */
    int  points[2];                 /* points[0] = punkty gracza 1, points[1] = punkty gracza 2 */
    int  whoseMove;		            /* 1 = twoj ruch, 2 = ruch przeciwnika */
    char symbol;		            /* symbol uzywany podczas gry (O lub X) */
    int  isGameInProgress;          /* czy gra sie toczy? (:= czy przeciwnik dolaczyl i czy jeszcze nie opuscil gry) */
};

/* Funkcja sprawdzajaca czy wyslano wiadomosc zwiazana z ruchem w grze */
int isMoveMode(char *mode){
    char m = mode[0];
    return ((m>='a' && m<='i') ? 1 : 0);
}

/* Funkcja sprawdzajaca czy ruch jest poprawny */
int isRightMove(char *f, char move){
    return ((f[move-'a'] == 'O' || f[move-'a'] == 'X') ? 0 : 1);
}

/* Funkcja sprawdzajaca wygrana */
int checkForWin(char *f, char mySymbol){
    /* 1 = wygrana, 0 = remis, -1 = nie do rozstrzygniecia */
    /* Sprawdzamy po wierszach */
         if(f[0] == mySymbol && f[0] == f[1] && f[1] == f[2]) return 1;
    else if(f[3] == mySymbol && f[3] == f[4] && f[4] == f[5]) return 1;
    else if(f[6] == mySymbol && f[6] == f[7] && f[7] == f[8]) return 1;
    /* Sprawdzamy po kolumnach */
    else if(f[0] == mySymbol && f[0] == f[3] && f[3] == f[6]) return 1;
    else if(f[1] == mySymbol && f[1] == f[4] && f[4] == f[7]) return 1;
    else if(f[2] == mySymbol && f[2] == f[5] && f[5] == f[8]) return 1;
    /* Sprawdzamy na przekatnych */
    else if(f[0] == mySymbol && f[0] == f[4] && f[4] == f[8]) return 1;
    else if(f[2] == mySymbol && f[2] == f[4] && f[4] == f[6]) return 1;
    /* Sprawdzamy okolicznosci remisu */
    else{
        int i;
        int isOX = 0;
        for(i=0; i<9; i++){
            if(f[i] =='O' || f[i]=='X'){
                isOX++;
            }
        }
        /* Jesli wszystkie pola na planszy sa O lub X i nie stwierdzono wygranej, to zwroc 0 (remis).
           W przeciwnym razie zwroc -1, bo zaden z warunkow wygranej lub remisu nie zostal spelniony. */
        return (isOX==9 ? 0 : -1);
    }
}

int main(int argc, char **argv){

    struct gameSet player1, player2, *shared;       /* player1 = ty, player2 = przeciwnik, shared = do pamieci dzielonej */
    struct sockaddr_in myAddr, oppAddr, *dest;		/* struktury adresowe */
    struct addrinfo *opponentInfo;			        /* informacje o przeciwniku */
    key_t shmkey;                                   /* klucz IPC */
    int shmid;                                      /* ID pamieci dzielonej */
    socklen_t structSize;
    int i, sockfd, child, whichField;

    /* Obsluga bledu podania niepoprawnej liczby argumentow */
    if(argc<2 || argc>3){
        printf("BLAD! Podano niewlasciwa liczbe argumentow!\n");
        printf("Sprobuj ponownie zgodnie z ponizszym schematem:\n\n");
        printf("%s adres_maszyny [nick]\n", argv[0]);
        return 1;
    }

    /* Zapisz nick gracza do struktury */
    if(argc == 3){
        if(strlen(argv[2]) > NICK_SIZE){
            printf("BLAD! Twoj nick jest za dlugi!\n");
            printf("Sprobuj ponownie z nickiem o maksymalnej dlugosci %d znakow.\n", NICK_SIZE);
        }
        else strcpy(player1.nickname, argv[2]);
    }
    else strcpy(player1.nickname, "NN");

    /* Pobierz informacje o drugim graczu */
    if(getaddrinfo(argv[1], "6101", NULL, &opponentInfo) != 0){
        printf("BLAD! getaddrinfo(): Nie udalo sie polaczyc z maszyna %s.\n", argv[1]);
        return 1;
    }

    /* Utworz gniazdo UDP */
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        printf("BLAD! socket(): Nie udalo sie utworzyc gniazda UDP.\n");
        return 1;
    }

    /* Uzupelnij struktury myAddr i oppAddr o potrzebne informacje */
    
    myAddr.sin_family = AF_INET;
    myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myAddr.sin_port = htons(6101);

    dest = (struct sockaddr_in *)(opponentInfo->ai_addr);
    oppAddr.sin_family = AF_INET;
    oppAddr.sin_addr = dest->sin_addr;
    oppAddr.sin_port = htons(6101);
    structSize = sizeof(oppAddr);

    /* Powiaz gniazdo z adresem maszyny */
    if(bind(sockfd, (struct sockaddr *)&myAddr, sizeof(myAddr)) < 0){
        printf("BLAD! bind(): Nie udalo sie powiazac gniazda z adresem Twojej maszyny.\n");
        close(sockfd);
        return 1;
    }

    /* Wszystko gotowe, mozna przygotowywac sie do rozpoczecia gry */
    printf("Rozpoczynam gre z %s.\n", inet_ntoa(dest->sin_addr));
    printf("* Napisz <koniec>, aby zakonczyc\n");
    printf("* Napisz <wynik>, aby wystwietlic aktualny wynik gry\n\n");
    printf("[Propozycja gry wyslana.]\n");

    /* Wyslij informacje o dolaczeniu do gry (tj. mode = <poczatek>) + ustaw wartosci poczatkowe */
    strcpy(player1.mode, "<poczatek>");
    player1.whoseMove = 2;      /* zaczyna przeciwnik... */
    player1.symbol = 'O';       /* ...z symbolem O */
    if(sendto(sockfd, &player1, sizeof(player1), 0, (struct sockaddr *)&oppAddr, sizeof(oppAddr)) < 0){
        printf("BLAD! sendto(): Nie udalo sie wyslac danych.\n");
        close(sockfd);
        return 1;
    }

    /* Utworz pamiec wspolna dla obu graczy do przechowywania planszy i wyniku punktowego rozgrywki */

    /* Stworz klucz IPC */
    if((shmkey = ftok(argv[0], 1)) == -1){
        printf("BLAD! ftok(): Nie udalo sie utworzyc klucza IPC.\n");
        close(sockfd);
        return 1;
    }

    /* Utworz pamiec wspolna */
    if((shmid = shmget(shmkey, sizeof(struct gameSet), 0666 | IPC_CREAT | IPC_EXCL)) == -1){
        printf("BLAD! shmget(): Nie udalo sie utworzyc pamieci wspolnej.\n");
        close(sockfd);
        return 1;
    }

    /* Dolacz pamiec wspolna */
    shared = (struct gameSet *) shmat(shmid, (void *)0, 0);
    if(shared == (struct gameSet *) -1){
        printf("BLAD! shmat(): Nie udalo sie dolaczyc pamieci wspolnej.\n");
        shmctl(shmid, IPC_RMID, 0);
        close(sockfd);
        return 1;
    }

    /* Ustaw wartosci poczatkowe */
    strcpy(player1.field, "abcdefghi");
    strcpy(player2.field, "abcdefghi");
    strcpy(shared->field, "abcdefghi");
    shared->isGameInProgress = 0;
    shared->symbol = 'O';
    shared->whoseMove = 2;

    /*
        Stworz duplikat procesu:
        * proces potomny odpowiada za odbieranie danych od przeciwnika i ich wyswietlanie
        * proces macierzysty odpowiada za wysylanie danych do przeciwnika i glowny przebieg rozgrywki
    */

    /* Proces potomny */
    if((child=fork()) == 0){

        while(1){
            
            /* Zaktualizuj plansze */
            strcpy(player1.field, shared->field);

            /* Odbierz dane od przeciwnika */
            if(recvfrom(sockfd, &player2, sizeof(player2), 0, (struct sockaddr *)&oppAddr, &structSize) < 0){
                printf("BLAD! recvfrom(): Nie udalo sie odebrac danych.\n");
                shmdt(shared);
                shmctl(shmid, IPC_RMID, 0);
                close(sockfd);
                return 1;
            }
            player1.whoseMove = shared->whoseMove = 1;
            strcpy(shared->nickname, player2.nickname);

            /* Dolaczenie gracza do rozgrywki */
            if(strcmp(player2.mode, "<poczatek>") == 0){
                printf("\n[%s (%s) dolaczyl do gry]\n", player2.nickname, inet_ntoa(oppAddr.sin_addr));
                /* Ustaw wartosci poczatkowe */
                shared->isGameInProgress = 1;
                shared->symbol = player1.symbol = 'X';
                shared->points[0] = 0;
                shared->points[1] = 0;
                strcpy(shared->field, "abcdefghi");
                shared->whoseMove = player1.whoseMove = 1;

                /* Wyswietl plansze */
                printf("\n");
                for(i=0; i<9; i++){
                    printf("%c ", shared->field[i]);
                    if((i+1)%3 == 0) printf("\n");
                }
                printf("[Wybierz pole] ");
                fflush(stdout);
            }

            /* Komunikat o opuszczeniu gry przez przeciwnika */
            else if(strcmp(player2.mode, "<koniec>") == 0){
                printf("\n[%s (%s) opuscil gre. Oczekiwanie na kolejnego gracza...]\n", player2.nickname, inet_ntoa(oppAddr.sin_addr));
                shared->isGameInProgress = 0;
            }

            /* Komunikat o przegranej (:= zwyciestwie przeciwnika) + reset */
            else if(strcmp(player2.mode, "<wygrana>") == 0){
                
                printf("\n[Przegrana! Sprobuj jeszcze raz]\n");

                shared->points[1]++;

                /* Reset planszy */
                player1.whoseMove = player2.whoseMove = 1;
                strcpy(player1.field, "abcdefghi");
                strcpy(player2.field, "abcdefghi");
                strcpy(shared->field, "abcdefghi");

                /* Wyswietl plansze */
                printf("\n");
                for(i=0; i<9; i++){
                    printf("%c ", shared->field[i]);
                    if((i+1)%3 == 0) printf("\n");
                }
                printf("[Wybierz pole] ");
                fflush(stdout);
            }

            /* Komunikat o remisie + reset */
            else if(strcmp(player2.mode, "<remis>") == 0){

                printf("\n[Remis! Zagraj jeszcze raz]\n");

                shared->points[0]++;
                shared->points[1]++;

                /* Reset planszy */
                strcpy(player1.field, "abcdefghi");
                strcpy(player2.field, "abcdefghi");
                strcpy(shared->field, "abcdefghi");

                /* Wyswietl plansze */
                printf("\n");
                for(i=0; i<9; i++){
                    printf("%c ", shared->field[i]);
                    if((i+1)%3 == 0) printf("\n");
                }
                printf("[Wybierz pole] ");
                fflush(stdout);
            }

            /* W przeciwnym razie poinformuj o ruchu przeciwnika i wyswietl plansze */
            else if(strcmp(player2.mode, "<wynik>") != 0){
                
                printf("\n[%s (%s) wybral pole %c]\n", player2.nickname, inet_ntoa(oppAddr.sin_addr), player2.mode[0]);

                strcpy(player1.field, player2.field);
                strcpy(shared->field, player1.field);
                shared->isGameInProgress = 1;

                /* Wyswietl plansze */
                printf("\n");
                for(i=0; i<9; i++){
                    printf("%c ", shared->field[i]);
                    if((i+1)%3 == 0) printf("\n");
                }
                printf("[Wybierz pole] ");
                fflush(stdout);
            }
            
        }
    }

    /* Proces macierzysty */
    else if(child > 0){

        while(1){
            
            /* Odczytanie trybu */
            scanf("%s", player1.mode);

            /* Tryb wpisania symbolu na plansze */
            if(isMoveMode(player1.mode)){
                if(shared->whoseMove == 1){                             /* jesli jest twoj ruch... */
                    strcpy(player1.field, shared->field);
                    whichField = player1.mode[0] - 'a';
                    if(isRightMove(shared->field, player1.mode[0])){    /* ...i jest poprawny... */
                        player1.field[whichField] = shared->symbol;     /* ...wprowadz go na plansze */
                    }
                    else{
                        printf("\n[Nieprawidlowy ruch! Wybierz inne pole] ");
                        continue;
                    }
                    strcpy(shared->field, player1.field);
                    shared->whoseMove = player1.whoseMove = 2;          /* przekaz ruch przeciwnikowi */
                    printf("\n");
                    for(i=0; i<9; i++){                                 /* wyswietl plansze */
                        printf("%c ", shared->field[i]);
                        if((i+1)%3 == 0) printf("\n");
                    }

                    if(checkForWin(shared->field, shared->symbol) == 1){        /* jesli mozna stwierdzic wygrana... */
                        printf("[Wygrana! Zagraj jeszcze raz]\n");
                        shared->points[0]++;                                    /* ...przyznaj punkty... */
                        strcpy(player1.mode, "<wygrana>");                      /* ...i wyslij informacje przeciwnikowi */
                    }
                    
                    else if(checkForWin(shared->field, shared->symbol) == 0){   /* jesli jest remis... */
                        printf("[Remis! Zagraj jeszcze raz]\n");
                        shared->points[0]++;                                    /* ...przyznaj punkty... */
                        shared->points[1]++;
                        strcpy(player1.mode, "<remis>");                        /* ...i wyslij informacje przeciwnikowi */
                    }
                }
                else{
                    printf("\n[Trwa tura przeciwnika. Poczekaj na swoja kolej]\n");
                    continue;
                }
            }

            /* Wyslij dane do przeciwnika */
            if(sendto(sockfd, &player1, sizeof(player1), 0, (struct sockaddr *)&oppAddr, sizeof(oppAddr)) < 0){
                printf("BLAD! sendto(): Nie udalo sie wyslac danych.\n");
                shmdt(shared);
                shmctl(shmid, IPC_RMID, 0);
                close(sockfd);
                return 1;
            }

            /* Wyswietl wynik rozgrywki */
            if(strcmp(player1.mode, "<wynik>") == 0){
                if(shared->isGameInProgress){
                    printf("\nTy %d : %d %s\n", shared->points[0], shared->points[1], shared->nickname);
                }
                else{
                    printf("\n[Przeciwnik jeszcze nie dolaczyl do gry.]\n");
                }
            }

            /* Obsluga zakonczenia programu */
            if(strcmp(player1.mode, "<koniec>") == 0){
                if(kill(child, SIGINT) < 0){
					printf("BLAD! kill(): Nie zamknieto potomka.\n");
                    shmdt(shared);
                    shmctl(shmid, IPC_RMID, 0);
					close(sockfd);
					return 1;
				}
                shmdt(shared);
                shmctl(shmid, IPC_RMID, 0);
				close(sockfd);
				return 0;
            }

        }
    }

    /* Obsluga bledu funkcji fork() */
    else{
        printf("BLAD! fork(): Nie udalo sie utworzyc procesu potomnego.\n");
        shmdt(shared);
        shmctl(shmid, IPC_RMID, 0);
        close(sockfd);
        return 1;
    }

    return 0;
}