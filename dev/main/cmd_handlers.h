/**
 * Command handler interface
 */
#ifndef CMD_HANDLERS_H
#define CMD_HANDLERS_H

/* Execute a command and write output to buffer */
void cmd_handler_execute(const char *cmd, char *out_buf, int out_bufsize);

#endif
