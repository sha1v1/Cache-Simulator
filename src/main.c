#include <stdlib.h>
#include <time.h>
#include "../include/commands.h"
#include "../include/config.h"
#include "../include/log.h"
#include "../include/report.h"
#include "../include/sim.h"

int main(){
    srand(time(NULL));

    //defaults first, so a setting the config file leaves out still has a value
    Config config;
    setConfigDefaults(&config);

    //the config module hands back what was wrong with a line rather than
    //printing it, so the complaint is worded here, where it is known that this
    //is a program starting up rather than, say, a file being validated
    char config_error[160];
    int read_status = readConfigFile(&config, DEFAULT_CONFIG_PATH,
                                     config_error, sizeof(config_error));
    if(read_status == -1){
        //not fatal: the built-in defaults are a usable machine, and saying so is
        //more useful than refusing to start over a missing file
        logInfo("No %s found, using built-in defaults.\n", DEFAULT_CONFIG_PATH);
    }
    else if(read_status != 0){
        reportConfigFileError(DEFAULT_CONFIG_PATH, config_error);
        return 1;
    }

    //simInit validates the configuration and owns the cache and memory, so a
    //bad config.txt stops here rather than part-way through the first access
    Simulator sim;
    SimStatus status = simInit(&sim, &config);
    if(status != SIM_OK){
        reportStartupError(status, &config);
        return 1;
    }

    int exit_code = runInteractive(&sim);

    //simInit allocated these, so main() releases them. runInteractive cannot:
    //each of its exits is a return from inside the menu loop.
    simFree(&sim);
    return exit_code;
}
