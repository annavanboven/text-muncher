#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>

int initialize(int bufferSize, char *fileName);
void *reader(void *args);
void *measurer(void *args);
void *numberer(void *args);
void *printer(void *args);
int testPrint(char **buffer, int bufferSize);
int initialize(int bufferSize, char *fileName);
char **threadManagement(FILE *file, char **buffer, int bufferSize);
char *rightShift(char *str, int counter);

/**
* The muncher program takes in a file and prints it line by line with its size and line number.
* It does this by spinning four threads: The reader reads in each line,
* the measurer measures the length of each line and appends it to the end,
* the numberer appends the line number to the beginning of the line,
* the printer prints the finished line and frees it from the buffer.
* Only one thread has access to each location in the buffer at a time, which requires the use of
* conditionals and mutexes.
* I worked with Danielle Dolan on this project.
*/

pthread_cond_t readerCond = PTHREAD_COND_INITIALIZER; //conditional for the reader thread
pthread_cond_t measureCond = PTHREAD_COND_INITIALIZER; //conditional for the measurer thread
pthread_cond_t numberCond = PTHREAD_COND_INITIALIZER; //conditional for numberer thread
pthread_cond_t printerCond = PTHREAD_COND_INITIALIZER; //conditional for conditional thread


//The params struct holds the shared parameters that need to be passed to each of
// the four threads.
typedef struct{
	int bufferSize; //size of the buffer
	char **buffer; //the array that will hold the lines of the file 
	FILE *file; //file to search through
	int readCounter; //number of lines reader has completed
	int measCounter; //number of lines measurer has completed
	int numCounter; //number of lines numberer has completed
	int printCounter; //number of lines printer cas completed
	int rend; //1 if reader hits end of file
	int mend; //1 if measurer measures last line
	int nend; //1 if numberer measures last line
	int pend; //1 if printer prints last line
	pthread_mutex_t **bufferMuts; //holds array of mutexes, one for each line of buffer
}params;


// The reader loops through a file and reads each line as a string into a position on the buffer.
// It must wait until there is an empty position.
void *reader(void *args){
	//grab the arguments 
	params *param = (params *)args;
	int bufferSize = param->bufferSize;
	char **buffer = param->buffer;
	FILE *file = param->file;
	
	size_t len = 0;
	param->readCounter = 0;
	//initialize every spot in the buffer
	for(int i = 0; i<bufferSize; i++){
		buffer[i] = NULL;
	}
	
	ssize_t read = 0;
	//go until you read end of the file
	while(read!=-1){
			//secure access to buffer
			pthread_mutex_lock(param->bufferMuts[param->readCounter%bufferSize]);
			//while something else is in the spot you want, wait
			while(buffer[param->readCounter%bufferSize]!=NULL){
				pthread_cond_wait(&readerCond,param->bufferMuts[param->readCounter%bufferSize]);
			}
			//read line into the buffer at the correct spot (this allocates space)
			read=getline(&buffer[param->readCounter%bufferSize],&len,file);

			//if you hit the end of the file, signal the measurer and release your
			//hold on the buffer.
			if(read==-1){
				param->rend = 1;
				pthread_cond_signal(&measureCond);
				pthread_mutex_unlock(param->bufferMuts[(param->readCounter)%bufferSize]);
				continue;
			}

			//reallocate space for the additional characters to be added by numberer and measurer
			buffer[param->readCounter%bufferSize] = (char *)realloc(buffer[param->readCounter%bufferSize],(sizeof(char)*strlen(buffer[param->readCounter%bufferSize])+20));
			if(buffer[param->readCounter%bufferSize]==NULL){
				printf("space could not be reallocated.\n");
				break;
			}

			//increment counter and signal the measurer that the buffer spot opened up
			param->readCounter++;
			pthread_cond_signal(&measureCond);
			pthread_mutex_unlock(param->bufferMuts[(param->readCounter-1)%bufferSize]);
	}
	//the reader has ended, so rend = 1.
	//In case it still has hold of a buffer, release the hold.
	//terminate thread
	param->rend = 1;
	pthread_cond_signal(&measureCond);
	pthread_mutex_unlock(param->bufferMuts[param->readCounter%bufferSize]);
	pthread_exit(0);
	
	
}

//The measurer goes through the buffer and measures each line that has been read in,
//apending the length to the end of the line.
void *measurer(void *args){
	//grab arguments 
	params *param = (params *)args;
	char **buffer = param->buffer;
	int bufferSize = param->bufferSize;

	int len; //will hold the length of a string
	int endCheck=0; //1 if you hit the end of the file 
	param->measCounter = 0;
	//go while the reader is still going, then go until the measurer
	// catches up to the reader
	while(param->rend==0||param->measCounter<param->readCounter){
		//secure access to the buffer	
		pthread_mutex_lock(param->bufferMuts[param->measCounter%bufferSize]);
		//if you're caught up to the reader, wait
		while(param->measCounter>=param->readCounter){
			//if you've measured as many lines as the reader AND
			//the reader is done, then you have measured the entire file.
			if(param->rend==1){
				param->mend = 1;
				pthread_cond_signal(&numberCond);
				pthread_mutex_unlock(param->bufferMuts[param->measCounter%bufferSize]);
				//once you break out of nested while loop, break out of the next one as well
				endCheck=1;
				break;
			}
			pthread_cond_wait(&measureCond,param->bufferMuts[param->measCounter%bufferSize]);
		}
		//if you broke out of the above while loop, break out of this one as well
		if(endCheck==1){
			break;
		}
		
		//grab the length and cut off the last character if it is an enter
		len = strlen(buffer[param->measCounter%bufferSize]);
		if(buffer[param->measCounter%bufferSize][len-1]=='\n'){
			buffer[param->measCounter%bufferSize][len-1]='\0';
			len--;
		}
		//append the length to the end of the string and increment
		sprintf(&buffer[param->measCounter%bufferSize][len]," (%d)",len);
		param->measCounter++;

		//if you are about to break the while loop, measurer has ended.
		//say so while the numberer thread is still sleeping.
		if(param->rend==1&&param->measCounter==param->readCounter){
			param->mend = 1;
		}

		//signal numberer thread and release mutex hold
		pthread_cond_signal(&numberCond);
		pthread_mutex_unlock(param->bufferMuts[(param->measCounter-1)%bufferSize]);
	}
	//these conditions are needed when an empty file is passed in
	param->mend = 1;
	pthread_cond_signal(&numberCond);
	pthread_mutex_unlock(param->bufferMuts[param->measCounter%bufferSize]);
	
	//terminate thread
	pthread_exit(0);
	
}

//The numberer appends the line number to the beggining of each line that
// has already been measured.
void *numberer(void *args){
	//store arguments
	params *param = (params *)args;
	param->numCounter = 0;
	char **buffer = param->buffer;
	int bufferSize = param->bufferSize;
	int endCheck = 0;

	//go while the measurer is still measuring lines.
	// Then, go until you've caught up to measurer.
	while(param->mend==0||param->numCounter<param->measCounter){
		//secure the buffer
		pthread_mutex_lock(param->bufferMuts[param->numCounter%bufferSize]);
		//if you've caught up to the measurer, wait
		while(param->numCounter>=param->measCounter){
			//if the measurer is finished and you have the same amount of lines numbered,
			//then you are also finished.
			if(param->mend==1){
				param->nend = 1;
				//signal printer and release hold on buffer
				pthread_cond_signal(&printerCond);
				pthread_mutex_unlock(param->bufferMuts[param->numCounter%bufferSize]);
				endCheck = 1;
				break;
			}
			//go to sleep until conditional is signaled	
			pthread_cond_wait(&numberCond,param->bufferMuts[param->numCounter%bufferSize]);
		}
		if(endCheck==1){
			break;
		}
		
		//append number to beginning of string, increment counter
		buffer[param->numCounter%bufferSize] = rightShift(buffer[param->numCounter%bufferSize],param->numCounter);
		param->numCounter++;
		//if you're about to break the while loop, you've reached the end of file
		if(param->mend==1&&param->numCounter>=param->measCounter){
			param->nend=1;
		}
		//signal printer and release hold on buffer
		pthread_cond_signal(&printerCond);
		pthread_mutex_unlock(param->bufferMuts[(param->numCounter-1)%bufferSize]);
	}
	param->nend = 1;
	pthread_cond_signal(&printerCond);
	pthread_mutex_unlock(param->bufferMuts[param->numCounter%bufferSize]);
	//terminate thread
		pthread_exit(0);
	
}

//The printer prints each line in the buffer that has already been
// measured and numbered. It then frees the space and lets the reader
// know a space is open.
void *printer(void *args){
	//store arguments
	params *param = (params *)args;
	param->printCounter = 0;
	char **buffer = param->buffer;
	int bufferSize = param->bufferSize;
	int endCheck = 0;

	//go while numberer is still going, then go until
	// you catch up
	while(param->nend==0||param->printCounter<param->numCounter){
		pthread_mutex_lock(param->bufferMuts[param->printCounter%bufferSize]);

		while(param->printCounter>=param->numCounter){	
			if(param->nend==1 ){
				pthread_cond_signal(&readerCond);
				pthread_mutex_unlock(param->bufferMuts[param->printCounter%bufferSize]);
				endCheck = 1;
				break;
			}
			pthread_cond_wait(&printerCond,param->bufferMuts[param->printCounter%bufferSize]);	
		}
		if(endCheck==1){
			break;
		}

		//print string, set spot equal to null, and free the space 
		printf("%s\n",buffer[param->printCounter%bufferSize]);
		buffer[param->printCounter%bufferSize] = NULL;
		free(buffer[param->printCounter%bufferSize]);
		//increment counter and release mutex hold
		//(no need to check for pend, because nobody is waiting on it)
		param->printCounter++;
		pthread_cond_signal(&readerCond);
		pthread_mutex_unlock(param->bufferMuts[(param->printCounter-1)%bufferSize]);
	}
	//terminate thread
	param->pend = 1;
	pthread_exit(0);
	
}

//The main function takes in the input arguments: a file name and a buffer size. 
int main(int argc, char *argv[]){
	//check valid arguments
	if(argc!=3){
		printf("I'm sorry! you chose an invalid number of inputs. The file must take in two inputs, \n");
		printf("the first being the file to read and \n");
		printf("the second being the size of a buffer. \n");
		return 1;
	}

	//grab arguments
	char *fileName = argv[1];
	int bufferSize = atoi(argv[2]);

	//check valid buffer size
	if(bufferSize<=0){
		printf("I'm sorry! Buffer size must be positive. \n");
		return 1;
	}
	
	initialize(bufferSize, fileName);
}

//The initialize function allocates space for a buffer that will hold the 
//indicated amount of spots. It opens the file and passes it to threadManagement.
int initialize(int bufferSize, char *fileName){
	char **buffer = (char **)malloc(sizeof(char*)*bufferSize);
	FILE *file = fopen(fileName, "r"); //read-only
	if(file==NULL){
		printf("File not found.\n");
		return 1;
	}
	threadManagement(file,buffer,bufferSize);
	free(buffer);
	return 0;
}

//The threadManagement function constructs the arguments for the threads, spins them off,
//and destroys them. 
//I used the following link for information on initializing mutexes.
//https://stackoverflow.com/questions/5138778/static-pthreads-mutex-initialization
char **threadManagement(FILE *file, char **buffer, int bufferSize){
	//create array of threads
	pthread_t *threads = (pthread_t*)malloc(sizeof(pthread_t) * 4);

	//allocate space for arguments and initialize arguments
	params *args = (params *)malloc(sizeof(params));
	args->bufferSize = bufferSize;
	args->buffer = buffer;
	args->file = file;
	args->rend = 0;
	args->mend = 0;
	args->nend = 0;
	args->pend = 0;

	//create array to hold a mutex for each line in the buffer
	args->bufferMuts = (pthread_mutex_t**)malloc(bufferSize * sizeof(pthread_mutex_t*));
	//for each spot in the buffer, create a mutex
	for(int i = 0; i<bufferSize;i++){
		args->bufferMuts[i] = malloc(sizeof(pthread_mutex_t));
		pthread_mutex_init((args->bufferMuts[i]), NULL);
	}
	
	//spin off the threads with the same parameters
	pthread_create(&threads[0], NULL, reader, (void *)args);
	pthread_create(&threads[1], NULL, measurer, (void *)args);
	pthread_create(&threads[2], NULL, numberer, (void *)args);
	pthread_create(&threads[3], NULL, printer, (void *)args);

	//once they all come back, join them together
	for(int i = 0; i< 4; i++){
		pthread_join(threads[i],NULL);
	}
	
	//destroy conditionals and mutexes
	pthread_cond_destroy(&readerCond);
	pthread_cond_destroy(&measureCond);
	pthread_cond_destroy(&numberCond);
	pthread_cond_destroy(&printerCond);
	for(int i = 0; i<bufferSize;i++){
		pthread_mutex_destroy((args->bufferMuts[i]));
	}
	
	//free allocated space
	free(threads);
	free(args);

	return buffer;
}

//rightShift is called by numberer to add the line number to the beginning of the 
//line.
char *rightShift(char *str, int counter){
	
	int strLen = strlen(str);
	char strung[strLen];
	
	//copy non-null chars into strung
	for(int i = 0; i<strLen; i++){
		strung[i] = str[i];
	}

	//place the appended string into str
	sprintf(str,"%d: %s",counter+1,strung);

	//delete unwanted characters from the end
	int i = strlen(str)-1;
	while(str[i]!=')'){
		str[i] = '\0';
		i--;
	}
	return str;
}

//testPrint is a test method used to print all the elements in a buffer.
int testPrint(char **buffer, int bufferSize){
	for(int i = 0; i<bufferSize;i++){
		if(buffer[i%bufferSize]==NULL){
			printf("i = %d \n",i);
		}
		else{
			printf("i = %d: %s\n",i,buffer[i%bufferSize]);
		}	
	}

	return 0;
}



