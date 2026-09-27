#include <stdlib.h>
#include "../include/cli.h"
#include "../include/commands.h"
#include "../include/config.h"
#include "../include/log.h"
#include "../include/report.h"
#include "../include/sim.h"
#include "../include/trace.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Opens the trace, runs it, and reports the result in the chosen form.
 *
 * @return int 0 if the whole trace ran, 1 if it could not be opened or held a
 *         line that could not be read
 *
 * A malformed line makes this non-zero even though the run completed, because a
 * statistic from a trace that was only partly read should not be mistaken for one
 * from a trace that was read whole.
 */
static int run_trace_mode(simulator_t *sim, const options_t *opts){
    FILE *stream = stdin;
    const char *name = "<stdin>";

    //the usual convention: a lone hyphen means the stream, not a file of that name
    if(strcmp(opts->trace_path, "-") != 0){
        stream = fopen(opts->trace_path, "r");
        if(!stream){
            log_error("Error: could not open %s\n", opts->trace_path);
            return 1;
        }
        name = opts->trace_path;
    }

    //the configuration goes above the results so a run is self-describing. For csv
    //the row carries it instead, and printing it here would corrupt the file.
    if(opts->format == FORMAT_HUMAN){
        report_config(sim);
    }

    trace_summary_t summary;
    int status = run_trace(sim, stream, name, &summary);

    if(stream != stdin){
        fclose(stream);
    }

    if(opts->format == FORMAT_CSV){
        report_csv(sim, name, &summary);
    }
    else{
        report_trace_summary(sim, name, &summary);
        report_stats(&sim->stats);
    }

    return status == 0 ? 0 : 1;
}

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

    //interactive mode exists to show the mechanism, so it narrates by default. A
    //trace run has nothing to narrate - it never reports an individual access -
    //so it starts quiet.
    log_level_t mode_default = (opts.mode == MODE_TRACE) ? LOG_QUIET : LOG_VERBOSE;
    set_log_level(opts.log_level_given ? opts.log_level : mode_default);

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

    int exit_code = (opts.mode == MODE_TRACE)
                  ? run_trace_mode(&sim, &opts)
                  : run_interactive(&sim);

    //sim_init allocated these, so main() releases them. Neither front end can:
    //each of their exits is a return from inside a loop.
    sim_free(&sim);
    return exit_code;
}
