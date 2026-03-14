#include "nemu.h"

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>

enum {
  TK_NOTYPE = 256, TK_DEC, TK_HEX, TK_REG, TK_EQ, TK_NEQ, TK_AND, TK_DEREF, TK_NEG

  /* TODO: Add more token types */

};

static struct rule {
  char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {"[ \t]+", TK_NOTYPE},
  {"0[xX][0-9a-fA-F]+", TK_HEX},
  {"[0-9]+", TK_DEC},
  {"\\$[a-zA-Z][a-zA-Z0-9]*", TK_REG},
  {"==", TK_EQ},
  {"!=", TK_NEQ},
  {"&&", TK_AND},
  {"\\+", '+'},
  {"-", '-'},
  {"\\*", '*'},
  {"/", '/'},
  {"\\(", '('},
  {"\\)", ')'}
};

#define NR_REGEX (sizeof(rules) / sizeof(rules[0]) )

static regex_t re[NR_REGEX];

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[64];
static int nr_token;

static bool is_binary_operator(int type) {
  return type == '+' || type == '-' || type == '*' || type == '/' ||
         type == TK_EQ || type == TK_NEQ || type == TK_AND;
}

static bool is_unary_operator(int type) {
  return type == TK_DEREF || type == TK_NEG;
}

static int precedence(int type) {
  switch (type) {
    case TK_AND: return 1;
    case TK_EQ:
    case TK_NEQ: return 2;
    case '+':
    case '-': return 3;
    case '*':
    case '/': return 4;
    case TK_DEREF:
    case TK_NEG: return 5;
    default: return 0;
  }
}

static uint32_t eval(int p, int q, bool *success);

static bool check_parentheses(int p, int q) {
  int balance = 0;
  int i = 0;

  if (tokens[p].type != '(' || tokens[q].type != ')') {
    return false;
  }

  for (i = p; i <= q; i ++) {
    if (tokens[i].type == '(') {
      balance ++;
    }
    else if (tokens[i].type == ')') {
      balance --;
      if (balance < 0) {
        return false;
      }
      if (balance == 0 && i < q) {
        return false;
      }
    }
  }

  return balance == 0;
}

static int dominant_operator(int p, int q) {
  int balance = 0;
  int op = -1;
  int min_precedence = 0x7fffffff;
  int i = 0;

  for (i = p; i <= q; i ++) {
    int type = tokens[i].type;

    if (type == '(') {
      balance ++;
      continue;
    }

    if (type == ')') {
      balance --;
      if (balance < 0) {
        return -1;
      }
      continue;
    }

    if (balance != 0) {
      continue;
    }

    if (!is_binary_operator(type) && !is_unary_operator(type)) {
      continue;
    }

    if (precedence(type) < min_precedence) {
      min_precedence = precedence(type);
      op = i;
      continue;
    }

    if (precedence(type) == min_precedence && is_binary_operator(type)) {
      op = i;
    }
  }

  if (balance != 0) {
    return -1;
  }

  return op;
}

static uint32_t reg_str2val(const char *s, bool *success) {
  int i = 0;

  if (strcmp(s, "eip") == 0) {
    *success = true;
    return cpu.eip;
  }

  for (i = 0; i < 8; i ++) {
    if (strcmp(s, regsl[i]) == 0) {
      *success = true;
      return reg_l(i);
    }
    if (strcmp(s, regsw[i]) == 0) {
      *success = true;
      return reg_w(i);
    }
    if (strcmp(s, regsb[i]) == 0) {
      *success = true;
      return reg_b(i);
    }
  }

  *success = false;
  return 0;
}

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        //Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
        //    i, rules[i].regex, position, substr_len, substr_len, substr_start);
        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
          case TK_NOTYPE:
            break;
          default:
            Assert(nr_token < (int)(sizeof(tokens) / sizeof(tokens[0])), "Too many tokens in expression: %s", e);
            tokens[nr_token].type = rules[i].token_type;
            Assert(substr_len < (int)sizeof(tokens[nr_token].str), "Token is too long: %.*s", substr_len, substr_start);
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token ++;
            break;
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

static void rewrite_unary_tokens(void) {
  int i = 0;

  for (i = 0; i < nr_token; i ++) {
    if (tokens[i].type != '*' && tokens[i].type != '-') {
      continue;
    }

    if (i == 0 ||
        !(tokens[i - 1].type == TK_DEC ||
          tokens[i - 1].type == TK_HEX ||
          tokens[i - 1].type == TK_REG ||
          tokens[i - 1].type == ')')) {
      tokens[i].type = (tokens[i].type == '*') ? TK_DEREF : TK_NEG;
    }
  }
}

static uint32_t eval(int p, int q, bool *success) {
  int op = -1;
  uint32_t val1 = 0;
  uint32_t val2 = 0;

  if (p > q) {
    *success = false;
    return 0;
  }

  if (p == q) {
    switch (tokens[p].type) {
      case TK_DEC:
        *success = true;
        return strtoul(tokens[p].str, NULL, 10);
      case TK_HEX:
        *success = true;
        return strtoul(tokens[p].str, NULL, 16);
      case TK_REG:
        return reg_str2val(tokens[p].str + 1, success);
      default:
        *success = false;
        return 0;
    }
  }

  if (check_parentheses(p, q)) {
    return eval(p + 1, q - 1, success);
  }

  op = dominant_operator(p, q);
  if (op < 0) {
    *success = false;
    return 0;
  }

  if (tokens[op].type == TK_NEG) {
    val2 = eval(op + 1, q, success);
    return *success ? (uint32_t)(-(int32_t)val2) : 0;
  }

  if (tokens[op].type == TK_DEREF) {
    val2 = eval(op + 1, q, success);
    return *success ? vaddr_read(val2, 4) : 0;
  }

  val1 = eval(p, op - 1, success);
  if (!*success) {
    return 0;
  }

  val2 = eval(op + 1, q, success);
  if (!*success) {
    return 0;
  }

  switch (tokens[op].type) {
    case '+': return val1 + val2;
    case '-': return val1 - val2;
    case '*': return val1 * val2;
    case '/':
      if (val2 == 0) {
        *success = false;
        return 0;
      }
      return val1 / val2;
    case TK_EQ: return val1 == val2;
    case TK_NEQ: return val1 != val2;
    case TK_AND: return val1 && val2;
    default:
      *success = false;
      return 0;
  }
}

uint32_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  if (nr_token == 0) {
    *success = false;
    return 0;
  }

  rewrite_unary_tokens();
  return eval(0, nr_token - 1, success);
}
