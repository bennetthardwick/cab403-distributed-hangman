/* ---------------------------------------------------------------- */
// CAB403: Client
/* ---------------------------------------------------------------- */
//	Work of Bennett Hardwick and Caleb Plum
/* ---------------------------------------------------------------- */

#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <strings.h> 
#include <netdb.h> 
#include <unistd.h>
#include <signal.h>

#define MAX_USERNAME_LENGTH 16
#define MAX_PASSWORD_LENGTH 16
#define MAXDATASIZE 512

#define h_addr h_addr_list[0] // C99 compatability

char authenticateUser();
void authFailed();
void welcomeMessage();
void connectToServer();

int createLeaderboard();
int addLeaderboardEntry(char *name);

void menu();
void showMenu(int a);
void hangman();
void quit();
void leaderboard();

void handleInterrupt();

void hangmanMessage();

char username[MAX_USERNAME_LENGTH];
char password[MAX_PASSWORD_LENGTH];

int sockfd, numbytes;  
char buf[MAXDATASIZE];
struct hostent *he;
struct sockaddr_in their_addr;


/* ---------------------------------------------------------------- */
// Main
/* ---------------------------------------------------------------- */

int main(int argc, char *argv[]) {

	signal(SIGINT, handleInterrupt);

	connectToServer(&argc, argv);

	if(authenticateUser()){

		welcomeMessage();
		showMenu(0);

	} else {
		authFailed();
		exit(1);
	}

	exit(1);

	return 1;
}

void menu(){

	char input[64];

	puts("Please enter a selection:\n");
	puts("<1> Play Hangman");
	puts("<2> Show Leaderboard");
	puts("<3> Quit\n");
	printf("Enter an option (1-3): ");
	scanf("%s", input);
	input[1] = '\0';

	showMenu(atoi(input));

}

void quit(){
	puts("\n=====================================================================================");
	puts("\n");
	puts("                                 Thanks for playing!");
	puts("                                       Good-bye!");
	puts("\n");
	puts("=====================================================================================");
	close(sockfd);
	exit(1);
}

void connectToServer(int *argc, char *argv[]) {
	if (*argc != 3) {
		fprintf(stderr,"usage: client hostname port\n");
		exit(1);
	}

	if ((he=gethostbyname(argv[1])) == NULL) {  /* get the host info */
		perror("gethostbyname");
		exit(1);
	}

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	their_addr.sin_family = AF_INET;      /* host byte order */
	their_addr.sin_port = htons(atoi(argv[2]));    /* short, network byte order */
	their_addr.sin_addr = *((struct in_addr *)he->h_addr);
	bzero(&(their_addr.sin_zero), 8);     /* zero the rest of the struct */

	if (connect(sockfd, (struct sockaddr *)&their_addr, \
	sizeof(struct sockaddr)) == -1) {
		perror("connect");
		exit(1);
	}
}

void welcomeMessage() {
	puts("");
	puts("=====================================================================================");
	puts("\n");
	puts("                                    Login successful.");
	puts("                    Welcome to the online hangman gaming system.");
	puts("\n");
	puts("=====================================================================================");
	puts("");
}

void authFailed() {
	puts("=====================================================================================");
	puts("\n");
	puts("      Login failed. You have entered either an incorrect username or password");
	puts("                     You will now be disconnected from the server...");
	puts("\n");
	puts("=====================================================================================");
}

void loginMessage() {
	puts("=====================================================================================");
	puts("\n");
	hangmanMessage();
	puts("                                  Please login to play.");
	puts("\n");
	puts("=====================================================================================");
	puts("");

	printf("Enter your username: ");
	scanf("%s", username);
	printf("Enter your password: ");
}

void hangmanMessage(){
	puts(" __ __  ____ ____   ____ ___ ___  ____ ____        ___  ____  _     ____ ____    ___ ");
	puts("|  |  |/    |    \\ /    |   |   |/    |    \\      /   \\|    \\| |   |    |    \\  /  _]");
	puts("|  |  |  o  |  _  |   __| _   _ |  o  |  _  |    |     |  _  | |    |  ||  _  |/  [_ ");
	puts("|  _  |     |  |  |  |  |  \\_/  |     |  |  |    |  O  |  |  | |___ |  ||  |  |    _]");
	puts("|  |  |  _  |  |  |  |_ |   |   |  _  |  |  |    |     |  |  |     ||  ||  |  |   [_ ");
	puts("|  |  |  |  |  |  |     |   |   |  |  |  |  |    |     |  |  |     ||  ||  |  |     |");
	puts("|__|__|__|__|__|__|___,_|___|___|__|__|__|__|     \\___/|__|__|_____|____|__|__|_____|\n");
}

void showMenu(int a) {
	switch(a){
		case 1:
			hangman();
		break;
		case 2:
			leaderboard();
		break;
		case 3:
			quit();
		break;
		default:
			menu();
		break;
	}
}

void hangman(){
	
	send(sockfd, "hm-start", sizeof("hm-start"), 0);

	puts("=====================================================================================");
	puts("                                  Let's play!\n");
	hangmanMessage();

	recv(sockfd, buf, MAXDATASIZE, 0);

	char guessedLetters[26] = "\0";

	do {

		int guesses = atoi(strtok(buf, "&"));
		char *word = strtok(NULL, "&");
		char input[512];

		puts("-------------------------------------------------------------------------------------");
		printf("Guesses: %s\n\nNumber of guesses left: %d\n\nWord: %s\n\n", guessedLetters, guesses, word);
		printf("Please enter a guess (a-z): ");
		scanf("%s", input);
		input[1] = '\0';

		if (!strchr(guessedLetters, input[0])) strcat(guessedLetters, &input[0]);

		send(sockfd, input, sizeof(input), 0);
		recv(sockfd, buf, MAXDATASIZE, 0);

	} while (strcmp(buf, "hm-win") != 0 && strcmp(buf, "hm-loss") != 0);

	puts("-------------------------------------------------------------------------------------\n");
	
	char input[64];

	if (strcmp(buf, "hm-win") == 0){
		send(sockfd, "phrase", sizeof("phrase"), 0);
		recv(sockfd, buf, MAXDATASIZE, 0);
		printf("Word: %s\n\n", buf);
		printf("Congratulations! You won!\n\nWould you like to return to the menu? (y/n): ");
		scanf("%s", input);

		input[1] = '\0';

		if(strcmp(&input[0], "n") == 0){
			quit();
		} else {
			puts("\n-------------------------------------------------------------------------------------");
			showMenu(0);
		}

	} else {

		printf("Oh no! You lost!\n\nWould you like to return to the menu? (y/n): ");
		scanf("%s", input);

		if(strcmp(&input[0], "n") == 0){
			quit();
		} else {
			showMenu(0);
		}

	}
}

void checkForConnection(){
	printf("Please wait to join the Hangman Online Game Lobby.\nYou have been placed in a queue.\n");
	recv(sockfd, buf, MAXDATASIZE, 0);
}

void leaderboard(){

	char *array[100];
	int index = -1;
	send(sockfd, "lb-start", sizeof("lb-start"), 0);
	recv(sockfd, buf, MAXDATASIZE, 0);

	do {
		index++;
		array[index] = malloc(sizeof buf);
		strcpy(array[index], buf);
		recv(sockfd, buf, MAXDATASIZE, 0);

	} while(strcmp(buf, "lb-end") != 0);


	puts("\nLeaderboard:");
	puts("---------------------------------------------");
	printf("| %-5s| ", "Rank");
	printf("%-20s| ", "Name");
	printf("%-6s| ", "Plays");
	printf("%-5s|\n", "Wins");
	puts("---------------------------------------------");

	for(int i = 0; i <= index; i++){
		//printf("%s\n", array[i]);
		printf("| %-5d| ", i + 1);
		printf("%-20s| ", strtok(array[i], "&"));
		printf("%-6s| ", strtok(NULL, "&"));
		printf("%-5s|\n", strtok(NULL, "&"));
	}

	for (int i = 0; i < index; i++){
		free(array[i]);
	}

	puts("---------------------------------------------");
	showMenu(0);
}

char authenticateUser() {

	checkForConnection();
	loginMessage();

	scanf("%s", password);
	sprintf(buf, "%s&%s", username, password);
	send(sockfd, buf, sizeof buf, 0);
	recv(sockfd, buf, MAXDATASIZE, 0);

	if (strcmp(buf, "success") == 0){
		return 1;
	}	else {
		close(sockfd);
		return 0;
	}
}

void handleInterrupt() {
	if(sockfd){
		close(sockfd);
	}
	exit(1);
}
