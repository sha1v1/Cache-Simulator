#include <stdlib.h>
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
    //what keeps "nothing was asked of me" distinguishable from "the work is done"
    //for anything scripting this program.
    if(opts.mode == MODE_NONE){
        print_usage(argv[0]);
        log_error("\nNo mode selected. Try --interactive.\n");
        return 1;
    }

    //the command line is the whole of the configuration, so this cannot fail for
    //any reason but the sizes not fitting together, which build_config explains
    config_t config;
    if(build_config(&opts, &config) != 0){
        return 1;
    }

    //interactive mode exists to show the mechanism, so its narration is on unless
    //the user asked for something quieter
    set_log_level(opts.log_level_given ? opts.log_level : LOG_VERBOSE);

    //seeded from the configuration rather than the clock, so that two runs of the
    //same command agree even when the replacement policy draws at random
    srand(config.seed);

    //sim_init validates the configuration and owns the cache and memory, so a bad
    //one stops here rather than part-way through the first access
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
