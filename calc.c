// Derin Atila 251ADC003
// (optional) https://github.com/derinatila/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Claude (Anthropic) AI, conversation: https://claude.ai/share/3fef1993-8da1-4dd0-a298-43fd3c477e42
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

/* Student data used in the output file and folder names. */
#define STUDENT_NAME     "derin"
#define STUDENT_LASTNAME "atila"
#define STUDENT_ID       "251ADC003"

/* ======================================================================
 *  Growable byte buffer
 *
 *  Used both for reading the input file and for collecting the output.
 *  The capacity is doubled whenever it runs out, so a file of n bytes is
 *  copied only O(n) times in total (1 + 1/2 + 1/4 + ... < 2 copies per
 *  byte).  Growing by a fixed amount instead would copy the data again and
 *  again and become quadratic on files of several megabytes.
 *
 *  AI (Claude): prompt "i need to read a file in c but i dont know its size
 *    beforehand the assignment says the file could be a few megabytes i want
 *    to use malloc and realloc how can I create a dynamic array that grows
 *    automatically without slowing down the program or causing memory leaks"
 *    Link: see line 4.  Used: doubling the capacity with realloc.
 * ====================================================================== */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buffer;

/* Makes sure at least `extra` more bytes fit.  Returns 0 on success. */
static int buf_reserve(Buffer *b, size_t extra)
{
    if (b->len + extra <= b->cap)
        return 0;
    size_t new_cap = b->cap ? b->cap : 4096;
    while (new_cap < b->len + extra)
        new_cap *= 2;
    char *p = realloc(b->data, new_cap);
    if (!p)
        return -1;
    b->data = p;
    b->cap = new_cap;
    return 0;
}

static int buf_append(Buffer *b, const char *s, size_t n)
{
    if (buf_reserve(b, n) != 0)
        return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    return 0;
}

/* Reads a whole file into memory in one pass.  One extra byte is always
 * kept after the data (set to '\0') because the number tokenizer
 * temporarily writes a terminator right after a float literal. */
static int read_whole_file(const char *path, Buffer *b)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    for (;;) {
        if (buf_reserve(b, 65536 + 1) != 0) {
            fclose(f);
            return -1;
        }
        size_t got = fread(b->data + b->len, 1, b->cap - b->len - 1, f);
        b->len += got;
        if (got == 0)
            break;
    }
    int failed = ferror(f);
    fclose(f);
    if (failed)
        return -1;
    b->data[b->len] = '\0';
    return 0;
}

/* ======================================================================
 *  Values: integer or float
 *
 *  A value remembers whether it is an integer (long long) or a float
 *  (double).  An integer literal is an integer, a literal with a '.' is a
 *  float.  An operation is done in double as soon as one of its operands
 *  is a float, otherwise it stays in long long.  So the "floatness" spreads
 *  upwards through the expression exactly like C's usual arithmetic
 *  conversions: 7 / 2 is 3, but 7.0 / 2 and 7 / 2.0 are 3.5.
 * ====================================================================== */
typedef struct {
    int       is_float;
    long long i;
    double    f;
} Value;

static double as_double(Value v)
{
    return v.is_float ? v.f : (double)v.i;
}

/* ======================================================================
 *  Tokenizer
 *
 *  The whole file is one char array and `pos` is a 0-based index into it.
 *  Because every character of the file (including '\n' and the characters
 *  of comment lines) stays in the array, the index of a character IS its
 *  position in the file; the reported position is simply index + 1.
 *  Each token remembers the index where it starts, so an error can always
 *  be reported at "the start of the token the parser was looking at".
 *
 *  Only spaces and tabs are skipped.  A '\n' is not whitespace: it is the
 *  END-OF-LINE token, so an expression can never continue on the next
 *  line.  The end of the file is also an END-OF-LINE token whose start is
 *  the file length, which gives "file length + 1" for a missing operand
 *  on a last line without '\n'.
 * ====================================================================== */
typedef enum {
    T_NUM, T_PLUS, T_MINUS, T_STAR, T_SLASH, T_POW,
    T_LPAREN, T_RPAREN, T_EOL, T_BAD
} TokenType;

typedef struct {
    TokenType type;
    size_t    start;  /* index of the first character of the token     */
    size_t    end;    /* index just after the token                     */
    Value     value;  /* only for T_NUM                                 */
} Token;

/* Parser state for the file that is being evaluated. */
static char  *src;        /* file contents (src[src_len] is '\0')        */
static size_t src_len;    /* number of bytes in the file                 */
static size_t pos;        /* where the tokenizer continues reading       */
static Token  cur;        /* the token the parser is looking at          */
static int    has_error;  /* set by the first error; parsing then stops  */
static size_t error_index;/* 0-based index of that first error           */

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Records an error at a 0-based index.  Only the first error counts: as
 * soon as has_error is set every parse function returns immediately, so
 * nothing after the first error is looked at.
 *
 * AI (Claude): prompt "when there is an error in the parser i want to report
 *   only the first error and stop. i am also struggling to track the exact
 *   character position (index) to report ERROR:<pos>. how should i structure
 *   the error flag and position counter"  Link: see line 4.
 *   Used: an error flag where only the first error is recorded. */
static void set_error(size_t index)
{
    if (!has_error) {
        has_error = 1;
        error_index = index;
    }
}

/* Reads a number starting at pos into cur.
 * Integer: digits.  Float: digits '.' digits.  ".5" never gets here (it
 * starts with '.', an invalid character).  For "5." the '.' is not followed
 * by a digit, so the number is invalid and the error token starts at the
 * '.', as the task requires.  "1.5e3" is read as 1.5 and the 'e' then
 * becomes an invalid-character token of its own.
 *
 * AI (Claude): prompt "when reading numbers from the file using strtod i
 *   first have to copy the number into a temporary array because the string
 *   continues afterward is there a more practical way to do this without
 *   copying the number to a separate array"  Link: see line 4.
 *   Used: calling strtod directly on the file buffer without a copy. */
static void lex_number(void)
{
    size_t p = pos;
    unsigned long long acc = 0;   /* unsigned: no undefined behaviour */
    while (p < src_len && is_digit(src[p])) {
        acc = acc * 10u + (unsigned long long)(src[p] - '0');
        p++;
    }

    if (p < src_len && src[p] == '.') {
        if (p + 1 < src_len && is_digit(src[p + 1])) {
            p++;
            while (p < src_len && is_digit(src[p]))
                p++;
            /* strtod would also read an exponent like "e3", so a '\0' is
             * put right after the literal and restored afterwards.  The
             * number is read straight from the file buffer, not copied. */
            char saved = src[p];
            src[p] = '\0';
            cur.value.f = strtod(src + pos, NULL);
            src[p] = saved;
            cur.value.is_float = 1;
            cur.value.i = 0;
            cur.type = T_NUM;
            cur.end = p;
            return;
        }
        cur.type = T_BAD;          /* "5." : error at the '.' */
        cur.start = p;
        cur.end = p + 1;
        return;
    }

    cur.type = T_NUM;
    cur.value.is_float = 0;
    cur.value.i = (long long)acc;
    cur.value.f = 0.0;
    cur.end = p;
}

/* Reads the token at pos into cur (pos is moved only over spaces/tabs). */
static void lex(void)
{
    while (pos < src_len && (src[pos] == ' ' || src[pos] == '\t'))
        pos++;

    cur.start = pos;
    if (pos >= src_len) {            /* end of file acts as end of line */
        cur.type = T_EOL;
        cur.end = pos;
        return;
    }

    char c = src[pos];
    cur.end = pos + 1;
    if (is_digit(c)) {
        lex_number();
        return;
    }
    switch (c) {
    case '\n': cur.type = T_EOL;    break;
    case '+':  cur.type = T_PLUS;   break;
    case '-':  cur.type = T_MINUS;  break;
    case '/':  cur.type = T_SLASH;  break;
    case '(':  cur.type = T_LPAREN; break;
    case ')':  cur.type = T_RPAREN; break;
    case '*':
        /* "**" is one token, so "2 ** ** 3" sees a second "**" (error at
         * it) and not two separate '*'. */
        if (pos + 1 < src_len && src[pos + 1] == '*') {
            cur.type = T_POW;
            cur.end = pos + 2;
        } else {
            cur.type = T_STAR;
        }
        break;
    default:
        /* Any other character, including '#' that is not at the start of
         * a line, is invalid.  It becomes a token so that the error is
         * reported only when the parser actually reaches it. */
        cur.type = T_BAD;
        break;
    }
}

static void next_token(void)
{
    pos = cur.end;
    lex();
}

/* ======================================================================
 *  Evaluation of one binary operation
 *
 *  op_index:    where the operator starts (used for inf/nan results)
 *  right_index: where the right operand starts (division by zero and a
 *               negative integer exponent are reported there, because
 *               the right operand is what makes the operation invalid)
 * ====================================================================== */

/* Integer power with non-negative exponent by repeated squaring: O(log e)
 * multiplications, so even a huge exponent like 1 ** 1000000000 is fast.
 * The base is squared only while bits are left, so no intermediate value
 * is bigger than the final result (the tests never overflow). */
static long long int_pow(long long base, long long e)
{
    long long result = 1;
    while (e > 0) {
        if (e & 1)
            result *= base;
        e >>= 1;
        if (e > 0)
            base *= base;
    }
    return result;
}

static Value apply_op(TokenType op, size_t op_index, Value a, Value b,
                      size_t right_index)
{
    Value r = {0, 0, 0.0};

    if (a.is_float || b.is_float) {
        double x = as_double(a);
        double y = as_double(b);
        double z = 0.0;
        switch (op) {
        case T_PLUS:  z = x + y; break;
        case T_MINUS: z = x - y; break;
        case T_STAR:  z = x * y; break;
        case T_SLASH:
            if (y == 0.0) {          /* 1.5 / 0.0 is division by zero */
                set_error(right_index);
                return r;
            }
            z = x / y;
            break;
        case T_POW:   z = pow(x, y); break;
        default:      break;
        }
        /* inf (10.0 ** 400) or nan ((0 - 8) ** 0.5) is not a valid result;
         * it is the operator that produced it, so report the operator. */
        if (isinf(z) || isnan(z)) {
            set_error(op_index);
            return r;
        }
        r.is_float = 1;
        r.f = z;
        return r;
    }

    long long x = a.i, y = b.i;
    r.is_float = 0;
    switch (op) {
    case T_PLUS:  r.i = x + y; break;
    case T_MINUS: r.i = x - y; break;
    case T_STAR:  r.i = x * y; break;
    case T_SLASH:
        if (y == 0) {
            set_error(right_index);
            return r;
        }
        r.i = x / y;                 /* C division truncates toward zero */
        break;
    case T_POW:
        if (y < 0) {                 /* 2 ** (1 - 2): no integer result */
            set_error(right_index);
            return r;
        }
        r.i = int_pow(x, y);         /* includes x ** 0 == 1, 0 ** 0 == 1 */
        break;
    default:
        break;
    }
    return r;
}

/* ======================================================================
 *  Recursive descent parser
 *
 *    expr    := term    { ('+' | '-') term }
 *    term    := power   { ('*' | '/') power }
 *    power   := primary [ '**' power ]
 *    primary := NUMBER | '(' expr ')'
 *
 *  Why this gives the right precedence: each level only calls the level
 *  below it for its operands.  expr can only combine whole terms, and a
 *  term is finished only when no more '*' or '/' follow, so in
 *  3 + 2 * 5 the "2 * 5" is already one term before '+' sees it.  The same
 *  way '**' binds tighter than '*' and '/'.  Parentheses go back to the
 *  top (expr), so anything inside them becomes a single primary.
 *
 *  Why + - * / are left-associative: the loops combine the result so far
 *  with the next operand immediately (left = left op right), so
 *  7 - 2 - 1 is (7 - 2) - 1.
 *
 *  Why ** is right-associative: in the grammar the exponent is a whole
 *  "power" again, so 2 ** 3 ** 2 means 2 ** (3 ** 2) = 512.  parse_power()
 *  does exactly this, but without calling itself: it first collects all
 *  operands of a "a ** b ** c ..." chain and then combines them from the
 *  RIGHT end.  This gives the same result as the recursive rule, but a
 *  chain of a million '**' in a big file cannot overflow the call stack.
 *
 *  Every function evaluates while it parses and returns the value.  After
 *  an error (has_error) every function returns at once, so the first error
 *  in reading order is the one that is reported.
 *
 *  Where syntax errors are reported: always at cur.start, the start of the
 *  token the parser was looking at when it could not continue:
 *    - "7 + * 3"   primary() expects a number or '(' and sees '*'
 *    - "3 *" '\n'  primary() sees the END-OF-LINE token (the '\n')
 *    - "(2 + 3"    primary() expects ')' and sees END-OF-LINE
 *    - "(1 + 2))"  the line must end after expr, but cur is ')'
 *    - "2 + & 5"   primary() sees the invalid-character token '&'
 *    - "-5"        no unary minus: primary() sees '-'
 * ====================================================================== */

static Value parse_expr(void);

static Value parse_primary(void)
{
    Value v = {0, 0, 0.0};
    if (has_error)
        return v;

    if (cur.type == T_NUM) {
        v = cur.value;
        next_token();
        return v;
    }
    if (cur.type == T_LPAREN) {
        next_token();
        v = parse_expr();
        if (has_error)
            return v;
        if (cur.type != T_RPAREN) {
            set_error(cur.start);
            return v;
        }
        next_token();
        return v;
    }
    set_error(cur.start);
    return v;
}

/* Operand stack for parse_power().  One global stack is shared by all
 * (nested) calls: each call only uses the part above the height it found
 * at its start and pops back to that height before returning.  It grows
 * by doubling, like Buffer.
 *
 * AI (Claude): prompt "for the ** (power) operator which is right-associative,
 *   i wrote a recursive function but the assignment says files can be huge.
 *   can a recursive function cause a stack overflow? how can i calculate this
 *   from right to left using my own dynamically allocated array (stack)
 *   instead"  Link: see line 4.
 *   Used: pushing the operands onto a stack and combining them from the right. */
typedef struct {
    Value  value;
    size_t start;     /* where this operand starts                      */
    size_t op_index;  /* where the '**' in front of it starts           */
} PowItem;

static PowItem *pow_stack;
static size_t   pow_top;
static size_t   pow_cap;

static int pow_push(Value v, size_t start, size_t op_index)
{
    if (pow_top == pow_cap) {
        size_t new_cap = pow_cap ? pow_cap * 2 : 64;
        PowItem *p = realloc(pow_stack, new_cap * sizeof *p);
        if (!p)
            return -1;
        pow_stack = p;
        pow_cap = new_cap;
    }
    pow_stack[pow_top].value = v;
    pow_stack[pow_top].start = start;
    pow_stack[pow_top].op_index = op_index;
    pow_top++;
    return 0;
}

static Value parse_power(void)
{
    Value v = {0, 0, 0.0};
    if (has_error)
        return v;

    size_t base = pow_top;
    size_t start = cur.start;
    v = parse_primary();
    if (has_error)
        return v;
    if (cur.type != T_POW)
        return v;

    if (pow_push(v, start, 0) != 0) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    while (cur.type == T_POW) {
        size_t op_index = cur.start;
        next_token();
        size_t operand_start = cur.start;
        Value e = parse_primary();
        if (has_error) {
            pow_top = base;
            return v;
        }
        if (pow_push(e, operand_start, op_index) != 0) {
            fprintf(stderr, "calc: out of memory\n");
            exit(1);
        }
    }

    /* Combine from the right: a ** (b ** (c ** d)).  For item i the
     * exponent is everything from item i+1 to the end, which starts where
     * item i+1 starts - that is where a negative exponent is reported. */
    Value result = pow_stack[pow_top - 1].value;
    for (size_t i = pow_top - 1; i > base; i--) {
        result = apply_op(T_POW, pow_stack[i].op_index,
                          pow_stack[i - 1].value, result,
                          pow_stack[i].start);
        if (has_error)
            break;
    }
    pow_top = base;
    return result;
}

static Value parse_term(void)
{
    Value left = parse_power();
    while (!has_error && (cur.type == T_STAR || cur.type == T_SLASH)) {
        TokenType op = cur.type;
        size_t op_index = cur.start;
        next_token();
        size_t right_index = cur.start;   /* start of the right operand */
        Value right = parse_power();
        if (has_error)
            break;
        left = apply_op(op, op_index, left, right, right_index);
    }
    return left;
}

static Value parse_expr(void)
{
    Value left = parse_term();
    while (!has_error && (cur.type == T_PLUS || cur.type == T_MINUS)) {
        TokenType op = cur.type;
        size_t op_index = cur.start;
        next_token();
        size_t right_index = cur.start;
        Value right = parse_term();
        if (has_error)
            break;
        left = apply_op(op, op_index, left, right, right_index);
    }
    return left;
}

/* ======================================================================
 *  Evaluating a whole file
 *
 *  The file is processed once from start to end.  Blank lines (only
 *  spaces/tabs) and lines whose first non-blank character is '#' are
 *  skipped, but their characters stay in the array, so they are still
 *  counted in later positions.  Results are collected in `out` and only
 *  written if the whole file had no error; on an error the output is just
 *  ERROR:<pos>, with no partial results.
 * ====================================================================== */
static void append_value(Buffer *out, Value v)
{
    char tmp[64];
    int n;
    if (v.is_float)
        n = snprintf(tmp, sizeof tmp, "%.15g\n", v.f);
    else
        n = snprintf(tmp, sizeof tmp, "%lld\n", v.i);
    if (n < 0 || buf_append(out, tmp, (size_t)n) != 0) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
}

/* Returns 1 if an error was found (error_index is then set). */
static int evaluate(char *data, size_t len, Buffer *out)
{
    src = data;
    src_len = len;
    pos = 0;
    has_error = 0;
    error_index = 0;
    pow_top = 0;

    while (pos < src_len) {
        size_t p = pos;
        while (p < src_len && (src[p] == ' ' || src[p] == '\t'))
            p++;
        if (p >= src_len)
            break;
        if (src[p] == '\n') {
            pos = p + 1;
            continue;
        }
        if (src[p] == '#') {
            while (p < src_len && src[p] != '\n')
                p++;
            pos = (p < src_len) ? p + 1 : p;
            continue;
        }

        lex();
        Value v = parse_expr();
        if (has_error)
            return 1;
        if (cur.type != T_EOL) {   /* e.g. "(1 + 2))" or "4 # text" */
            set_error(cur.start);
            return 1;
        }
        append_value(out, v);
        pos = cur.end;             /* just after the '\n' (or at EOF) */
    }
    return 0;
}

/* ======================================================================
 *  Files and folders
 * ====================================================================== */

static char *copy_n(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    if (!p) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *concat4(const char *a, const char *b, const char *c,
                     const char *d)
{
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c), ld = strlen(d);
    char *p = malloc(la + lb + lc + ld + 1);
    if (!p) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    memcpy(p, a, la);
    memcpy(p + la, b, lb);
    memcpy(p + la + lb, c, lc);
    memcpy(p + la + lb + lc, d, ld);
    p[la + lb + lc + ld] = '\0';
    return p;
}

static int ends_with_txt(const char *name)
{
    size_t n = strlen(name);
    return n >= 4 && strcmp(name + n - 4, ".txt") == 0;
}

/* Last path component without trailing '/' ("../in/" -> "in",
 * "labs/task1.txt" -> "task1.txt").  If strip_txt, a final ".txt" is
 * removed ("task1.txt" -> "task1"). */
static char *base_name(const char *path, int strip_txt)
{
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/')
        end--;
    size_t start = end;
    while (start > 0 && path[start - 1] != '/')
        start--;
    char *name = copy_n(path + start, end - start);
    if (strip_txt && ends_with_txt(name))
        name[strlen(name) - 4] = '\0';
    return name;
}

/* Creates the output folder if it does not exist yet (its parent must
 * exist).  An existing folder is fine.  Returns 0 on success.
 *
 * AI (Claude): prompt "when creating the output directory i need to use mkdir
 *   and combine file paths safely without causing buffer overflows how can i
 *   do this and how do i get the folder name from the input file"
 *   Link: see line 4.  Used: treating EEXIST as success after checking with
 *   stat that the path is a folder. */
static int make_output_dir(const char *dir)
{
    if (mkdir(dir, 0777) == 0)
        return 0;
    int saved = errno;
    struct stat st;
    if (saved == EEXIST && stat(dir, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    fprintf(stderr, "calc: cannot create output folder '%s': %s\n",
            dir, strerror(saved));
    return -1;
}

/* Evaluates one input file and writes
 * <out_dir>/<base>_<name>_<lastname>_<id>.txt.  Returns 0 on success,
 * 1 if the input could not be read or the output could not be written.
 * An ERROR:<pos> result is a normal result and returns 0. */
static int process_file(const char *in_path, const char *out_dir)
{
    Buffer in = {NULL, 0, 0};
    if (read_whole_file(in_path, &in) != 0) {
        fprintf(stderr, "calc: cannot read input file '%s': %s\n",
                in_path, strerror(errno));
        free(in.data);
        return 1;
    }

    Buffer out = {NULL, 0, 0};
    if (evaluate(in.data, in.len, &out)) {
        char tmp[64];
        int n = snprintf(tmp, sizeof tmp, "ERROR:%zu\n", error_index + 1);
        out.len = 0;                       /* drop partial results */
        if (n < 0 || buf_append(&out, tmp, (size_t)n) != 0) {
            fprintf(stderr, "calc: out of memory\n");
            exit(1);
        }
    }
    free(in.data);

    char *base = base_name(in_path, 1);
    char *file_name = concat4(base, "_" STUDENT_NAME "_" STUDENT_LASTNAME
                              "_" STUDENT_ID, ".txt", "");
    char *out_path = concat4(out_dir, "/", file_name, "");
    free(base);
    free(file_name);

    int rc = 0;
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "calc: cannot write '%s': %s\n",
                out_path, strerror(errno));
        rc = 1;
    } else {
        size_t written = out.len ? fwrite(out.data, 1, out.len, f) : 0;
        if (fclose(f) != 0 || written != out.len) {
            fprintf(stderr, "calc: error while writing '%s'\n", out_path);
            rc = 1;
        }
    }
    free(out_path);
    free(out.data);
    return rc;
}

/* Evaluates every *.txt regular file directly inside dir.  The names are
 * collected first and processed afterwards, so result files written while
 * the folder is being read (e.g. with -o pointing into the same folder)
 * are never taken as new input.  Names starting with '.' are skipped,
 * like the shell's *.txt does.
 *
 * AI (Claude): prompt "in the folder mode (-d) i need to loop through all files
 *   but only process .txt files and ignore subfolders or other files like .md
 *   how do i filter files in c when reading a directory"  Link: see line 4.
 *   Used: the .txt name check together with stat and S_ISREG. */
static int process_dir(const char *dir, const char *out_dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "calc: cannot open folder '%s': %s\n",
                dir, strerror(errno));
        return 1;
    }

    char **paths = NULL;
    size_t count = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || !ends_with_txt(e->d_name))
            continue;
        char *path = concat4(dir, "/", e->d_name, "");
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            free(path);                /* subfolder named x.txt etc. */
            continue;
        }
        if (count == cap) {
            size_t new_cap = cap ? cap * 2 : 16;
            char **p = realloc(paths, new_cap * sizeof *p);
            if (!p) {
                fprintf(stderr, "calc: out of memory\n");
                exit(1);
            }
            paths = p;
            cap = new_cap;
        }
        paths[count++] = path;
    }
    closedir(d);

    /* If the output folder cannot be created nothing can be written.
     * Otherwise every file is processed even if one of them fails; the
     * exit code is then non-zero. */
    int rc = 0;
    int can_write = make_output_dir(out_dir) == 0;
    if (!can_write)
        rc = 1;
    for (size_t i = 0; i < count; i++) {
        if (can_write && process_file(paths[i], out_dir) != 0)
            rc = 1;
        free(paths[i]);
    }
    free(paths);
    return rc;
}

/* ======================================================================
 *  Command line
 *
 *  AI (Claude): prompt "how can i parse command line arguments in c to handle
 *    flags like -d for directory and -o for output directory, and what is the
 *    best way to check if the input file exists before starting the process"
 *    Link: see line 4.
 * ====================================================================== */
static void usage(void)
{
    fprintf(stderr,
        "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR]"
        " [input.txt]\n"
        "  calc -o OUTDIR input.txt   evaluate one file\n"
        "  calc -d DIR -o OUTDIR      evaluate every *.txt directly in DIR\n"
        "  without -o the output folder is <input_base>_%s_%s\n",
        STUDENT_NAME, STUDENT_ID);
}

int main(int argc, char *argv[])
{
    const char *dir = NULL;
    const char *out_dir = NULL;
    const char *input = NULL;

    if (argc < 2) {
        usage();
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-d") == 0 || strcmp(a, "--dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "calc: option '%s' needs a folder\n", a);
                usage();
                return 1;
            }
            dir = argv[++i];
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--output-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "calc: option '%s' needs a folder\n", a);
                usage();
                return 1;
            }
            out_dir = argv[++i];
        } else if (a[0] == '-') {
            fprintf(stderr, "calc: unknown option '%s'\n", a);
            usage();
            return 1;
        } else if (input) {
            fprintf(stderr, "calc: only one input file can be given\n");
            usage();
            return 1;
        } else {
            input = a;
        }
    }

    if (dir && input) {
        fprintf(stderr, "calc: give either -d DIR or an input file, not both\n");
        usage();
        return 1;
    }
    if (!dir && !input) {
        fprintf(stderr, "calc: no input file or folder given\n");
        usage();
        return 1;
    }

    /* Default output folder: <input_base>_<name>_<id> in the current
     * folder, where input_base is the file name without .txt, or the
     * folder's name with -d. */
    char *default_out = NULL;
    if (!out_dir) {
        char *b = base_name(dir ? dir : input, dir ? 0 : 1);
        default_out = concat4(b, "_" STUDENT_NAME "_" STUDENT_ID, "", "");
        free(b);
        out_dir = default_out;
    }

    int rc;
    if (dir) {
        rc = process_dir(dir, out_dir);
    } else {
        /* Check the input first so that a missing file does not leave an
         * empty output folder behind. */
        FILE *f = fopen(input, "rb");
        if (!f) {
            fprintf(stderr, "calc: cannot open input file '%s': %s\n",
                    input, strerror(errno));
            rc = 1;
        } else {
            fclose(f);
            rc = make_output_dir(out_dir) != 0 ? 1
                                                : process_file(input, out_dir);
        }
    }

    free(default_out);
    free(pow_stack);
    return rc;
}