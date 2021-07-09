#include <string.h>

#include "common.h"
#include "scanner.h"

void initScanner(Scanner *scanner, const char *source) {
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
    scanner->rawString = false;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isHexDigit(char c) {
    return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || (c == '_'));
}

static bool isAtEnd(Scanner *scanner) {
    return *scanner->current == '\0';
}

static char advance(Scanner *scanner) {
    scanner->current++;
    return scanner->current[-1];
}

static char peek(Scanner *scanner) {
    return *scanner->current;
}

static char peekNext(Scanner *scanner) {
    if (isAtEnd(scanner)) return '\0';
    return scanner->current[1];
}

static bool match(Scanner *scanner, char expected) {
    if (isAtEnd(scanner)) return false;
    if (*scanner->current != expected) return false;
    scanner->current++;
    return true;
}

static Token makeToken(Scanner *scanner, TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner->start;
    token.length = (int) (scanner->current - scanner->start);
    token.line = scanner->line;
    return token;
}

static Token errorToken(Scanner *scanner, const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int) strlen(message);
    token.line = scanner->line;

    return token;
}

static void skipWhitespace(Scanner *scanner) {
    for (;;) {
        char c = peek(scanner);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(scanner);
                break;

            case '\n':
                scanner->line++;
                advance(scanner);
                break;

            case '/':
                if (peekNext(scanner) == '*') {
                    // Multiline comments
                    advance(scanner);
                    advance(scanner);
                    while (true) {
                        while (peek(scanner) != '*' && !isAtEnd(scanner)) {
                            if ((c = advance(scanner)) == '\n') {
                                scanner->line++;
                            }
                        }

                        if (isAtEnd(scanner))
                            return;

                        if (peekNext(scanner) == '/') {
                            break;
                        }
                        advance(scanner);
                    }
                    advance(scanner);
                    advance(scanner);
                } else if (peekNext(scanner) == '/') {
                    // A comment goes until the end of the line.
                    while (peek(scanner) != '\n' && !isAtEnd(scanner)) advance(scanner);
                } else {
                    return;
                }
                break;

            default:
                return;
        }
    }
}

static TokenType checkKeyword(Scanner *scanner, int start, int length,
                              const char *rest, TokenType type) {
    if (scanner->current - scanner->start == start + length &&
        memcmp(scanner->start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(Scanner *scanner) {
    switch (scanner->start[0]) {
        case 'a':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'b': {
                        return checkKeyword(scanner, 2, 6, "stract", TOKEN_ABSTRACT);
                    }

                    case 'n': {
                        return checkKeyword(scanner, 2, 1, "d", TOKEN_AND);
                    }

                    case 's': {
                        return checkKeyword(scanner, 2, 0, "", TOKEN_AS);
                    }
                }
            }
            break;
        case 'b':
            return checkKeyword(scanner, 1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a':
			return checkKeyword(scanner, 2, 2, "se", TOKEN_CASE);
                    case 'l':
                        return checkKeyword(scanner, 2, 3, "ass", TOKEN_CLASS);
                    case 'o': {
                        // Skip second char
                        // Skip third char
                        if (scanner->current - scanner->start > 3) {
                            switch (scanner->start[3]) {
                                case 't':
                                    return checkKeyword(scanner, 4, 4, "inue", TOKEN_CONTINUE);
                                case 's':
                                    return checkKeyword(scanner, 4, 1, "t", TOKEN_CONST);
                            }
                        }
                    }
                }
            }
            break;
        case 'd':
               if (scanner->current - scanner->start > 3) {
                            switch (scanner->start[3]) {
                                case 'a':
                                    return checkKeyword(scanner, 4, 3, "ult", TOKEN_DEFAULT);
                            }
               }
            return checkKeyword(scanner, 1, 2, "ef", TOKEN_DEF);
        case 'e':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'l':
                        return checkKeyword(scanner, 2, 2, "se", TOKEN_ELSE);
                    case 'n':
                        return checkKeyword(scanner, 2, 2, "um", TOKEN_ENUM);
                }
            }
            break;
        case 'f':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a':
                        return checkKeyword(scanner, 2, 3, "lse", TOKEN_FALSE);
                    case 'o':
                        return checkKeyword(scanner, 2, 1, "r", TOKEN_FOR);
                    case 'r':
                        return checkKeyword(scanner, 2, 2, "om", TOKEN_FROM);
                }
            }
            break;
        case 'i':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'f':
                        return checkKeyword(scanner, 2, 0, "", TOKEN_IF);
                    case 'm':
                        return checkKeyword(scanner, 2, 4, "port", TOKEN_IMPORT);
                }
            }
            break;
        case 'n':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'o':
                        return checkKeyword(scanner, 2, 1, "t", TOKEN_BANG);
                    case 'i':
                        return checkKeyword(scanner, 2, 1, "l", TOKEN_NIL);
                }
            }
            break;
        case 'o':
            return checkKeyword(scanner, 1, 1, "r", TOKEN_OR);
        case 'p':
            return checkKeyword(scanner, 1, 6, "rivate", TOKEN_PRIVATE);
        case 'r':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'e':
                        return checkKeyword(scanner, 2, 4, "turn", TOKEN_RETURN);
                }
            } else {
                if (scanner->start[1] == '"' || scanner->start[1] == '\'') {
                    scanner->rawString = true;
                    return TOKEN_R;
                }
            }
            break;
        case 's':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 't':
                        return checkKeyword(scanner, 2, 4, "atic", TOKEN_STATIC);
                    case 'u':
                        return checkKeyword(scanner, 2, 3, "per", TOKEN_SUPER);
		    case 'w':
                        return checkKeyword(scanner, 2, 4, "itch", TOKEN_SWITCH);
                }
            }
            break;
        case 't':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'h':
                        return checkKeyword(scanner, 2, 2, "is", TOKEN_THIS);
                    case 'r':
                        if (scanner->current - scanner->start > 2) {
                            switch (scanner->start[2]) {
                                case 'u':
                                    return checkKeyword(scanner, 3, 1, "e", TOKEN_TRUE);
                                case 'a':
                                    return checkKeyword(scanner, 3, 2, "it", TOKEN_TRAIT);
                            }
                        }
                }
            }
            break;
        case 'u':
            return checkKeyword(scanner, 1, 2, "se", TOKEN_USE);
        case 'v':
            return checkKeyword(scanner, 1, 2, "ar", TOKEN_VAR);
        case 'w':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'h':
                        return checkKeyword(scanner, 2, 3, "ile", TOKEN_WHILE);
                    case 'i':
                        return checkKeyword(scanner, 2, 2, "th", TOKEN_WITH);
                }
            }
            break;
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier(Scanner *scanner) {
    while (isAlpha(peek(scanner)) || isDigit(peek(scanner))) advance(scanner);

    return makeToken(scanner,identifierType(scanner));
}

static Token exponent(Scanner *scanner) {
    // Consume the "e"
    advance(scanner);
    while (peek(scanner) == '_') advance(scanner);
    if (peek(scanner) == '+' || peek(scanner) == '-') {
        // Consume the "+ or -"
        advance(scanner);
    }
    if (!isDigit(peek(scanner)) && peek(scanner) != '_') return errorToken(scanner, "Invalid exopnent literal");
    while (isDigit(peek(scanner)) || peek(scanner) == '_') advance(scanner);
    return makeToken(scanner,TOKEN_NUMBER);
}

static Token number(Scanner *scanner) {
    while (isDigit(peek(scanner)) || peek(scanner) == '_') advance(scanner);
    if (peek(scanner) == 'e' || peek(scanner) == 'E')
        return exponent(scanner);
    // Look for a fractional part.
    if (peek(scanner) == '.' && (isDigit(peekNext(scanner)))) {
        // Consume the "."
        advance(scanner);
        while (isDigit(peek(scanner)) || peek(scanner) == '_') advance(scanner);
        if (peek(scanner) == 'e' || peek(scanner) == 'E')
            return exponent(scanner);
    }
    return makeToken(scanner,TOKEN_NUMBER);
}

static Token hexNumber(Scanner *scanner) {
    while (peek(scanner) == '_') advance(scanner);
    if (peek(scanner) == '0')advance(scanner);
    if ((peek(scanner) == 'x') || (peek(scanner) == 'X')) {
        advance(scanner);
        if (!isHexDigit(peek(scanner))) return errorToken(scanner, "Invalid hex literal");
        while (isHexDigit(peek(scanner))) advance(scanner);
        return makeToken(scanner,TOKEN_NUMBER);
    } else return number(scanner);
}


static Token string(Scanner *scanner, char stringToken) {
    while (peek(scanner) != stringToken && !isAtEnd(scanner)) {
        if (peek(scanner) == '\n') {
            scanner->line++;
        } else if (peek(scanner) == '\\' && !scanner->rawString) {
            scanner->current++;
        }
        advance(scanner);
    }
    if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated string.");

    // The closing " or '.
    advance(scanner);
    scanner->rawString = false;
    return makeToken(scanner,TOKEN_STRING);
}

void backTrack(Scanner *scanner) {
    scanner->current--;
}

Token scanToken(Scanner *scanner) {
    skipWhitespace(scanner);

    scanner->start = scanner->current;

    if (isAtEnd(scanner)) return makeToken(scanner, TOKEN_EOF);

    char c = advance(scanner);

    if (isAlpha(c)) return identifier(scanner);
    if (isDigit(c)) return hexNumber(scanner);

    switch (c) {
        case '(':
            return makeToken(scanner, TOKEN_LEFT_PAREN);
        case ')':
            return makeToken(scanner, TOKEN_RIGHT_PAREN);
        case '{':
            return makeToken(scanner, TOKEN_LEFT_BRACE);
        case '}':
            return makeToken(scanner, TOKEN_RIGHT_BRACE);
        case '[':
            return makeToken(scanner, TOKEN_LEFT_BRACKET);
        case ']':
            return makeToken(scanner, TOKEN_RIGHT_BRACKET);
        case ';':
            return makeToken(scanner, TOKEN_SEMICOLON);
        case ':':
            return makeToken(scanner, TOKEN_COLON);
        case ',':
            return makeToken(scanner, TOKEN_COMMA);
        case '.':
            return makeToken(scanner, TOKEN_DOT);
        case '/': {
            if (match(scanner, '=')) {
                return makeToken(scanner, TOKEN_DIVIDE_EQUALS);
            } else {
                return makeToken(scanner, TOKEN_SLASH);
            }
        }
        case '*': {
            if (match(scanner, '=')) {
                return makeToken(scanner, TOKEN_MULTIPLY_EQUALS);
            } else if (match(scanner, '*')) {
                return makeToken(scanner, TOKEN_STAR_STAR);
            } else {
                return makeToken(scanner, TOKEN_STAR);
            }
        }
        case '%':
            return makeToken(scanner, TOKEN_PERCENT);
        case '-': {
            if (match(scanner, '=')) {
                return makeToken(scanner, TOKEN_MINUS_EQUALS);
            } else {
                return makeToken(scanner, TOKEN_MINUS);
            }
        }
        case '+': {
            if (match(scanner, '=')) {
                return makeToken(scanner, TOKEN_PLUS_EQUALS);
            } else {
                return makeToken(scanner, TOKEN_PLUS);
            }
        }
        case '&':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_AMPERSAND_EQUALS : TOKEN_AMPERSAND);
        case '^':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_CARET_EQUALS : TOKEN_CARET);
        case '|':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_PIPE_EQUALS : TOKEN_PIPE);
        case '!':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            if (match(scanner, '=')) {
                return makeToken(scanner, TOKEN_EQUAL_EQUAL);
            } else if (match(scanner, '>')) {
                return makeToken(scanner, TOKEN_ARROW);
            } else {
                return makeToken(scanner, TOKEN_EQUAL);
            }
        case '?':
            if (match(scanner, '.')) {
                return makeToken(scanner, TOKEN_QUESTION_DOT);
            }
            return makeToken(scanner, TOKEN_QUESTION);
        case '<':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(scanner, match(scanner, '=') ?
                             TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"':
            return string(scanner, '"');
        case '\'':
            return string(scanner, '\'');
    }

    return errorToken(scanner, "Unexpected character.");
}
