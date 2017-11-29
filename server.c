/* ---------------------------------------------------------------- */
// CAB403: Server (Using pthreads implementing a threadpool)
/* ---------------------------------------------------------------- */
//	Work of Bennett Hardwick and Caleb Plum
/* ---------------------------------------------------------------- */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define HANGMAN_FILE "hangman_text.txt"
#define AUTH_FILE "Authentication.txt"

#define DEFAULT_PORT 12345
#define BACKLOG 4

#define MAXDATASIZE 512

#define LEADERBOARD 1

#define LOCK 1
#define UNLOCK 0

#define NUM_HANDLER_THREADS 10

#define ERROR -1

struct Entry {
	char *object;
	char *objectType;
} *entries;

struct User {
	char *username;
	char *password;
} *users;

struct LeaderBoard {
	char *username;
	int gamesWon;
	int gamesPlayed;
} *leaderBoard = NULL;

int num_requests = 0;
int totalRequests = 0;

struct Request {
	int number;
	int sockfd;
	struct Request *next;
};	

struct Request *requests = NULL;
struct Request *last_request = NULL;

typedef struct thread_socket {
	int sockfd;
	pthread_t *thread;
} thdata;

int userCount = 0, entryCount = 0, authCount = 0;
int port = DEFAULT_PORT;

int sockfd, numbytes;
struct sockaddr_in my_addr; 
struct sockaddr_in their_addr;
socklen_t sin_size;

char buf[MAXDATASIZE];
struct User currentUser;


pthread_t threads[NUM_HANDLER_THREADS];
int thread_id[NUM_HANDLER_THREADS];
int leaderboard_rc = 0;

pthread_mutex_t leaderboard_write_mutex, leaderboard_rc_mutex, leaderboard_read_mutex;
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

pthread_cond_t got_request = PTHREAD_COND_INITIALIZER;

/* ---------------------------------------------------------------- */
// Function Declarations
/* ---------------------------------------------------------------- */

// SETUP //
void loadEntries();
void loadAuthData();
void init();

// SOCKET //
void startServer();
void listenForConnection();

// GAME PLAY //
int authenticateUser(char *_buf, int new_fd, char *uname, char *pwd );
int recvAuthDataAndAuthenticate(char *_buf, int new_fd );
int gameLoop(int new_fd, char *username );

// LEADER BOARD //
int addLeaderboardEntry(char *name);
int addLossFor(char *name);
int addWinFor(char *name);

// UTIL // 
int min(int a, int b);
int max(int a, int b);
void handleInterrupt();
void freeResources();

// CLIENT SERVICES //
int hangmanLoop(int new_fd, char *username);
int leaderboardLoop(int new_fd);

// PTHREAD RUNNER //
void handleConnection(void *ptr);

// MUTEX LOCK UTIL //
void mutexInit();
void mutexRead(char a);
void mutexWrite(char a);

// THREADPOOL UTIL //
void addRequest(int sockfd, int request_num, pthread_mutex_t *p_mutex, pthread_cond_t *p_cond_var);

/* ---------------------------------------------------------------- */
// Main Loop
/* ---------------------------------------------------------------- */
int main(int argc, char *argv[]){

	signal(SIGINT, handleInterrupt);

	if (argc >= 2) {
		port = atoi(argv[1]);
	}

	init();
	startServer();

	listenForConnection();

	close(sockfd);
    freeResources();
	return 1;
}

/* ---------------------------------------------------------------- */
// Function Definitions
/* ---------------------------------------------------------------- */

// Add a request to the queue
void addRequest(int sockfd, int request_num, pthread_mutex_t *p_mutex, pthread_cond_t *p_cond_var){

	struct Request *request;

	request = malloc(sizeof(struct Request));
	request->number = request_num;
	request->sockfd = sockfd;
	request->next = NULL;

	pthread_mutex_lock(p_mutex);

	if (num_requests == 0){
		requests = request;
		last_request = request;
	} else {
		last_request->next = request;
		last_request = request;
	}

	num_requests++;

	pthread_mutex_unlock(p_mutex);
	pthread_cond_signal(p_cond_var);
}

// Get a request from the queue
struct Request *getRequest(pthread_mutex_t *p_mutex){

	struct Request *request;

	pthread_mutex_lock(p_mutex);

	if (num_requests > 0){
		request = requests;
		requests = request->next;

		if (requests == NULL){
			last_request = NULL;
		}

		num_requests--;

	} else {
		request = NULL;
	}

	pthread_mutex_unlock(p_mutex);

	return request;
}

// Function passed to threads in the threadpool
// after accepting a request. 
// Handles the gameloop for a client.
void handleRequest(struct Request *request, int thread_id){
	char username[64];
	int sockfd = request->sockfd;

	if (send(sockfd, "connected", sizeof("conected"), 0) == -1 ){
		return;
	}

	if (recvAuthDataAndAuthenticate(username, sockfd) == ERROR) return;

	if(!(strcmp(username, "_failed_") == 0)){
		if (gameLoop(sockfd, username) == ERROR) return;
	} else {

	}
}

// Loop that handles threadpool requests.
void *handleRequestLoop(void *data){
	struct Request *request;
	int thread_id = *((int *)data);

	pthread_mutex_lock(&request_mutex);

	while(1){
		if (num_requests > 0){
			request = getRequest(&request_mutex);
			if (request) {
				pthread_mutex_unlock(&request_mutex);
				handleRequest(request, thread_id);
				free(request);
				pthread_mutex_lock(&request_mutex);
			}
		} else {
			pthread_cond_wait(&got_request, &request_mutex);
		}
	}
}

// Add a win in the leaderboard 
// depending on the username.
// This is a critical section so mutex
// locks are used.
int addWinFor(char *name){

	mutexWrite(LOCK);

	for (int i = 0; i < userCount; i++){
		if(strcmp(leaderBoard[i].username, name) == 0){
			leaderBoard[i].gamesWon++;
			leaderBoard[i].gamesPlayed++;

			mutexWrite(UNLOCK);
			return 1;
		}
	}
	mutexWrite(UNLOCK);
	return 0;
}

// Add a loss in the leaderboard 
// depending on the username.
// This is a critical section so mutex
// locks are used.
int addLossFor(char *name){

	mutexWrite(LOCK);

	for (int i = 0; i < userCount; i++){
		if(strcmp(leaderBoard[i].username, name) == 0){
			leaderBoard[i].gamesPlayed++;

			mutexWrite(UNLOCK);

			return 1;
		}
	}

	mutexWrite(UNLOCK);

	return 0;
}

// Add a leaderboard entry for a username
// This is a critical section so mutex
// locks are used.
int addLeaderboardEntry(char *name){

	mutexWrite(LOCK);

	// Check to see if the user already exists in the data store
	for (int i = 0; i < userCount; i++){
		if(strcmp(leaderBoard[i].username, name) == 0){
		// they're the same

			mutexWrite(UNLOCK);

		return -1;
		}
	}

	userCount++;
	leaderBoard = realloc(leaderBoard, userCount * sizeof(struct LeaderBoard));
	leaderBoard[userCount - 1].username = malloc(strlen(name) + 1);
	strcpy(leaderBoard[userCount - 1].username, name);
	leaderBoard[userCount - 1].gamesWon = 0;
	leaderBoard[userCount - 1].gamesPlayed = 0;

	mutexWrite(UNLOCK);

	return 1; // return success
}	

// Create the POSIX threads that will serve
// the threadpool
void createThreads(){

	for (int i = 0; i < NUM_HANDLER_THREADS; i++){
		thread_id[i] = i;
		pthread_create(&threads[i], NULL, handleRequestLoop, (void *)&thread_id[i]);
	}
}

// Initialise the application.
void init(){
	srand(clock());
	loadEntries();
	loadAuthData();
	mutexInit();
	createThreads();
}

// Initialise the mutex locks
void mutexInit(){	
	leaderboard_rc = 0;
	pthread_mutex_init(&leaderboard_rc_mutex, NULL);
	pthread_mutex_init(&leaderboard_write_mutex, NULL);
	pthread_mutex_init(&leaderboard_read_mutex, NULL);
}

// Lock write while reading and unlock write if
// there are no more readers
void mutexRead(char a){
	if (a){
		pthread_mutex_lock(&leaderboard_read_mutex);
		pthread_mutex_lock(&leaderboard_rc_mutex);
		leaderboard_rc++;
		if (leaderboard_rc == 1) pthread_mutex_lock(&leaderboard_write_mutex);
		pthread_mutex_unlock(&leaderboard_rc_mutex);
		pthread_mutex_unlock(&leaderboard_read_mutex);
	} else {
		pthread_mutex_lock(&leaderboard_rc_mutex);
		leaderboard_rc--;
		if (leaderboard_rc == 0) pthread_mutex_unlock(&leaderboard_write_mutex);
		pthread_mutex_unlock(&leaderboard_rc_mutex);
	}
}

// Lock and unlock write and read mutexs
void mutexWrite(char a){
	if (a) {
		pthread_mutex_lock(&leaderboard_read_mutex);
		pthread_mutex_lock(&leaderboard_write_mutex);
	} else {
		pthread_mutex_unlock(&leaderboard_write_mutex);	
		pthread_mutex_unlock(&leaderboard_read_mutex);
	}
}

// Listen for a connection from the client, and 
// add a request to the threadpool after accepting
void listenForConnection(){
	while(1){
		sin_size = sizeof(struct sockaddr_in);

		int fd;

		if ((fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
			perror("accept");
		} else {

			printf("Server: got connection from %s\n", inet_ntoa(their_addr.sin_addr));
			addRequest(fd, totalRequests++, &request_mutex, &got_request);

		}



	}	
}

// Cleanly deallocate resources. 
void freeResources(){
	for(int i = 0; i < max(max(authCount, max(userCount, entryCount)), NUM_HANDLER_THREADS); i++ ){

		if(i < entryCount){
			free(entries[i].object);
			free(entries[i].objectType);
		}

		if (i < userCount){
			free(leaderBoard[i].username);
		}

		if (i < authCount){
			free(users[i].username);
			free(users[i].password);
		}

		if (i < NUM_HANDLER_THREADS){
			pthread_cancel(threads[i]);
		}

	};

	free(users);
	free(entries);
	free(leaderBoard);
}

// Main loop of the service. 
// Play the game, show the leaderboard
// or quit.
int gameLoop(int new_fd, char *username ) {
	while (1) {

		// Recieve instruction from the client
		if (recv(new_fd, buf, MAXDATASIZE, 0) <= 0){
			close(new_fd);
			return ERROR;
		}

		// Based on the instruction, play game, show leaderboard
		// or quit
		if (strcmp(buf, "lb-start") == 0){
			if (leaderboardLoop(new_fd) == ERROR) return ERROR;
		} else if (strcmp(buf, "hm-start") == 0){
			if (hangmanLoop(new_fd, username) == ERROR ) return ERROR;
		} else if (strcmp(buf, "") == 0){
			close(new_fd);
			return 1;
		}


	}
	//{ close(new_fd); }
}

// Play the hangman game with the client
int hangmanLoop(int new_fd, char *username ) {

	struct Entry pair = entries[rand() % entryCount];
	int guesses = min((int) strlen(pair.object) + (int) strlen(pair.objectType) + 10, 26);
	int lettersLeft = strlen(pair.object) + strlen(pair.objectType);
	char *words = malloc(strlen(pair.object) + strlen(pair.objectType) + 1);
	char _buf[MAXDATASIZE];
	char guessedLetters[26] = "\0";

	// Generate the ____ _____ string
	memset(words, '_', strlen(pair.objectType));
	memset(words + strlen(pair.objectType), ' ', 1);
	memset(words + strlen(pair.objectType) + 1, '_', strlen(pair.object));
	memset(words + strlen(pair.objectType) + 1 + strlen(pair.object), '\0', 1);

	// Send the game screen to the client
	sprintf(_buf, "%d&%s", guesses, words);
	if (send(new_fd, _buf, sizeof _buf, 0) == -1) { 
		close(new_fd); 
		free(words); 
		return ERROR;
	}

	// Play the game
	while(guesses > 0){
		if(recv(new_fd, _buf, MAXDATASIZE, 0) <= 0) { 
			close(new_fd); 
			free(words);
			return ERROR;
		}

		_buf[1] = '\0';
		
		if (!strchr(guessedLetters, _buf[0])){

			strcat(guessedLetters, &_buf[0]);

			for (int i = 0; i < strlen(pair.objectType); i++){
				if (_buf[0] == pair.objectType[i]){
					memset(words + i, _buf[0], 1);
					lettersLeft--;
				}
			}

			for (int i = 0; i < strlen(pair.object); i++) {
				if (_buf[0] == pair.object[i]){
					memset(words + i + 1 + strlen(pair.objectType), _buf[0], 1);
					lettersLeft--;
				}
			}
		}

		guesses--;

		// If any of the 'finished' criteria are met, send either a loss or a win
		// else send the word to the client and keep playing
		if (lettersLeft <= 0){
			if (send(new_fd, "hm-win", sizeof("hm-win"), 0) == -1) { 
				close(new_fd); 
				free(words);
				return ERROR;
			}

			if(recv(new_fd, _buf, MAXDATASIZE, 0) <= 0) { 
				close(new_fd); 
				free(words);
				return ERROR;
			}

			sprintf(_buf, "%s %s", pair.objectType, pair.object);

			if (send(new_fd, _buf, sizeof(_buf), 0) == -1) {
				close(new_fd); 
				free(words);
				return ERROR;
			}

			addWinFor(username);
			break;
		} else if (guesses > 0){
			sprintf(_buf, "%d&%s", guesses, words);
			if (send(new_fd, _buf, sizeof _buf, 0) == -1) { 
				close(new_fd); 
				free(words);
				return ERROR;
			}
		} else if (guesses <= 0) {
			if (send(new_fd, "hm-loss", sizeof("hm-loss"), 0) == -1) { 
				close(new_fd); 
				free(words);
				return ERROR;
			}
			addLossFor(username);
			break;
		} 
	}

	// Free the dynamically allocated data
	free(words);
	return 1;

}

// Send the leaderboard to the client.
int leaderboardLoop(int new_fd){

	for (int i = 0; i < userCount; i++){
		sprintf(buf, "%s&%d&%d", leaderBoard[i].username, leaderBoard[i].gamesPlayed, leaderBoard[i].gamesWon);

		if (send(new_fd, buf, sizeof buf, 0) == -1) { 
			close(new_fd); 
			return ERROR;
		}
	}

	if (send(new_fd, "lb-end", sizeof("lb-end"), 0) == -1) { 
		close(new_fd); 
		return ERROR;
	}

	return 1;
}

void startServer() {

	// Create the socket
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	/* generate the end point */
	my_addr.sin_family = AF_INET;         /* host byte order */
	my_addr.sin_port = htons(port);     /* short, network byte order */
	my_addr.sin_addr.s_addr = INADDR_ANY; /* auto-fill with my IP */
		/* bzero(&(my_addr.sin_zero), 8);   ZJL*/     /* zero the rest of the struct */

	/* bind the socket to the end point */
	if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) \
	== -1) {
		perror("bind");
		exit(1);
	}

	/* start listening */
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	printf("Server started on port %d\n", port);
}

// Load the hangman file and correctly tokenise the entries
void loadEntries(){
	FILE *fp;
	char buff[128];
	fp = fopen(HANGMAN_FILE, "r");

	entryCount = 1;
	int ch = 0;

	while((ch = fgetc(fp)) != EOF){
		if (ch == '\n') entryCount++;
	}

	rewind(fp);

	entries = malloc(entryCount * sizeof(struct Entry));

	for (int i = 0; i < entryCount; i++){

		char object[64];
		char objectType[64];

		fscanf(fp, "%s", buff);
		strcpy(object, strtok(buff, ","));
		strcpy(objectType, strtok(NULL, ","));

		entries[i].object = malloc(strlen(object) + 1);
		entries[i].objectType = malloc(strlen(objectType) + 1);

		strcpy(entries[i].object, object);
		strcpy(entries[i].objectType, objectType);

	}

	fclose(fp);
}

// Load the authentication file for authenticating users
void loadAuthData(){
	FILE *fp;

	// Open a stream to the file
	fp = fopen(AUTH_FILE, "r");

	authCount = 1;
	int ch = 0;

	while((ch = fgetc(fp)) != EOF){
		if (ch == '\n') authCount++;
	}

	rewind(fp);

	users = malloc(authCount * sizeof(struct User));

	for (int i = 0; i < authCount; i++){

		char username[64], password[64];

		fscanf(fp, "%s", username);
		fscanf(fp, "%s", password);

		users[i].username = malloc(strlen(username) + 1);
		users[i].password = malloc(strlen(password) + 1);

		strcpy(users[i].username, username);
		strcpy(users[i].password, password);

	}

	fclose(fp);
}

// Recv auth data from the client and try to authenticate
int recvAuthDataAndAuthenticate(char *_buf, int new_fd) {
	
	char buf[MAXDATASIZE];

	if (recv(new_fd, buf, MAXDATASIZE, 0) <= 0) { 
		close(new_fd); 
		return -1;
	}

	return authenticateUser(_buf, new_fd, strtok(NULL, ""), strtok(buf, "&"));
}

// Authenticate the user
int authenticateUser(char *_buf, int new_fd, char *pwd, char *uname){


	for (int i = 1; i < authCount; i++){

		if (strcmp(users[i].username, uname) == 0){
			if (strcmp(users[i].password, pwd) == 0){
				addLeaderboardEntry(users[i].username);
				if (send(new_fd, "success", sizeof("success"), 0) == -1) { 
					close(new_fd);
					return ERROR; 
				}

				strcpy(_buf, users[i].username);
				return 1; 

			} else {
				if (send(new_fd, "failed", sizeof("failed"), 0) == -1) { 
					close(new_fd);
					return ERROR; 
				}
				strcpy(_buf, "_failed_");
			}
		}
	}
	if (send(new_fd, "failed", sizeof("failed"), 0) == -1) { 
		close(new_fd);
		return -1; 
	}
	strcpy(_buf, "_failed_");
	return -1;
}

// Find the smaller of two numbers
int min(int a, int b){
	return (a < b ? a : b);
}

// Find the larger of two numbers
int max(int a, int b){
	return (a > b ? a : b);
}

// Handle a SIGINT (Ctrl - C) interrupt. Free memory and close
// connections so the port isn't bound.
void handleInterrupt(){
	printf("\n\nInterrupt recieved. Closing connection.\n\n");
	freeResources();
	close(sockfd);
	printf("Memory successfully free'd and socket closed... Exiting.\n");
	exit(1);
}
