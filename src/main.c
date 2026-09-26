#include <stdlib.h>
#include <time.h>
#include "../include/cli.h"
#include "../include/commands.h"
#include "../include/config.h"
#include "../include/log.h"
#include "../include/report.h"
#include "../include/sim.h"

int main(int argc, char **argv){
    options_t opts;
    set_option_defaults(&opts);
    if(parse_args(argc, argv, &opts) != 0){
        print_usage(argv[0]);
        return 1;
    }

    if(opts.mode == MODE_HELP){
        print_usage(argv[0]);
        return 0;
    }

    //no mode means there is nothing to run. Saying so with a non-zero status is
    //what keeps "nothing was asked of me" distinguishable from "the work is
    //done" for anything scripting this program.
    if(opts.mode == MODE_NONE){
        print_usage(argv[0]);
        log_error("\nNo mode selected. Try --interactive.\n");
        return 1;
    }

    srand(time(NULL));

    //interactive mode exists to show the mechanism, so its narration is on
    //unless the user asked for something quieter
    set_log_level(opts.log_level_given ? opts.log_level : LOG_VERBOSE);

    //defaults first, so a setting the config file leaves out still has a value
    config_t config;
    set_config_defaults(&config);

    //the config module hands back what was wrong with a line rather than
    //printing it, so the complaint is worded here, where it is known that this
    //is a program starting up rather than, say, a file being validated
    char config_error[160];
    int read_status = read_config_file(&config, opts.config_path,
                                     config_error, sizeof(config_error));
    if(read_status == -1){
        //a file the user named and we cannot open is a mistake worth stopping
        //for. The default one merely being absent is not: the built-in defaults
        //are a usable machine, and saying so beats refusing to start.
        if(opts.config_path_given){
            report_config_file_error(opts.config_path, NULL);
            return 1;
        }
        log_info("No %s found, using built-in defaults.\n", opts.config_path);
    }
    else if(read_status != 0){
        report_config_file_error(opts.config_path, config_error);
        return 1;
    }

    //sim_init validates the configuration and owns the cache and memory, so a
    //bad config stops here rather than part-way through the first access
    simulator_t sim;
    sim_status_t status = sim_init(&sim, &config);
    if(status != SIM_OK){
        report_startup_error(status, &config);
        return 1;
    }

    int exit_code = run_interactive(&sim);

    //sim_init allocated these, so main() releases them. run_interactive cannot:
    //each of its exits is a return from inside the command loop.
    sim_free(&sim);
    return exit_code;
}
