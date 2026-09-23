#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



void readConfigFile(Config *config){

    char cwd[1024];
if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("Current working directory: %s\n", cwd);
}

    //open config file
    FILE* config_file = fopen("config.txt", "r");
    if(!config_file){
        printf("Could not open config.txt\n");
        exit(1);
    }
    
    char buffer[50];
    //fgets reads a line and stores in buffer after terminating it with \0
    //i.e. buffer stores a valid string 
    while(fgets(buffer, sizeof(buffer), config_file)){
        
        //blank lines (including a trailing newline at the end of the file)
        //hold no setting, so skip them before trying to parse a pair
        if(buffer[strspn(buffer, " \t\r\n")] == '\0'){
            continue;
        }

        //to store the key-value pair in the current line
        char key[25], value[25];

        //without this check a failed parse would leave key and value unset,
        //and every branch below would then read uninitialized memory
        if(sscanf(buffer, "%24[^=]=%24s", key, value) != 2){
            printf("Malformed line in config file: %s", buffer);
            exit(1);
        }

        if(strcmp(key, "num_sets") == 0){
            config->num_sets = atoi(value);
            printf("Read the number of sets\n");
        }
        else if(strcmp(key, "main_memory_size") == 0){
            config->main_memory_size = atoi(value);
            printf("Red main memory size\n");
        }
        else if(strcmp(key, "lines_per_set") == 0){
            config->lines_per_set = atoi(value);
            printf("read lines per set\n");
        }
        else if(strcmp(key, "replacement_policy") == 0){
            if(strcmp(value, "LRU") == 0){
                config->replacement_policy = POLICY_LRU;
            }
            else if(strcmp(value, "RANDOM") == 0){
                config->replacement_policy = POLICY_RANDOM;
            }
            else{
                printf("Error: unknown replacement_policy '%s' (expected LRU or RANDOM)\n", value);
                exit(1);
            }
            printf("Read the replacement policy\n");
        }
        else if(strcmp(key,"write_policy") == 0){
            if(strcmp(value, "WRITE_THROUGH") == 0){
                config->write_policy = WRITE_THROUGH;
            }
            else if(strcmp(value, "WRITE_BACK") == 0){
                config->write_policy = WRITE_BACK;
            }
            else{
                printf("Error: unknown write_policy '%s' (expected WRITE_THROUGH or WRITE_BACK)\n", value);
                exit(1);
            }
            printf("read write policy\n");
        }
        else{
            printf("Unknown key: %s\n", key);
            exit(1);
        }

    }
    printf("Read config file.\n");
    return;
}