#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int num_sets;
    int main_memory_size;
    int lines_per_set;
    char replacement_policy[10];
    char write_policy[15];
} Config;

void readConfigFile(Config *config);

#endif 