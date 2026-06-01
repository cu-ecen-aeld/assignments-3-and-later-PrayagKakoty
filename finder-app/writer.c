#include<stdio.h>
#include<stdlib.h>
#include<syslog.h>

int main(int argc, char *argv[]){
	openlog(argv[0], LOG_PID, LOG_USER);
	if(argc != 3){
		printf("Usage: %s <file_path> <string>\n", argv[0]);
		syslog(LOG_ERR, "Expected args = 3 but got args = %d\n", argc);
		closelog();
		return 1;
	}

	//We assume the directory exists
	FILE *fp = fopen(argv[1], "w");
	if(fp == NULL){
		printf("Could not open file %s\n", argv[1]);
		syslog(LOG_ERR, "Could not open file %s\n", argv[1]);
		closelog();
		return 1;
	}
	
	if(fprintf(fp, "%s", argv[2]) < 0){
		printf("Failed to write to %s\n", argv[1]);
		syslog(LOG_ERR, "Failed to write to %s\n", argv[1]);
		fclose(fp);
		closelog();
		return 1;
	}

	syslog(LOG_DEBUG, "Writing %s to %s\n", argv[2], argv[1]);
	fclose(fp);
	closelog();
}
