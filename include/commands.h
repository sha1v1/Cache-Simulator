#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>
#include <stdio.h>
#include "sim.h"

//What executing one command line produced.
typedef enum {
    CMD_OK,            //the command ran
    CMD_QUIT,          //the user asked to stop
    CMD_SYNTAX_ERROR,  //the line could not be understood
    CMD_FAILED         //the line was understood but the access failed
} CommandStatus;

/**
 * @brief Parses and runs one command line against a simulator.
 *
 * @param sim the simulator to drive
 * @param line the line to run; tokenized in place, so it must be writable
 * @param interactive true to prompt for arguments the line left out
 * @return CommandStatus what happened
 *
 * Everything the user can ask for funnels through here, so the command language
 * is defined in exactly one place.
 */
CommandStatus runCommandLine(Simulator *sim, char *line, bool interactive);

//Runs the menu loop until the user quits or input ends. Returns a process exit code.
int runInteractive(Simulator *sim);

//The command reference, shown at startup and by the "help" command.
void printCommandHelp(void);

#endif
