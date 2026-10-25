// Name: Derin Atila Student ID: 251ADC003
// (optional) https://github.com/derinatilla/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: The C Programming Language (K&R) book, course lecture notes. No AI tools were used.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

// Global variables to track the file content and position
char *source = NULL;
long file_length = 0;
long current_pos = 0;

// Error handling structure
typedef struct {
    double value;
    int is_float;
    int error;
    long error_pos;
} Result;

// Function prototypes
Result parse_expression();
Result parse_term();
Result parse_power();
Result parse_primary();
Result make_error(long pos);
Result make_number(double val, int is_float);
void skip_whitespace();
char peek();
char advance();

// Tokenizer: skips spaces and comments. 
// Why: We don't have a separate tokenizer, the parser reads directly from the buffer.
void skip_whitespace() {
    while (current_pos < file_length) {
        char c = source[current_pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            current_pos++;
        } else if (c == '#') {
            // Skip comment until end of line
            while (current_pos < file_length && source[current_pos] != '\n') {
                current_pos++;
            }
        } else {
            break;
        }
    }
}

char peek() {
    skip_whitespace();
    if (current_pos >= file_length) return '\0';
    return source[current_pos];
}

char advance() {
    skip_whitespace();
    if (current_pos >= file_length) return '\0';
    return source[current_pos++];
}

Result make_error(long pos) {
    Result r = {0, 0, 1, pos};
    return r;
}

Result make_number(double val, int is_float) {
    Result r = {val, is_float, 0, 0};
    return r;
}

// Grammar Hint: primary ::= NUMBER | '(' expr ')'
Result parse_primary() {
    skip_whitespace();
    if (current_pos >= file_length) return make_error(current_pos + 1);

    char c = source[current_pos];
    
    if (c == '(') {
        current_pos++;
        Result res = parse_expression();
        if (res.error) return res;
        
        skip_whitespace();
        if (current_pos >= file_length || source[current_pos] != ')') {
            return make_error(current_pos + 1); 
        }
        current_pos++;
        return res;
    }

    // Try to parse a number (integer or float)
    if (isdigit(c)) {
        long start = current_pos;
        int is_float = 0;
        
        while (current_pos < file_length && isdigit(source[current_pos])) current_pos++;
        
        if (current_pos < file_length && source[current_pos] == '.') {
            is_float = 1;
            current_pos++;
            if (current_pos >= file_length || !isdigit(source[current_pos])) {
                return make_error(current_pos + 1); // Error at the dot or after
            }
            while (current_pos < file_length && isdigit(source[current_pos])) current_pos++;
        }
        
        long len = current_pos - start;
        char *num_str = malloc(len + 1);
        strncpy(num_str, &source[start], len);
        num_str[len] = '\0';
        
        double val;
        if (is_float) {
            val = strtod(num_str, NULL);
        } else {
            val = (double)strtoll(num_str, NULL, 10);
        }
        free(num_str);
        return make_number(val, is_float);
    }

    // If we don't recognize it, it's a syntax error or invalid character.
    return make_error(current_pos + 1);
}

// Grammar Hint: power ::= primary [ '**' power ]
// Why: ** is right-associative. power() calls itself, grouping as 2**(3**2).
Result parse_power() {
    Result base = parse_primary();
    if (base.error) return base;

    skip_whitespace();
    if (current_pos + 1 < file_length && source[current_pos] == '*' && source[current_pos + 1] == '*') {
        current_pos += 2; // consume '**'
        
        Result exp = parse_power(); // Right-associative
        if (exp.error) return exp;

        if (!base.is_float && !exp.is_float) {
            if (exp.value < 0) {
                return make_error(current_pos); // negative integer exponent error
            }
            double res = pow(base.value, exp.value);
            if (isinf(res) || isnan(res)) return make_error(current_pos); // float result error for ints
            return make_number(res, 0);
        } else {
            double res = pow(base.value, exp.value);
            if (isinf(res) || isnan(res)) return make_error(current_pos);
            return make_number(res, 1);
        }
    }
    return base;
}

// Grammar Hint: term ::= power { ('*' | '/') power }
Result parse_term() {
    Result left = parse_power();
    if (left.error) return left;

    while (1) {
        skip_whitespace();
        if (current_pos >= file_length) break;
        
        char op = source[current_pos];
        // Check for single '*' and '/' (avoid catching '**')
        if ((op == '*' && (current_pos + 1 >= file_length || source[current_pos+1] != '*')) || op == '/') {
            current_pos++;
            
            Result right = parse_power();
            if (right.error) return right;

            if (op == '*') {
                left.value *= right.value;
                left.is_float = left.is_float || right.is_float;
            } else if (op == '/') {
                if (right.value == 0.0) {
                    return make_error(current_pos - (long)strlen(" 0")); // division by zero error pos
                }
                left.value /= right.value;
                left.is_float = left.is_float || right.is_float;
            }
        } else {
            break;
        }
    }
    return left;
}

// Grammar Hint: expr ::= term { ('+' | '-') term }
Result parse_expression() {
    Result left = parse_term();
    if (left.error) return left;

    while (1) {
        skip_whitespace();
        if (current_pos >= file_length) break;
        
        char op = source[current_pos];
        if (op == '+' || op == '-') {
            current_pos++;
            Result right = parse_term();
            if (right.error) return right;

            if (op == '+') {
                left.value += right.value;
            } else {
                left.value -= right.value;
            }
            left.is_float = left.is_float || right.is_float;
        } else {
            break;
        }
    }
    return left;
}

// Helper to extract base name from filename
void get_base_name(const char *filename, char *out_base) {
    strcpy(out_base, filename);
    char *ext = strrchr(out_base, '.');
    if (ext && strcmp(ext, ".txt") == 0) {
        *ext = '\0';
    }
}

int main(int argc, char *argv[]) {
    char *input_file = NULL;
    char *out_dir = NULL;
    char *dir_to_scan = NULL;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 < argc) { out_dir = argv[++i]; }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (i + 1 < argc) { dir_to_scan = argv[++i]; }
        } else {
            input_file = argv[i];
        }
    }

    if (!input_file && !dir_to_scan) {
        fprintf(stderr, "Error: No input file or directory specified.\n");
        return 1;
    }

    // Create output directory if it doesn't exist
    if (out_dir) {
        mkdir(out_dir, 0777); // Ignore error if exists
    }

    // If scanning a directory
    if (dir_to_scan) {
        DIR *d = opendir(dir_to_scan);
        if (!d) {
            fprintf(stderr, "Error: Cannot open directory %s\n", dir_to_scan);
            return 1;
        }
        struct dirent *dir_entry;
        while ((dir_entry = readdir(d)) != NULL) {
            if (strstr(dir_entry->d_name, ".txt") != NULL) {
                // Construct full path
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_to_scan, dir_entry->d_name);
                
                // Read file
                FILE *f = fopen(full_path, "rb");
                if (!f) continue;
                fseek(f, 0, SEEK_END);
                file_length = ftell(f);
                fseek(f, 0, SEEK_SET);
                source = malloc(file_length + 1);
                if (fread(source, 1, file_length, f) != (size_t)file_length) return 1;
                source[file_length] = '\0';
                fclose(f);

                // Process
                current_pos = 0;
                Result res = parse_expression();
                skip_whitespace();
                
                // Check for extra characters at the end
                if (!res.error && current_pos < file_length) {
                    res = make_error(current_pos + 1);
                }

                // Output
                char base[256];
                get_base_name(dir_entry->d_name, base);
                char out_name[1024];
                if (out_dir) {
                    snprintf(out_name, sizeof(out_name), "%s/%s_derin_atila_251ADC003.txt", out_dir, base);
                } else {
                    snprintf(out_name, sizeof(out_name), "%s_derin_atila_251ADC003.txt", base);
                }
                
                FILE *out = fopen(out_name, "w");
                if (out) {
                    if (res.error) fprintf(out, "ERROR:%ld\n", res.error_pos);
                    else if (res.is_float) fprintf(out, "%.15g\n", res.value);
                    else fprintf(out, "%lld\n", (long long)res.value);
                    fclose(out);
                }
                free(source);
            }
        }
        closedir(d);
    } 
    // If processing a single file
    else if (input_file) {
        FILE *f = fopen(input_file, "rb");
        if (!f) {
            fprintf(stderr, "Error: Cannot open file %s\n", input_file);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        file_length = ftell(f);
        fseek(f, 0, SEEK_SET);
        source = malloc(file_length + 1);
        if (fread(source, 1, file_length, f) != (size_t)file_length) return 1;
        source[file_length] = '\0';
        fclose(f);

        current_pos = 0;
        Result res = parse_expression();
        skip_whitespace();
        
        if (!res.error && current_pos < file_length) {
            res = make_error(current_pos + 1);
        }

        char base[256];
        get_base_name(input_file, base);
        char out_name[1024];
        if (out_dir) {
            snprintf(out_name, sizeof(out_name), "%s/%s_derin_atila_251ADC003.txt", out_dir, base);
        } else {
            snprintf(out_name, sizeof(out_name), "%s_derin_atila_251ADC003.txt", base);
        }

        FILE *out = fopen(out_name, "w");
        if (out) {
            if (res.error) fprintf(out, "ERROR:%ld\n", res.error_pos);
            else if (res.is_float) fprintf(out, "%.15g\n", res.value);
            else fprintf(out, "%lld\n", (long long)res.value);
            fclose(out);
        }
        free(source);
    }

    return 0;
}