#include <stdlib.h>
#include <time.h>
#include "../include/cli.h"
#include "../include/commands.h"
#include "../include/config.h"
#include "../include/log.h"
#include "../include/report.h"
#include "../include/sim.h"

int main(int argc, char **argv){
    Options opts;
    setOptionDefaults(&opts);
    if(parseArgs(argc, argv, &opts) != 0){
        printUsage(argv[0]);
        return 1;
    }

    if(opts.mode == MODE_HELP){
        printUsage(argv[0]);
        return 0;
    }

    //no mode means there is nothing to run. Saying so with a non-zero status is
    //what keeps "nothing was asked of me" distinguishable from "the work is
    //done" for anything scripting this program.
    if(opts.mode == MODE_NONE){
        printUsage(argv[0]);
        logError("\nNo mode selected. Try --interactive.\n");
        return 1;
    }

    srand(time(NULL));

    //interactive mode exists to show the mechanism, so its narration is on
    //unless the user asked for something quieter
    setLogLevel(opts.log_level_given ? opts.log_level : LOG_VERBOSE);

    //defaults first, so a setting the config file leaves out still has a value
    Config config;
    setConfigDefaults(&config);

    //the config module hands back what was wrong with a line rather than
    //printing it, so the complaint is worded here, where it is known that this
    //is a program starting up rather than, say, a file being validated
    char config_error[160];
    int read_status = readConfigFile(&config, opts.config_path,
                                     config_error, sizeof(config_error));
    if(read_status == -1){
        //a file the user named and we cannot open is a mistake worth stopping
        //for. The default one merely being absent is not: the built-in defaults
        //are a usable machine, and saying so beats refusing to start.
        if(opts.config_path_given){
            reportConfigFileError(opts.config_path, NULL);
            return 1;
        }
        logInfo("No %s found, using built-in defaults.\n", opts.config_path);
    }
    else if(read_status != 0){
        reportConfigFileError(opts.config_path, config_error);
        return 1;
    }

    //simInit validates the configuration and owns the cache and memory, so a
    //bad config stops here rather than part-way through the first access
    Simulator sim;
    SimStatus status = simInit(&sim, &config);
    if(status != SIM_OK){
        reportStartupError(status, &config);
        return 1;
    }

    int exit_code = runInteractive(&sim);

    //simInit allocated these, so main() releases them. runInteractive cannot:
    //each of its exits is a return from inside the command loop.
    simFree(&sim);
    return exit_code;
}
