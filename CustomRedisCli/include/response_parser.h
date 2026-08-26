#ifndef RESPONSE_PARSER_H
#define RESPONSE_PARSER_H

/* Reads one RESP (REDIS Serialization Protocol v2) reply from sockfd and
 * returns it as a newly-allocated, human-readable string. The caller owns
 * the returned pointer and must free() it. Never returns NULL - on
 * failure it returns a malloc'd "(Error) ..." message instead, so callers
 * don't need a null check before printing. */
char *response_parser_parse(int sockfd);

#endif /* RESPONSE_PARSER_H */
