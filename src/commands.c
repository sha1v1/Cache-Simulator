#include "../include/commands.h"
#include "../include/log.h"
#include "../include/report.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

//Long enough for any command plus an over-long argument the user can then be
//told about, rather than having it silently split across two commands.
#define LINE_MAX_LEN 256

/**
 * @brief Splits off the next whitespace-separated token, in place.
 *
 * @param cursor points at the rest of the line; advanced past the token
 * @return char* the token, or NULL once the line is exhausted
 *
 * Hand-rolled rather than strtok_r so the file needs no feature macros to build
 * the same way everywhere.
 */
static char *nextToken(char **cursor){
    char *s = *cursor;
    while(*s && isspace((unsigned char)*s)){
        s++;
    }
    if(*s == '\0'){
        *cursor = s;
        return NULL;
    }
    char *start = s;
    while(*s && !isspace((unsigned char)*s)){
        s++;
    }
    if(*s){
        *s = '\0';
        s++;
    }
    *cursor = s;
    return start;
}

/**
 * @brief Parses an address, in hex with or without an 0x prefix.
 *
 * @param token the text to parse
 * @param out where the address is stored
 * @return int 0 on success, -1 if the text is not a whole hex number
 *
 * Hex is the default because that is how addresses are written everywhere else
 * in this program; base 16 also accepts the 0x prefix, so both "100" and "0x100"
 * mean the same address and neither silently parses as decimal.
 */
static int parseAddress(const char *token, unsigned int *out){
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(token, &end, 16);

    //reject trailing junk: "0x1g" must not quietly become 0x1
    if(end == token || *end != '\0'){
        return -1;
    }
    if(errno == ERANGE || value > UINT_MAX){
        return -1;
    }
    *out = (unsigned int)value;
    return 0;
}

/**
 * @brief Parses the byte to write: a single character, or an explicit byte value.
 *
 * @param token the text to parse
 * @param out where the byte is stored
 * @return int 0 on success, -1 if the text is neither form
 *
 * A one-character token is that character, so "w 0x10 7" writes '7' as it always
 * has. Anything longer has to say what it means with 0x41 or \x41, which is what
 * keeps "7" and "0x37" from being two readings of the same token.
 */
static int parseByte(const char *token, uint8_t *out){
    if(token[0] != '\0' && token[1] == '\0'){
        *out = (uint8_t)token[0];
        return 0;
    }

    const char *digits = NULL;
    if(strncasecmp(token, "0x", 2) == 0){
        digits = token + 2;
    }
    else if(token[0] == '\\' && (token[1] == 'x' || token[1] == 'X')){
        digits = token + 2;
    }
    if(!digits || *digits == '\0'){
        return -1;
    }

    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(digits, &end, 16);
    if(end == digits || *end != '\0' || errno == ERANGE || value > 0xFF){
        return -1;
    }
    *out = (uint8_t)value;
    return 0;
}

void printCommandHelp(void){
    printf("\nCommands\n");
    printf("  r <addr>            read a byte     (r 0x100)\n");
    printf("  w <addr> <value>    write a byte    (w 0x1a f  |  w 0x1a 0x41)\n");
    printf("  d                   display the cache\n");
    printf("  s                   show statistics\n");
    printf("  c                   show the configuration\n");
    printf("  v                   toggle verbose narration of the internals\n");
    printf("  reset               empty the cache and clear the statistics\n");
    printf("  h                   show this help\n");
    printf("  q                   quit\n");
    printf("\nAddresses are hex, with or without 0x. A value is one character,\n");
    printf("or 0xNN for a byte that is awkward to type. Blank lines and lines\n");
    printf("starting with # are ignored. The old menu numbers 1-4 still work.\n");
}

/**
 * @brief Reads a line from stdin into buf, dropping the trailing newline.
 *
 * @return int 1 on success, 0 at end of input
 *
 * A line longer than the buffer has its remainder discarded, so the leftover
 * characters cannot come back as a second, bogus command.
 */
static int readLine(char *buf, size_t size){
    if(!fgets(buf, (int)size, stdin)){
        return 0;
    }
    size_t len = strlen(buf);
    if(len > 0 && buf[len - 1] == '\n'){
        buf[len - 1] = '\0';
        return 1;
    }
    //no newline yet: the line was too long, so throw away the rest of it
    int c;
    while((c = getchar()) != '\n' && c != EOF);
    logError("Warning: input line too long, truncated\n");
    return 1;
}

/**
 * @brief Returns the next token, prompting for it if the line ran out.
 *
 * @param cursor the rest of the line
 * @param prompt what to ask for when interactive
 * @param interactive whether prompting is allowed
 * @param buf scratch space for a prompted answer
 * @return char* the token, or NULL if none is available
 *
 * This is what lets "r" and "r 0x100" both work: the argument can come with the
 * command or be asked for. A caller with nobody to ask passes interactive false
 * and gets a plain syntax error instead of a hung prompt.
 */
static char *tokenOrPrompt(char **cursor, const char *prompt, bool interactive,
                           char *buf, size_t size){
    char *token = nextToken(cursor);
    if(token || !interactive){
        return token;
    }

    printf("%s", prompt);
    fflush(stdout);
    if(!readLine(buf, size)){
        return NULL;
    }
    char *reply = buf;
    return nextToken(&reply);
}

CommandStatus runCommandLine(Simulator *sim, char *line, bool interactive){
    char *cursor = line;
    char *cmd = nextToken(&cursor);

    //blank lines and comments are not commands, so they are quietly skipped
    if(!cmd || cmd[0] == '#'){
        return CMD_OK;
    }

    char argbuf[LINE_MAX_LEN];
    AccessInfo info;

    if(strcasecmp(cmd, "r") == 0 || strcasecmp(cmd, "read") == 0 || strcmp(cmd, "1") == 0){
        char *token = tokenOrPrompt(&cursor, "Address (hex): 0x", interactive,
                                    argbuf, sizeof(argbuf));
        unsigned int addr;
        if(!token || parseAddress(token, &addr) != 0){
            logError("Error: expected an address, as in 'r 0x100'\n");
            return CMD_SYNTAX_ERROR;
        }
        SimStatus status = simRead(sim, addr, &info);
        if(status != SIM_OK){
            reportAccessError(status, addr);
            return CMD_FAILED;
        }
        reportAccess(sim, 'R', addr, &info);
        reportAccessDetail(sim, 'R', addr, &info);
        return CMD_OK;
    }

    if(strcasecmp(cmd, "w") == 0 || strcasecmp(cmd, "write") == 0 || strcmp(cmd, "2") == 0){
        char *token = tokenOrPrompt(&cursor, "Address (hex): 0x", interactive,
                                    argbuf, sizeof(argbuf));
        unsigned int addr;
        if(!token || parseAddress(token, &addr) != 0){
            logError("Error: expected an address, as in 'w 0x1a f'\n");
            return CMD_SYNTAX_ERROR;
        }

        //a second scratch buffer: the address may already be living in argbuf
        char valbuf[LINE_MAX_LEN];
        token = tokenOrPrompt(&cursor, "Value (character or 0xNN): ", interactive,
                              valbuf, sizeof(valbuf));
        uint8_t value;
        if(!token || parseByte(token, &value) != 0){
            logError("Error: expected one character or a byte like 0x41\n");
            return CMD_SYNTAX_ERROR;
        }

        SimStatus status = simWrite(sim, addr, value, &info);
        if(status != SIM_OK){
            reportAccessError(status, addr);
            return CMD_FAILED;
        }
        reportAccess(sim, 'W', addr, &info);
        reportAccessDetail(sim, 'W', addr, &info);
        return CMD_OK;
    }

    if(strcasecmp(cmd, "d") == 0 || strcasecmp(cmd, "display") == 0 ||
       strcasecmp(cmd, "cache") == 0 || strcmp(cmd, "3") == 0){
        reportCache(sim->cache);
        return CMD_OK;
    }

    if(strcasecmp(cmd, "s") == 0 || strcasecmp(cmd, "stats") == 0){
        reportStats(&sim->stats);
        return CMD_OK;
    }

    if(strcasecmp(cmd, "c") == 0 || strcasecmp(cmd, "config") == 0){
        reportConfig(sim);
        return CMD_OK;
    }

    if(strcasecmp(cmd, "v") == 0 || strcasecmp(cmd, "verbose") == 0){
        //the internals - which page was allocated, which line was chosen - are
        //worth seeing when following the mechanism and noise the rest of the
        //time, so they are a toggle rather than a permanent setting
        bool turning_on = getLogLevel() < LOG_VERBOSE;
        setLogLevel(turning_on ? LOG_VERBOSE : LOG_NORMAL);
        logInfo("Verbose narration %s.\n", turning_on ? "on" : "off");
        return CMD_OK;
    }

    if(strcasecmp(cmd, "reset") == 0){
        SimStatus status = simReset(sim);
        if(status != SIM_OK){
            logError("Error: %s; nothing was changed\n", simStatusMessage(status));
            return CMD_FAILED;
        }
        logInfo("Cache emptied and statistics cleared.\n");
        return CMD_OK;
    }

    if(strcasecmp(cmd, "h") == 0 || strcasecmp(cmd, "help") == 0 ||
       strcmp(cmd, "?") == 0 || strcasecmp(cmd, "menu") == 0){
        printCommandHelp();
        return CMD_OK;
    }

    if(strcasecmp(cmd, "q") == 0 || strcasecmp(cmd, "quit") == 0 ||
       strcasecmp(cmd, "exit") == 0 || strcmp(cmd, "4") == 0){
        return CMD_QUIT;
    }

    logError("Error: unknown command '%s'. Type 'h' for the command list.\n", cmd);
    return CMD_SYNTAX_ERROR;
}

int runInteractive(Simulator *sim){
    printf("--- Cache Simulator ---\n");
    reportConfig(sim);
    printCommandHelp();

    char line[LINE_MAX_LEN];
    while(1){
        printf("\ncache> ");
        fflush(stdout);

        if(!readLine(line, sizeof(line))){
            //stdin closed, e.g. a piped script that ran out or a Ctrl-D
            printf("\nEnd of input. Exiting Cache Simulator...\n");
            return 0;
        }

        //a bad command is reported and the prompt comes back: only 'q' and end
        //of input end the session
        if(runCommandLine(sim, line, true) == CMD_QUIT){
            printf("Exiting Cache Simulator...\n");
            return 0;
        }
    }
}
